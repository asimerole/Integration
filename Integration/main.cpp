#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS
#define BOOST_USE_STATIC_LIBS
#define CURL_STATICLIB

#include <fstream>
#include <string>
#include <atomic>
#include <locale>
#include <nlohmann/json.hpp>
#include <db_connection.h>
#include "integration_handler.h"
#include "ftp_handler.h"
#include "mail_handler.h"
#include "onedrive_handler.h"
#include "utils.h"
#include "resource.h"
#include <WinUser.h>

std::atomic<bool> integrationIsActive(false);
std::atomic<bool> ftpIsActive(false);
std::atomic<bool> mailIsActive(false);
std::atomic<bool> oneDriveIsActive(false);
std::atomic<bool> isRebuilingNow(false);
std::atomic<bool> firstLaunch(true);
std::atomic<bool> dbIsFull(false);
std::atomic<bool> fastDbBuild(false);

std::thread oneDriveThread;
std::thread ftpThread;
std::thread integrationThread;

using json = nlohmann::json;

const std::wstring FTP_RECIEVED_DIR = L"/Cache";

const LPCWSTR APP_VERSION = L"v1.04.1 від 25.07.2025"; 

/// <summary>
/// Installing modules activity from the database
/// </summary>
/// <param name="integrationIsActive"> For integration </param>
/// <param name="ftpIsActive"> For FTP module </param>
/// <param name="mailIsActive"> for mailing module </param>
/// <param name="oneDriveIsActive">For the operation of the oneDrive module, namely deleting files</param>

void setModuleActivity(std::atomic<bool>& integrationIsActive,
    std::atomic<bool>& ftpIsActive,
    std::atomic<bool>& mailIsActive,
    std::atomic<bool>& oneDriveIsActive,
    std::atomic<bool>& dbIsFull) {
    static std::mutex dbMutex;
    std::lock_guard<std::mutex> lock(dbMutex);

    try {
        Database db;
        if (!db.connectToDatabase()) {
            logError(L"DB connection failed", LOG_PATH);
            return;
        }

        SQLHDBC dbc = db.getConnectionHandle();
        if (!dbc) {
            logError(L"Invalid DB handle", LOG_PATH);
            return;
        }

        std::wstring activityJson;
        try {
            activityJson = Database::getJsonConfigFromDatabase("file_integration", dbc);
            if (activityJson.empty()) {
                logError(L"Empty JSON config", LOG_PATH);
                return;
            }
        }
        catch (...) {
            logError(L"Failed to get JSON config", LOG_PATH);
            return;
        }

        try {
            json configJson = json::parse(activityJson.begin(), activityJson.end());

            // Simplified Safe Flag Installation
            auto safeSetFlag = [&](const char* key, auto& flag) {
                try {
                    if (configJson.contains(key)) {
                        flag.store(configJson[key].template get<bool>());
                    }
                }
                catch (...) {
                    logError(L"Error setting flag " + stringToWString(key), LOG_PATH);
                }
                };

            safeSetFlag("integrationIsActive", integrationIsActive);
            safeSetFlag("ftpIsActive", ftpIsActive);
            safeSetFlag("mailIsActive", mailIsActive);
            safeSetFlag("oneDriveIsActive", oneDriveIsActive);
            safeSetFlag("dbIsFull", dbIsFull);

        }
        catch (const std::exception& e) {
            logError(L"JSON parse error: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
        }
    }
    catch (...) {
        logError(L"Critical error in setModuleActivity", EXCEPTION_LOG_PATH);
    }
}

void updateModuleActivity(std::atomic<bool>& integrationIsActive,
    std::atomic<bool>& ftpIsActive,
    std::atomic<bool>& mailIsActive,
    std::atomic<bool>& oneDriveIsActive,
    std::atomic<bool>& dbIsFull)
{
    Database db;
    db.connectToDatabase();
    SQLHDBC dbc = db.getConnectionHandle();

    // Generate new JSON with unique values
    json updatedConfig = {
        {"integrationIsActive", integrationIsActive.load()},
        {"ftpIsActive", ftpIsActive.load()},
        {"mailIsActive", mailIsActive.load()},
        {"oneDriveIsActive", oneDriveIsActive.load()},
        {"dbIsFull", dbIsFull.load()}
    };

    // Convert JSON to String
    std::string jsonString = updatedConfig.dump();

    // Preparing a SQL query to update the database
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring wJsonString = converter.from_bytes(jsonString);

    std::wstring query = L"UPDATE [ReconDB].[dbo].[access_settings] "
        L"SET value = ? "
        L"WHERE name = 'file_integration'";

    SQLHSTMT stmt;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        logError(L"Failed to allocate SQL statement handle.", LOG_PATH);
        return;
    }

    // Parameter binding
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        wJsonString.length(), 0, (SQLPOINTER)wJsonString.c_str(), wJsonString.length() * sizeof(wchar_t), nullptr);

    // Executing a request
    if (SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS) == SQL_SUCCESS) {
        std::wcout << L"Configuration updated successfully.\n";
    }
    else {
        logError(L"Failed to update configuration in the database.", LOG_PATH);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    db.disconnectFromDatabase();
}

