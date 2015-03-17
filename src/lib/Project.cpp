#include "Project.h"

#include "utility/logging/logging.h"
#include "utility/messaging/type/MessageFinishedParsing.h"
#include "utility/utility.h"

#include "data/access/StorageAccessProxy.h"
#include "data/graph/Token.h"
#include "data/parser/cxx/CxxParser.h"
#include "settings/ApplicationSettings.h"
#include "settings/ProjectSettings.h"

std::shared_ptr<Project> Project::create(StorageAccessProxy* storageAccessProxy)
{
	std::shared_ptr<Project> ptr(new Project(storageAccessProxy));
	ptr->clearStorage();
	return ptr;
}

Project::~Project()
{
}

bool Project::loadProjectSettings(const std::string& projectSettingsFile)
{
	bool success = ProjectSettings::getInstance()->load(projectSettingsFile);
	if(success)
	{
		m_projectSettingsFilepath = projectSettingsFile;
	}
	return success;
}

bool Project::saveProjectSettings( const std::string& projectSettingsFile )
{
	if(!projectSettingsFile.empty())
	{
		m_projectSettingsFilepath = projectSettingsFile;
		ProjectSettings::getInstance()->save(projectSettingsFile);
	}
	else if (!m_projectSettingsFilepath.empty())
	{
		ProjectSettings::getInstance()->save(m_projectSettingsFilepath);
	}
	else
	{
		return false;
	}
	LOG_INFO_STREAM(<< "Projectsettings saved in File: " << m_projectSettingsFilepath);
	return true;
}

void Project::clearProjectSettings()
{
	m_projectSettingsFilepath.clear();
	ProjectSettings::getInstance()->clear();
}

bool Project::setSourceDirectoryPath(const std::string& sourceDirectoryPath)
{
	m_projectSettingsFilepath = sourceDirectoryPath + "/ProjectSettings.xml";
	return ProjectSettings::getInstance()->setSourcePaths(std::vector<std::string>(1, sourceDirectoryPath));
}

void Project::clearStorage()
{
	m_storage = std::make_shared<Storage>();
	m_storageAccessProxy->setSubject(m_storage.get());

	Token::resetNextId();
}

void Project::parseCode()
{
	std::shared_ptr<ProjectSettings> projSettings = ProjectSettings::getInstance();
	std::shared_ptr<ApplicationSettings> appSettings = ApplicationSettings::getInstance();

	std::vector<std::string> sourcePaths = projSettings->getSourcePaths();
	if (!sourcePaths.size())
	{
		return;
	}

	std::vector<std::string> includePaths(sourcePaths);

	// TODO: move this creation to another place (after projectsettings have been loaded)
	if (!m_fileManager)
	{
		std::vector<std::string> sourceExtensions;
		sourceExtensions.push_back(".cpp");
		sourceExtensions.push_back(".cc");

		std::vector<std::string> includeExtensions;
		includeExtensions.push_back(".h");
		includeExtensions.push_back(".hpp");

		m_fileManager = std::make_shared<FileManager>(sourcePaths, includePaths, sourceExtensions, includeExtensions);
	}

	m_fileManager->fetchFilePaths();
	std::set<FilePath> addedFilePaths = m_fileManager->getAddedFilePaths();
	std::set<FilePath> updatedFilePaths = m_fileManager->getUpdatedFilePaths();
	std::set<FilePath> removedFilePaths = m_fileManager->getRemovedFilePaths();

	utility::append(updatedFilePaths, m_storage->getDependingFilePathsAndRemoveFileNodes(updatedFilePaths));
	utility::append(updatedFilePaths, m_storage->getDependingFilePathsAndRemoveFileNodes(removedFilePaths));

	m_storage->clearFileData(updatedFilePaths);
	m_storage->clearFileData(removedFilePaths);

	std::vector<FilePath> filesToParse;
	filesToParse.insert(filesToParse.end(), addedFilePaths.begin(), addedFilePaths.end());
	filesToParse.insert(filesToParse.end(), updatedFilePaths.begin(), updatedFilePaths.end());

	if (filesToParse.size() == 0)
	{
		MessageFinishedParsing(0, 0, m_storage->getErrorCount()).dispatch();
		return;
	}

	Parser::Arguments args;

	utility::append(args.compilerFlags, projSettings->getCompilerFlags());
	utility::append(args.compilerFlags, appSettings->getCompilerFlags());

	// Add the include paths as HeaderSearchPaths as well, so clang will also look here when searching include files.
	utility::append(args.systemHeaderSearchPaths, includePaths);
	utility::append(args.systemHeaderSearchPaths, projSettings->getHeaderSearchPaths());
	utility::append(args.systemHeaderSearchPaths, appSettings->getHeaderSearchPaths());

	utility::append(args.frameworkSearchPaths, projSettings->getFrameworkSearchPaths());
	utility::append(args.frameworkSearchPaths, appSettings->getFrameworkSearchPaths());

	CxxParser parser(m_storage.get(), m_fileManager.get());

	float duration = utility::duration(
		[&]()
		{
			parser.parseFiles(filesToParse, args);
		}
	);

	// m_storage->logGraph();
	// m_storage->logLocations();

	MessageFinishedParsing(filesToParse.size(), duration, m_storage->getErrorCount()).dispatch();
}

Project::Project(StorageAccessProxy* storageAccessProxy)
	: m_storageAccessProxy(storageAccessProxy)
{
}