/// <summary>
/// Integration module launch method
/// </summary>
static void runIntegration() {
    try {
        Database db;
        db.connectToDatabase();
        if (!db.isConnected()) {
			logError(L"[Integration] Failed to connect to the database.", INTEGRATION_LOG_PATH);
            throw;
        }
        std::wstring rootFolder = db.getPathFromDbByName(db.getConnectionHandle(), L"root_directory");
		std::wstring pathToWinRec = db.getPathFromDbByName(db.getConnectionHandle(), L"winrec-bs");

        while (integrationIsActive || !isRebuilingNow) {
            std::unordered_set<std::wstring> parentFolders;
            std::wstring filename;
            try {
                size_t fileCount = 0;
                const size_t maxFilesPerBatch = 1000;

                // If it's the first launch, we go through all the files
                if (firstLaunch && !dbIsFull) {
                    //logIntegrationError(L"[Integration] First launch started. Building directory tree...");
                    Integration::collectRootPaths(parentFolders, rootFolder);

                    for (const auto& path : parentFolders) {
                        if (!integrationIsActive) {
                            if (isRebuilingNow) return;
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }

                        for (const auto& entry : fs::recursive_directory_iterator(path)) {
                            if (!integrationIsActive) {
                                if (isRebuilingNow) return;
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                                continue;
                            }

                            if(fastDbBuild){
                                std::wstring fileDate = getFileDate(entry.path().wstring());
                                if (!isWithinLastNDays(fileDate, 60)) {
                                    continue;
                                }
                            }

                            FileInfo fileInfo;
                            try {
                                Integration::collectInfo(fileInfo, entry, rootFolder, pathToWinRec, db.getConnectionHandle());
                                Integration::sortFiles(fileInfo);
                                Integration::fileIntegrationDB(db.getConnectionHandle(), fileInfo, mailIsActive, dbIsFull);
                                fileInfo.files.clear();
                            }
                            catch (...) {
                                fileInfo.files.clear();
                                throw;
                            }

                        }
                    }

                    if (isRebuilingNow) return;

                    firstLaunch = false;
                    dbIsFull = true;
                    updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);
                    logError(L"[Integration] Database is now full. Switching to external cache processing.", INTEGRATION_LOG_PATH);
                    parentFolders.clear();
                }
                else {
                    // If it's not the first launch, then it's the second one and we process only the "Cache" folder
                    const std::wstring pathCache = rootFolder + FTP_RECIEVED_DIR;
                    logError(L"[Integration] Second launch was started... Path to folder is: " + pathCache, INTEGRATION_LOG_PATH);

                    if (!fs::exists(pathCache)) {
                        fs::create_directory(pathCache);
                    }

                    for (const auto& entry : fs::directory_iterator(pathCache)) {
                        if (!integrationIsActive) {
                            if (isRebuilingNow) return;
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }
                        if (fileCount++ >= maxFilesPerBatch) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            fileCount = 0;
                            break;
                        }
                        logError(L"[Integration] cycle 'for in integration' was started for path: " + pathCache, INTEGRATION_LOG_PATH);

                        // Check that the file does not end in ".meta"
                        if (entry.path().extension() == L".meta") {
                            continue;
                        }

                        if (!fs::exists(entry.path())) {
                            continue;
                        }

                        // Waiting for file writing to complete
                        std::uintmax_t previousSize = 0;
                        std::uintmax_t currentSize = fs::file_size(entry.path());
                        int retries = 5;

                        while (retries-- > 0 && previousSize != currentSize) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            previousSize = currentSize;
                            currentSize = fs::file_size(entry.path());
                        }

                        if (previousSize != currentSize) {
                            logError(L"Error: file " + entry.path().wstring() + L" still changing, skipping.", INTEGRATION_LOG_PATH);
                            continue;
                        }

                        filename = entry.path().filename().wstring();
                        std::wstring metaFileJson, metaFilePath, finalFilePath;
                        metaFilePath = entry.path().parent_path().wstring() + L"/" + entry.path().filename().wstring() + L".meta";
                        
                        if (!fs::exists(metaFilePath)) {
                            logError(L"Meta file was not found. File name: " + filename, INTEGRATION_LOG_PATH);
                            int reconNum = 0;
                            if (filename.size() >= 8) {
                                std::wstring numStr = filename.substr(5, 3);
                                try {
                                    reconNum = std::stoi(numStr);
                                }
                                catch (const std::exception& e) {
                                    logError(L"Error while converting: " + stringToWString(e.what()), INTEGRATION_LOG_PATH);
                                }
                            }
                            finalFilePath = Integration::getPathByRNumber(reconNum, db.getConnectionHandle());
                        }
                        else {

                            std::ifstream metaFile(metaFilePath, std::ios::binary);

                            if (!metaFile) {
                                logError(L"Error: Failed to open file " + metaFilePath, INTEGRATION_LOG_PATH);
                                continue;
                            }

                            // Reading the entire JSON file into a string
                            std::string jsonContent((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());
                            metaFile.close();

                            if (jsonContent.empty()) {
                                logError(L"Error: file " + entry.path().wstring() + L" is empty.", INTEGRATION_LOG_PATH);
                                continue;
                            }

                            json configJson;
                            try {
                                configJson = json::parse(jsonContent);
                            }
                            catch (const json::exception& e) {
                                logError(L"Parsing error JSON: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                                continue;
                            }
                            if (configJson.contains("targetPath") && configJson["targetPath"].is_string()) {
                                try {
                                    finalFilePath = utf8_to_wstring(configJson["targetPath"].get<std::string>());
                                }
                                catch (const std::exception& e) {
                                    logError(L"Error converting string to wstring: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                                    continue;
                                }
                            }
                            else {
                                logError(L"Error: Field 'targetPath' is missing or not a string.", INTEGRATION_LOG_PATH);
                                continue;
                            }
                        }

                        if (finalFilePath.empty()) {
                            logError(L"[Integration] finalFilePath for '" + filename + L"' is empty. Skiping...", INTEGRATION_LOG_PATH);
                            continue;
                        }

                        FileInfo fileInfo;
                        try {
                            Integration::collectInfo(fileInfo, entry, rootFolder, pathToWinRec, db.getConnectionHandle());
                        }
                        catch (...) {
                            fileInfo.files.clear();
                            logError(L"Exception in collect info (2)", EXCEPTION_LOG_PATH);
                            throw;
                        }                 
                        bool movingIsSuccessful = false;
                        // Function for copying and deleting a file
                        auto moveFile = [&](const std::wstring& fileName) {
                            std::wstring fullFilePath = entry.path().parent_path().wstring() + L"/" + fileName;
                            std::wstring metaFilePath = fullFilePath + L".meta";

                            if (!fs::exists(finalFilePath)) {
                                logError(L"[MoveFile] Final path for '" + fileName + L"' was not found. Please check meta file.", INTEGRATION_LOG_PATH);
                                return;
                            }

                            try {
                                fs::copy(fullFilePath, finalFilePath + L"/" + fileName, fs::copy_options::overwrite_existing);
                                fs::remove(fullFilePath);
                                if (fs::exists(metaFilePath)) {
                                    fs::remove(metaFilePath);
                                }
                                movingIsSuccessful = true;
                            }
                            catch (const std::exception& e) {
                                movingIsSuccessful = false;
                                logError(L"[MoveFile] Exception while moving file '" + fileName + L"' from '" + fullFilePath + L"' to '" +
                                    finalFilePath + L"/" + fileName + L"': " + utf8_to_wstring(e.what()), EXCEPTION_LOG_PATH);
                            }
                            };


                            for (auto& file : fileInfo.files) {
                                file->parentFolderPath = finalFilePath;
                                file->processPath(rootFolder);

                                // Moving files
                                moveFile(file->fileName);
                            }

                        try {
                            if (movingIsSuccessful) Integration::sortFiles(fileInfo);
                            if (movingIsSuccessful) Integration::fileIntegrationDB(db.getConnectionHandle(), fileInfo, mailIsActive, dbIsFull);
                            fileInfo.files.clear();
                        }
                        catch (...) {
                            fileInfo.files.clear();
                            logError(L"Exception functions sort or file integration (2).", EXCEPTION_LOG_PATH);
                            throw;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::minutes(1));
                }
            }
            catch (const std::exception& e) {
                logError(L"Exception in Integration module: " + stringToWString(e.what()) + stringToWString(" With File: ") + filename, EXCEPTION_LOG_PATH);
                //db.disconnectFromDatabase();
                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
        }
        db.disconnectFromDatabase();
    }
    catch (const std::exception& e) {
        logError(L"Exception caught in runIntegration setup: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

/// <summary>
/// Method of launching FTP module
/// </summary>
static void StartFtpModule() {
    try {
        Database db;
        int feeding_time = 0;
        std::string rootFolder;

        while (ftpIsActive) {
            if (!ftpIsActive) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            db.connectToDatabase();
            SQLHDBC dbc = db.getConnectionHandle();

            // Check the connection to the database before performing operations
            if (!db.isConnected()) {
                logError(L"[FTP]: Database connection lost. Reconnecting...", LOG_PATH);
                db.connectToDatabase();
                dbc = db.getConnectionHandle();
            }

            feeding_time = db.getCycleTimeFromDB(dbc);
            rootFolder = wstringToString(db.getPathFromDbByName(dbc, L"root_directory"));
            std::string oneDriveJson = wstringToUtf8(Database::getJsonConfigFromDatabase("onedrive", dbc));
            auto oneDriveConfig = parseOneDriveConfig(oneDriveJson);

            std::vector<ServerInfo> servers;
            //logError(L"[FTP]: Collecting server information...");
            Ftp::getInstance().collectServers(servers, dbc);
            //logError(L"[FTP]: Number of servers collected: " + std::to_wstring(servers.size()));
            std::wstring ftpCacheDirPath = stringToWString(rootFolder)  +  FTP_RECIEVED_DIR;
            if (!fs::exists(ftpCacheDirPath)) {
                fs::create_directory(ftpCacheDirPath);
            }

            // Logging operations on each server
            for (auto& server : servers) {
                if (!ftpIsActive) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }

                Ftp::getInstance().createLocalDirectoryTree(server, rootFolder);
                Ftp::getInstance().createLocalDirectoryTree(server, wstringToString(oneDriveConfig.oneDrivePath));

                std::string remoteFolderPath = Ftp::getInstance().encodeURL(wstringToString(server.remoteFolderPath));
                std::string url = wstringToString(Ftp::getInstance().protocol()) + wstringToString(server.ip) + "/" +
                    remoteFolderPath + "/";

                Ftp::getInstance().fileTransfer(server, url, oneDriveConfig.oneDrivePath, ftpIsActive, oneDriveIsActive, dbc, ftpCacheDirPath);

            }

            servers.clear();
            db.disconnectFromDatabase();
            std::this_thread::sleep_for(std::chrono::seconds(feeding_time));
        }

        // logError("[FTP]: Disconnecting from database...");
        //db.disconnectFromDatabase();
        // logError("[FTP]: Disconnected from database successfully.");
    }
    catch (const std::exception& e) {
        logError(L"[FTP]: Exception caught in StartFtpModule setup: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

/// <summary>
/// Method to launch OneDrive module
/// </summary>
static void  StartOneDriveModule() {
    try {
        Database db;
        db.connectToDatabase();
        SQLHDBC dbc = db.getConnectionHandle();
        while (oneDriveIsActive) {
            if (!oneDriveIsActive) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            std::unordered_set<std::wstring> parentFolders;
            try {
                std::string oneDriveJson = wstringToUtf8(Database::getJsonConfigFromDatabase("onedrive", dbc));
                auto oneDriveConfig = parseOneDriveConfig(oneDriveJson);
                Integration::collectRootPaths(parentFolders, oneDriveConfig.oneDrivePath);
                for (const auto& path : parentFolders) {
                    if (!oneDriveIsActive) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }

                    for (const auto& entry : fs::recursive_directory_iterator(path)) {
                        if (!oneDriveIsActive) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }

                        //FilePair pair;
                        FileInfo fileInfo;

                        std::wstring fullPathToFile = entry.path().wstring();
                        std::wstring fileName = entry.path().filename().wstring();
                        auto expressFile = std::make_shared<ExpressFile>();

                        // If starting with prefix "REXPR"
                        if (fileName.rfind(L"REXPR", 0) == 0) { 
                            expressFile->fileName = fileName;
                            expressFile->fullPath = fullPathToFile;

                            expressFile->readDataFromFile();

                        }
                        else if (fileName.rfind(L"RECON", 0) == 0) { // Если начинается на "RECON"
                            std::wstring correspondingRexprFile = L"REXPR" + fileName.substr(5); // Заменяем префикс "RECON" на "REXPR"
                            std::wstring correspondingRexprPath = entry.path().parent_path().wstring() + correspondingRexprFile;

                            expressFile->fileName = correspondingRexprFile;
                            expressFile->fullPath = correspondingRexprPath;

                            if (fs::exists(correspondingRexprPath)) {
                                expressFile->readDataFromFile();
                            }
                            else {
                                Integration::runExternalProgramWithFlag(pathToOMPExecutable, fullPathToFile);
                            }
                        }

                        deleteOldFiles(fullPathToFile, oneDriveConfig.monthCount, expressFile->date);
                    }
                }
                parentFolders.clear();
            }
            catch (const std::exception& e) {
                logError(L"Exception in StartOneDriveModule module: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                db.disconnectFromDatabase();
                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
            std::this_thread::sleep_for(std::chrono::minutes(60));
        }
        db.disconnectFromDatabase();
    }
    catch (const std::exception& e) {
        logError(L"Exception caught in StartOneDriveModule setup: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

// General exception handler for threads
void ThreadExceptionHandler(const std::string& thread_name, const std::exception& e) {
    logError(L"Exception in " + stringToWString(thread_name) + L": " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
}

void startOneDriveThread() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    if (oneDriveThread.joinable()) {
        return;
    }

    oneDriveIsActive = true;
    oneDriveThread = std::thread([]() {
        try {
            StartOneDriveModule();
        }
        catch (const std::exception& e) {
            ThreadExceptionHandler("OneDrive", e);
        }
        catch (...) {
            logError(L"Unknown exception in OneDrive thread", EXCEPTION_LOG_PATH);
        }
        });
}

void startFTPThread() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    if (ftpThread.joinable()) {
        return;
    }

    ftpIsActive = true;
    ftpThread = std::thread([]() {
        try {
            StartFtpModule();
        }
        catch (const std::exception& e) {
            ThreadExceptionHandler("FTP", e);
        }
        catch (...) {
            logError(L"Unknown exception in FTP thread", EXCEPTION_LOG_PATH);
        }
        });
}

void startIntegrationThread() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    if (integrationThread.joinable()) {
        return;
    }

    integrationIsActive = true;
    integrationThread = std::thread([]() {
        try {
            runIntegration();
        }
        catch (const std::exception& e) {
            ThreadExceptionHandler("Integration", e);
        }
        catch (...) {
            logError(L"Unknown exception in Integration thread", EXCEPTION_LOG_PATH);
        }
        });
}

void stopIntegration() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    if (!integrationThread.joinable()) {
        return;
    }

    integrationIsActive = false;
    firstLaunch = true;
    isRebuilingNow = true;

    try {
        integrationThread.join();
    }
    catch (const std::exception& e) {
        logError(L"Error joining integration thread: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }

    isRebuilingNow = false;
}

/// <summary>
/// Rebuilding the database
/// </summary>
static bool rebuildDatabase()
{
    try {
        // Waiting for confirmation from the user
        std::wstring message = L"Ви впевнені, що хочете продовжити?";
        std::wstring title = L"Підтвердження";
        if (!showConfirmationDialog(message, title)) {
            logError(L"User canceled the rebuild process.", LOG_PATH);
            return false; // Exit if user has not confirmed
        }

        message = L"Використати прискорену сбірку бази?";
        title = L"Спосіб перезбірки";
        if (showConfirmationDialog(message, title)) {
			fastDbBuild = true;
        }

        stopIntegration();

        Database db;
        db.connectToDatabase();

        // Forming a request to delete data and reset identifiers
        std::wstringstream sql;
        sql << L"BEGIN TRANSACTION;\n"
            << L"DELETE FROM [ReconDB].[dbo].[data];\n"
            << L"DELETE FROM [ReconDB].[dbo].[struct_units];\n"
            << L"DELETE FROM [ReconDB].[dbo].[struct];\n"
            << L"DELETE FROM [ReconDB].[dbo].[units];\n"
            << L"DBCC CHECKIDENT ('[ReconDB].[dbo].[units]', RESEED, 0);\n"
            << L"DBCC CHECKIDENT ('[ReconDB].[dbo].[struct]', RESEED, 0);\n"
            << L"DBCC CHECKIDENT ('[ReconDB].[dbo].[data]', RESEED, 0);\n"
            << L"COMMIT;";

        db.executeSQL(db.getConnectionHandle(), sql);

        // Checking database cleanup with waiting
        std::wstringstream checkSql;
        checkSql << L"SELECT COUNT(*) FROM [ReconDB].[dbo].[data];";

        int maxRetries = 30; // We wait for a maximum of 30 attempts (30 seconds)
        int result = -1;
        while (maxRetries-- > 0) {
            result = db.executeSQLAndGetIntResult(db.getConnectionHandle(), checkSql);
            if (result == 0) {
                break; // Cleaning completed
            }
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait 1 second and try again
        }

        db.disconnectFromDatabase();
		bool startIntegrationAfterRebuild = true;

        // Logging the results of the check
        if (result == 0) {
            // Waiting for confirmation from the user
            std::wstring message = L"Базу даних було успішно очищено. Запустити модуль інтеграції файлів?";
            std::wstring title = L"Повторну збірку завершено";
            startIntegrationAfterRebuild = showConfirmationDialog(message, title);
        }
        if (startIntegrationAfterRebuild) {
            startIntegrationThread();
        } 

        dbIsFull = false;
        updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);

        return result == 0;
    }
    catch (const std::exception& e) {
        logError(L"Exception in rebuildDatabase: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
        return false;
    }
}


HWND hwndMain = NULL;
HWND hwndAuth = NULL;

void updateMenuDuringRebuild(HWND hwndMain, bool isRebuilding) {
    HMENU hMenu = GetSubMenu(GetMenu(hwndMain), 0); // We get the first submenu
    if (!hMenu) return;

    // Updating the text of the rebuild button
    ModifyMenuW(hMenu, WM_USER + 3, MF_BYPOSITION | MF_STRING, WM_USER + 3, isRebuilding ? L"Повторна збірка (идет персборка...)" : L"Повторна збірка");

    // Turn buttons off or on
    EnableMenuItem(hMenu, WM_USER + 4, isRebuilding ? MF_GRAYED : MF_ENABLED); // Disable/enable the "Integration into DB" button
    EnableMenuItem(hMenu, WM_USER + 3, isRebuilding ? MF_GRAYED : MF_ENABLED); // Disable/enable the "Rebuild" button

    // Updating the menu
    DrawMenuBar(hwndMain);
}

void rebuildDatabaseAsync()
{
    if (isRebuilingNow)
        return;
    isRebuilingNow = true;

    // Refreshing the menu to show that the rebuild has begun
    updateMenuDuringRebuild(hwndMain, true);

    // We start rebuilding the database in the main thread
    std::thread([]() {
        bool success = rebuildDatabase(); // We are rebuilding the database

        isRebuilingNow = false;
        firstLaunch = true;

        // Sending a message to the main thread
        if (success) {
            PostMessage(hwndMain, WM_USER + 100, 0, 0); //Successful rebuild
        }
        else {
            PostMessage(hwndMain, WM_USER + 101, 0, 0); // Rebuild error
        }

        // We update the menu after the rebuild is complete
        PostMessage(hwndMain, WM_USER + 102, 0, 0); // Reassembly completed
        }).detach(); // The stream will be automatically deleted upon completion.
}


/// <summary>
/// WIN API finctions
/// </summary>
/// Global variables
static NOTIFYICONDATAW nid;
HINSTANCE g_hInstance;

void backgroundTask() {
    try {
        setModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);

        if (ftpIsActive.load()) startFTPThread();
        if (integrationIsActive.load()) startIntegrationThread();
        if (oneDriveIsActive.load()) startOneDriveThread();
    }
    catch (...) {
        logError(L"Exception in backgroundTask", EXCEPTION_LOG_PATH);
    }
}


static void createTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, L"Recon-Integration");

    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void createPopupMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, APP_VERSION);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL); // Separator

    AppendMenuW(hMenu, MF_STRING | (oneDriveIsActive ? MF_CHECKED : 0), WM_USER + 7, L"OneDrive - ввим/вимк");
    AppendMenuW(hMenu, MF_STRING | (mailIsActive ? MF_CHECKED : 0), WM_USER + 6, L"Поштова розсилка - ввим/вимк");
    AppendMenuW(hMenu, MF_STRING | (ftpIsActive ? MF_CHECKED : 0), WM_USER + 5, L"Сбір - ввим/вимк");                  
    AppendMenuW(hMenu, MF_STRING | (integrationIsActive ? MF_CHECKED : 0), WM_USER + 4, L"Інтеграція в БД - ввим/вимк");   
    AppendMenuW(hMenu, MF_STRING, WM_USER + 3, L"Повторна збірка");                                                        
    AppendMenuW(hMenu, MF_STRING, WM_USER + 2, L"Вихід");

    SetMenuDefaultItem(hMenu, WM_USER + 2, FALSE);
    SetForegroundWindow(hwnd);
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

bool isAuthorized = false;

HWND hwndLoginEdit = NULL;
HWND hwndPasswordEdit = NULL;
HWND hwndShowPasswordCheckbox = NULL;
HWND hwndLoginButton = NULL;

std::string hashPassword(const std::string& password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.c_str()), password.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    return ss.str();
}

bool CheckUserInDB(SQLHDBC dbc, std::wstring login, std::wstring& password) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(stringToWString("Failed to allocate SQL statement handle: insertInUnitTable"), INTEGRATION_LOG_PATH);
        return false;
    }

    const wchar_t* sqlQuery = LR"(
        SELECT [password]
        FROM [ReconDB].[dbo].[users] WHERE [login] = ?
        AND [type] = 'Адмін'
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false;
    }
    std::wstring hashedPassword = stringToWString(hashPassword(wstringToString(password)));
    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)login.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false;
    }

    wchar_t passwordFromDb[256] = { 0 };
    ret = SQLFetch(hstmt);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false; // User not found
    }

    ret = SQLGetData(hstmt, 1, SQL_C_WCHAR, passwordFromDb, sizeof(passwordFromDb), nullptr);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return false;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

    return hashedPassword.compare(passwordFromDb) == 0;
}

bool CheckCredentials(std::wstring &login, std::wstring &password) {
    if (hwndLoginEdit == NULL || hwndPasswordEdit == NULL) {
        MessageBox(NULL, L"Не вдалося знайти елементи введення", L"Помилка", MB_OK | MB_ICONERROR);
        return false;
    }
    Database db;
    db.connectToDatabase();
    SQLHDBC dbc = db.getConnectionHandle();
    if (!db.isConnected()) {
        MessageBox(NULL, L"Не вдалося підключитися до бази даних", L"Помилка", MB_OK | MB_ICONERROR);
    }

    wchar_t loginBuffer[256];
    wchar_t passwordBuffer[256];
    GetWindowTextW(hwndLoginEdit, loginBuffer, 256);
    GetWindowTextW(hwndPasswordEdit, passwordBuffer, 256);
    login = loginBuffer;
    password = passwordBuffer;

    if (CheckUserInDB(dbc, login, password)) {
        db.disconnectFromDatabase();
        return true;
    }
    db.disconnectFromDatabase();
    return false;
}

LRESULT CALLBACK AuthWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        CreateWindowW(TEXT("STATIC"), L"Логін:", WS_VISIBLE | WS_CHILD,
            20, 20, 100, 20, hwnd, NULL, NULL, NULL);

        hwndLoginEdit = CreateWindowW(TEXT("EDIT"), L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            20, 45, 200, 25, hwnd, NULL, NULL, NULL);

        CreateWindowW(TEXT("STATIC"), L"Пароль:", WS_VISIBLE | WS_CHILD,
            20, 80, 100, 20, hwnd, NULL, NULL, NULL);

        hwndPasswordEdit = CreateWindowW(TEXT("EDIT"), L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
            20, 105, 200, 25, hwnd, NULL, NULL, NULL);

        CreateWindowW(TEXT("button"), TEXT("Показати"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
            225, 110, 100, 15, hwnd, (HMENU) 2, NULL, NULL);
		CheckDlgButton(hwnd, 2, BST_UNCHECKED); 

        CreateWindowW(TEXT("button"), TEXT("Запам'ятати пароль"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
            20, 135, 160, 20, hwnd, (HMENU)3, NULL, NULL);
        CheckDlgButton(hwnd, 3, BST_CHECKED);

        hwndLoginButton = CreateWindowW(TEXT("button"), L"Увійти", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            20, 160, 100, 30, hwnd, (HMENU)IDOK, NULL, NULL);

        std::wstring login;
        std::wstring password;

        loadCredentials(login, password);

        SetWindowTextW(hwndLoginEdit, login.c_str());
        SetWindowTextW(hwndPasswordEdit, password.c_str());

        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case IDOK: {
		        std::wstring login, password;
                if (CheckCredentials(login, password)) {
                    isAuthorized = true;
                    BOOL checked = IsDlgButtonChecked(hwnd, 3);
                    if (checked) {
		    		    saveCredentailsToHash(login, password);
                    }
                    else {
                        deleteCredentialsFromRegistry();
                    }
                    DestroyWindow(hwnd);
                }
                else {
                    MessageBox(hwnd, L"Невірний логін або пароль", L"Помилка", MB_OK | MB_ICONERROR);
                }
                break;
            }
            case 2: {
                BOOL checked = IsDlgButtonChecked(hwnd, 2);

                wchar_t buf[256];
                GetWindowTextW(hwndPasswordEdit, buf, 256);

                DestroyWindow(hwndPasswordEdit);

                DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
                if (checked) {
                    style |= ES_PASSWORD;
                }
                else {
                    style &= ~ES_PASSWORD;
                }

                hwndPasswordEdit = CreateWindowW(
                    TEXT("EDIT"), buf, style,
                    20, 105, 200, 25, hwnd, NULL, NULL, NULL
                );

                CheckDlgButton(hwnd, 2, checked ? BST_UNCHECKED : BST_CHECKED);
                break;
            }
            case 3: {
                BOOL checked = IsDlgButtonChecked(hwnd, 3);
                CheckDlgButton(hwnd, 3, checked ? BST_UNCHECKED : BST_CHECKED);
                break;
            }
        }
        break;
    case WM_DESTROY:
        if (!isAuthorized) {
            PostQuitMessage(0);
        }
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        createTrayIcon(hwnd);
        break;
    case WM_USER + 1:
        if (lParam == WM_RBUTTONUP) {
            createPopupMenu(hwnd);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case WM_USER + 2:
            DestroyWindow(hwnd);
            break;  
        case WM_USER + 3:
            rebuildDatabaseAsync();
            break;  
        case WM_USER + 4:
            integrationIsActive = !integrationIsActive;
            if (integrationIsActive) {
                startIntegrationThread();
            }
            updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive,oneDriveIsActive, dbIsFull);
            createPopupMenu(hwnd);
            break;  
        case WM_USER + 5:
            ftpIsActive = !ftpIsActive;
            if (ftpIsActive) {
                startFTPThread();
            }
            updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);
            createPopupMenu(hwnd);
            break;  
        case WM_USER + 6:
            mailIsActive = !mailIsActive;
            updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);
            createPopupMenu(hwnd);
            break;  
        case WM_USER + 7:
            oneDriveIsActive = !oneDriveIsActive;
            if (oneDriveIsActive) {
                startOneDriveThread();
            }
            updateModuleActivity(integrationIsActive, ftpIsActive, mailIsActive, oneDriveIsActive, dbIsFull);
            createPopupMenu(hwnd);
            break;
        case WM_USER + 100:
            showConfirmationDialog(L"Базу даних було успішно очищено, а інтеграцію файлів повторно запущено.",
                L"Повторна збірка завершена");
            break;
        case WM_USER + 101:
            showConfirmationDialog(L"Помилка при очищенні бази даних. Мабуть, не всі дані видалились.",
                L"Помилка повторної збірки");
        case WM_USER + 102:
            updateMenuDuringRebuild(hwnd, false);  
            break;
        default:
            break;
        }
        break;  
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_hInstance = hInstance;

    WNDCLASSW wcAuth = {};
    wcAuth.lpfnWndProc = AuthWindowProc;
    wcAuth.hInstance = hInstance;
    wcAuth.lpszClassName = L"AuthWindowClass";
    RegisterClassW(&wcAuth);

    hwndAuth = CreateWindowExW(
        0,
        L"AuthWindowClass",
        L"Авторизація",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 350, 250,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwndAuth, SW_SHOW);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (!IsWindow(hwndAuth) && isAuthorized && hwndMain == NULL) {

            WNDCLASSW wcMain = {};
            wcMain.lpfnWndProc = WindowProc;
            wcMain.hInstance = hInstance;
            wcMain.lpszClassName = L"MyWindowClass";
            RegisterClassW(&wcMain);

            hwndMain = CreateWindowExW(
                0,
                L"MyWindowClass",
                L"Hidden Window",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                NULL,
                NULL,
                hInstance,
                NULL
            );

            ShowWindow(hwndMain, SW_HIDE);

            // Запускаем задачи, трей и поток
            createTrayIcon(hwndMain);
            std::thread(backgroundTask).detach();
        }
    }
    return 0;
}