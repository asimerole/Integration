#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

#ifndef SQL_SS_LENGTH_UNLIMITED
#define SQL_SS_LENGTH_UNLIMITED 0xFFFFFFFF
#endif


#include "integration_handler.h"
#include <db_connection.h>
#include "utils.h"
#include "mail_handler.h"

namespace fs = boost::filesystem;

std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

const std::wstring pathToOMPExecutable = L"C:\\Recon\\WinRec-BS\\OMP_C"; // Path to OMP_C app

std::map<int, std::time_t> Integration::serverPings;

// Method of starting an external program with a flag and waiting for it to complete
bool Integration::runExternalProgramWithFlag(const std::wstring& programPath, const std::wstring& inputFilePath) {
    try {
        // Creating console commandd with flag -N
        std::wstring command = L"\"" + programPath + L"\" \"" + inputFilePath + L"\" -N";

        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION pi = {};

        // Create a process
        if (!CreateProcessW(NULL, &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            //logError("[Integration]: Failed to create process for command: " + wstringToUtf8(command));
            return false;
        }

        // Waiting for external program to complete
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 1500); // Wait 1.5 sec
        if (waitResult == WAIT_TIMEOUT) {
            //If the process is frozen, force it to close
            TerminateProcess(pi.hProcess, 1);
            //logError("[Integration]: The process has expired and was terminated with the file path specified.:" + wstringToString(inputFilePath));
            return false;
        }
        else {
            // Close process and thread descriptors
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            return true;
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in runExternalProgramWithFlag: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
        return false;
    }
}

bool Integration::isFileNameValid(const std::wstring& fileName) {
    static const std::vector<std::wstring> validPrefixes = {
        L"RNET", L"RPUSK", L"DAILY", L"DIAGN", L"RECON", L"REXPR"
    };

    return std::any_of(validPrefixes.begin(), validPrefixes.end(),
        [&](const std::wstring& prefix) {
            return boost::algorithm::istarts_with(fileName, prefix);
        });
}

//Method to check if a file is a data file
bool Integration::checkIsDataFile(const std::wstring& fileName) {
    return fileName.size() >= 5 && (fileName.substr(0, 5) == L"RECON" || fileName.substr(0, 5) == L"recon");
}

// Method to check if a file is an express file
bool Integration::checkIsExpressFile(const std::wstring& fileName) {
    return fileName.size() >= 5 && (fileName.substr(0, 5) == L"REXPR" || fileName.substr(0, 5) == L"rexpr");
}

bool Integration::checkIsOtherFiles(const std::wstring& fileName)
{
    std::vector<std::wstring> validPrefixes = {
        L"RNET", L"RPUSK", L"DAILY", L"DIAGN"
    };

    return std::any_of(validPrefixes.begin(), validPrefixes.end(),
        [&](const std::wstring& prefix) {
            return fileName.rfind(prefix, 0) == 0;
        });
}

// Method to check if a folder is sorted
bool Integration::isSortedFolder(const std::wstring& folderName) {
    try {
        if (folderName.size() == 7 && folderName[4] == L'_') {
            std::wstring year = folderName.substr(0, 4);  // ex. '2024'
            std::wstring month = folderName.substr(5, 2); // ex. '02'

            // Checking if year and month contain only numbers
            if (std::all_of(year.begin(), year.end(), ::isdigit) &&
                std::all_of(month.begin(), month.end(), ::isdigit)) {
                return true;
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in isSortedFolder: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
    return false;
}

void Integration::insertServerPing(int reconId, std::time_t lastPing)
{
    serverPings[reconId] = lastPing;
}

// Function to extract value from parameter string
std::wstring Integration::extractParamValue(const std::wstring& content, const std::wstring& marker) {
    std::size_t pos = content.rfind(marker);

    if (pos != std::wstring::npos) {
        std::size_t startPos = pos + marker.length();
        std::size_t endPos = content.find(L'$', startPos);

        if (endPos != std::wstring::npos) {
            return content.substr(startPos, endPos - startPos);
        }
        else {
            return content.substr(startPos);
        }
    }
    return L"";
}

// Function to extract value using regular expression
std::wstring Integration::extractValueWithRegex(const std::wstring& content, const std::wregex& regex) {
    std::wsmatch match;
    if (std::regex_search(content, match, regex) && match.size() > 1) {
        return match.str(1);
    }
    return L"";
}

// Collecting file paths
void Integration::collectRootPaths(std::unordered_set<std::wstring>& parentFolders, const std::wstring rootFolder) {
    try {
        std::wstring fullPath;
        std::wstring currentDir;

        if (!fs::exists(rootFolder) || !fs::is_directory(rootFolder)) {
            logError(stringToWString("The folder does not exist or is inaccessible: ") + stringToWString(converter.to_bytes(rootFolder)), INTEGRATION_LOG_PATH);
            return;
        }

        for (auto it = fs::recursive_directory_iterator(rootFolder); it != fs::recursive_directory_iterator(); ++it) {
            if (fs::is_directory(*it)) {
                fullPath = it->path().wstring();
                if (fullPath != currentDir) {
                    currentDir = fullPath;
                }
            }
            else {
                std::wstring fileName = it->path().filename().wstring(); 
                std::size_t pos = it->path().wstring().find(fileName);      

                std::wstring path = it->path().parent_path().wstring();

                // Add a check to ignore the "Cache" folder
                if (path.find(L"\\Cache") != std::wstring::npos || path.find(L"/Cache") != std::wstring::npos) {
                    continue;
                }
                if (parentFolders.find(path) == parentFolders.end()) { 
                    parentFolders.insert(path);
                }
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in collectRootPaths: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

std::tuple<int, int, int, int> Integration::getRecordIDs(SQLHDBC dbc, std::shared_ptr<BaseFile> file, bool needDataProcess) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle", INTEGRATION_LOG_PATH);
        return { -1, -1, -1, -1 };
    }

    std::wstring sqlQuery = LR"(
        SELECT 
            u.id AS unit_id,
            s.id AS struct_id,
            d.id AS data_id
    )";

    if (needDataProcess) {
        sqlQuery += LR"(,
            dp.id AS dataProcess_id
        )";
    }
    else {
        sqlQuery += LR"(,
            NULL AS dataProcess_id
        )";
    }

    sqlQuery += LR"(
        FROM [ReconDB].[dbo].[units] u
        LEFT JOIN [ReconDB].[dbo].[struct] s 
            ON s.recon_id = ? AND s.object = ?
        LEFT JOIN [ReconDB].[dbo].[data] d 
            ON d.struct_id = s.id AND d.file_num = ? AND d.time = CAST(? AS TIME(3))
    )";

    if (needDataProcess) {
        sqlQuery += LR"(
            LEFT JOIN [ReconDB].[dbo].[data_process] dp ON dp.id = d.id
        )";
    }

    sqlQuery += LR"(
        WHERE u.unit = ? AND u.substation = ?
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return { -1, -1, -1, -1 };
    }

    // Bind parameters
    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,
        const_cast<int*>(&file->reconNumber), 0, nullptr);   

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)file->object.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_VARCHAR, 3, 0,
        (SQLWCHAR*)file->fileNum.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 4, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 30, 0,
        (SQLWCHAR*)file->time.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 5, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)file->unit.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 6, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)file->substation.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return { -1, -1, -1, -1 };
    }

    int unit_id = -1, struct_id = -1, data_id = -1, dataProcess_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &unit_id, 0, nullptr);
        SQLGetData(hstmt, 2, SQL_C_SLONG, &struct_id, 0, nullptr);
        SQLGetData(hstmt, 3, SQL_C_SLONG, &data_id, 0, nullptr);
        SQLGetData(hstmt, 4, SQL_C_SLONG, &dataProcess_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return std::make_tuple(unit_id, struct_id, data_id, dataProcess_id);
}

static void logSQLError(const std::string& message, SQLHANDLE handle, SQLSMALLINT type) {
    SQLWCHAR sqlState[6], messageText[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT textLength;

    SQLRETURN ret = SQLGetDiagRec(type, handle, 1, sqlState, &nativeError, messageText, SQL_MAX_MESSAGE_LENGTH, &textLength);
    if (SQL_SUCCEEDED(ret)) {
        std::wstring errorMessage = L"[ERROR] " + std::wstring(message.begin(), message.end()) +
            L" SQLState: " + sqlState +
            L", Error Message: " + messageText +
            L", NativeError: " + std::to_wstring(nativeError);
        //logError(std::string(errorMessage.begin(), errorMessage.end())); 
        logError(stringToWString(converter.to_bytes(errorMessage)), INTEGRATION_LOG_PATH);
    }
    else {
        logError(stringToWString(message) + L" - No additional diagnostic information available.", INTEGRATION_LOG_PATH);
    }
}

int Integration::insertIntoUnitTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(stringToWString("Failed to allocate SQL statement handle: insertInUnitTable"), INTEGRATION_LOG_PATH);
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        INSERT INTO [ReconDB].[dbo].[units] ([unit], [substation]) 
        OUTPUT INSERTED.id
        VALUES(?,?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1 ;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)file->unit.c_str(), 0, nullptr);
    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)file->substation.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int unit_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &unit_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return unit_id;
}

int Integration::insertIntoStructTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoStructTable", INTEGRATION_LOG_PATH);
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        INSERT INTO [ReconDB].[dbo].[struct] ([recon_id], [object], [files_path]) 
        OUTPUT INSERTED.id
        VALUES(?,?,?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,      // [recon_id]
        const_cast<int*>(&file->reconNumber), 0, nullptr);

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,    // [object]
        (SQLWCHAR*)file->object.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,    // [files_path]
        (SQLWCHAR*)file->parentFolderPath.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int struct_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &struct_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return struct_id;
}

int Integration::insertIntoDataTable(SQLHDBC dbc, const FileInfo& fileInfo, int struct_id)
{
    std::shared_ptr<ExpressFile> expressFile = nullptr;
    std::shared_ptr<DataFile> dataFile = nullptr;
    std::shared_ptr<BaseFile> baseFile = nullptr;

    switch (fileInfo.files.size()) {
    case 1:
        if (typeid(*fileInfo.files[0]) == typeid(BaseFile)) {
            baseFile = std::dynamic_pointer_cast<BaseFile>(fileInfo.files[0]);
		}
		else if (typeid(*fileInfo.files[0]) == typeid(ExpressFile)) {
			expressFile = std::dynamic_pointer_cast<ExpressFile>(fileInfo.files[0]);
		}
        else {
            logError(L"[Integration] Expected only BaseFile, got derived type.", INTEGRATION_LOG_PATH);
        }
        break;

    case 2:
        for (const auto& file : fileInfo.files) {
            if (auto ef = std::dynamic_pointer_cast<ExpressFile>(file)) {
                expressFile = ef;
            }
            else if (auto df = std::dynamic_pointer_cast<DataFile>(file)) {
                dataFile = df;
            }
            else {
                logError(L"[Integration] Unknown file type in fileInfo.files.", INTEGRATION_LOG_PATH);
            }
        }
        break;

    default:
        logError(L"[Integration] Unexpected number of files. Expected 1 or 2.", INTEGRATION_LOG_PATH);
        break;
    }


    // Determine which file to use for general parameters
    std::shared_ptr<BaseFile> file = expressFile ? expressFile : baseFile;
    if (!file) {
        //logError(L"[Integration] No valid file found for binding common parameters.", INTEGRATION_LOG_PATH);
        return -1;
    }

    // Date and Time Conversion
    std::wstring formattedDate = file->date.substr(6, 4) + L"-" + file->date.substr(3, 2) + L"-" + file->date.substr(0, 2);
    std::wstring formattedTime = file->time;

    if (file->binaryData.empty()) {
		return -1; // No binary data to insert
    }

    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoDataTable", INTEGRATION_LOG_PATH);
        return -1;
    }

    std::wstring sqlQuery = LR"(
        INSERT INTO [ReconDB].[dbo].[data] 
        ([struct_id], [date], [time], [file_num]
    )";

    std::wstring values = LR"(
        OUTPUT INSERTED.id
        VALUES(?, ?, ?, ?)";

    int paramIndex = 1;
    std::vector<SQLLEN> fileSizes;
    std::vector<void*> filePointers;

    // собираем данные
    if (dataFile && dataFile->hasDataFile) {
        if (!dataFile->binaryData.empty()) {
            sqlQuery += L", [data_file]";
            values += L", ?";
            filePointers.push_back((void*)dataFile->binaryData.data());
            fileSizes.push_back(static_cast<SQLLEN>(dataFile->binaryData.size()));
        }
    }
    if (expressFile && expressFile->hasExpressFile) {
        if (!expressFile->binaryData.empty()) {        
            sqlQuery += L", [express_file]";
            values += L", ?";
            filePointers.push_back((void*)expressFile->binaryData.data());
            fileSizes.push_back(static_cast<SQLLEN>(expressFile->binaryData.size()));
        }
    }
    if (baseFile && !expressFile && !dataFile) {
        if (!baseFile->binaryData.empty()) {
            sqlQuery += L", [other_type_file], [file_type]";
            values += L", ?, ?";
            filePointers.push_back((void*)baseFile->binaryData.data());
            fileSizes.push_back(static_cast<SQLLEN>(baseFile->binaryData.size()));
        }
    }

    // Completing the SQL query
    sqlQuery += L") " + values + L");";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        logSQLError("Failed to prepare SQL query", hstmt, SQL_HANDLE_STMT);
        return -1;
    }

    // Bind the required parameters
    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0,
        const_cast<int*>(&struct_id), 0, nullptr);

    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind struct_id", hstmt, SQL_HANDLE_STMT);
     
    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, formattedDate.size(), 0,
        (SQLWCHAR*)formattedDate.c_str(), (formattedDate.size() + 1) * sizeof(wchar_t), nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind date", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, formattedTime.size(), 0,
        (SQLWCHAR*)formattedTime.c_str(), (formattedTime.size() + 1) * sizeof(wchar_t), nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind time", hstmt, SQL_HANDLE_STMT);

    std::wstring fileNum = file->fileNum;
    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)fileNum.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind file_num", hstmt, SQL_HANDLE_STMT);

    // Bind binary files
    for (size_t i = 0; i < filePointers.size(); ++i) {
        void* binData = filePointers[i];
        SQLLEN* binSize = &fileSizes[i];  

		// Bind the binary data
        ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, 
            SQL_C_BINARY, SQL_LONGVARBINARY,
            *binSize, 0, binData, *binSize, binSize);
        
        if (!SQL_SUCCEEDED(ret)) {
            logSQLError("Failed to bind binary file", hstmt, SQL_HANDLE_STMT);
        }
    }

    if (baseFile) {
        ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
            (SQLWCHAR*)file->fileName.substr(0, 5).c_str(), 0, nullptr);
        if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind file_num", hstmt, SQL_HANDLE_STMT);
    }

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute SQL query in insertIntoDataTable", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int data_id = -1;
    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &data_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return data_id;
}

int Integration::insertIntoProcessTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file, int data_id)
{
    auto expressFile = std::dynamic_pointer_cast<ExpressFile>(file);
    if (!expressFile) {
        logError(L"[Integration::insertIntoProcessTable] Expected ExpressFile but got something else", INTEGRATION_LOG_PATH);
        return -1;
    }

    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoProcessTable", INTEGRATION_LOG_PATH);
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        SET TRANSACTION ISOLATION LEVEL READ COMMITTED;
        INSERT INTO [ReconDB].[dbo].[data_process] ([id], [damaged_line], [trigger], [event_type]) 
        OUTPUT INSERTED.id
        VALUES(?, ?, ?, ?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,                          
        const_cast<int*>(&data_id), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 1", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,      
        (SQLWCHAR*)expressFile->damagedLine.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 2", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)expressFile->factor.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 3", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 4, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)expressFile->typeKz.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 4", hstmt, SQL_HANDLE_STMT);

    const int maxRetries = 3;
    int attempt = 0;
    while (attempt < maxRetries) {
        ret = SQLExecute(hstmt);
        if (SQL_SUCCEEDED(ret)) break; 
        if (ret == SQL_ERROR) {
            SQLINTEGER nativeError;
            SQLCHAR sqlState[6], errorMsg[256];
            SQLSMALLINT textLength;
            SQLGetDiagRecA(SQL_HANDLE_STMT, hstmt, 1, sqlState, &nativeError, errorMsg, sizeof(errorMsg), &textLength);

            if (nativeError == 1205) { // Deadlock
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Пауза перед повтором
                attempt++;
                continue;
            }
        }
        logSQLError("Failed to execute SQL query in function 'insertIntoProcessTable", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int dataProcess_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &dataProcess_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return dataProcess_id;
}

std::wstring formatDateTime(std::time_t time)
{
    std::tm* tm_ptr = std::localtime(&time);
    std::wostringstream woss;
    woss << std::put_time(tm_ptr, L"%Y-%m-%d %H:%M:%S");
    return woss.str(); 
}

void Integration::insertIntoLogsTable(SQLHDBC dbc, const FileInfo& fileInfo, int struct_id)
{
    std::shared_ptr<ExpressFile> expressFile = nullptr;
    std::shared_ptr<DataFile> dataFile = nullptr;
    std::shared_ptr<BaseFile> baseFile = nullptr;

    int filesCount = fileInfo.files.size();

    switch (filesCount) {
    case 1:
        if (typeid(*fileInfo.files[0]) == typeid(BaseFile)) {
            baseFile = std::dynamic_pointer_cast<BaseFile>(fileInfo.files[0]);
        }
        else {
            logError(L"[Integration] Expected only BaseFile, got derived type.", INTEGRATION_LOG_PATH);
            return;
        }
        break;

    case 2:
        for (const auto& file : fileInfo.files) {
            if (auto ef = std::dynamic_pointer_cast<ExpressFile>(file)) {
                expressFile = ef;
            }
            else if (auto df = std::dynamic_pointer_cast<DataFile>(file)) {
                dataFile = df;
            }
            else {
                logError(L"[Integration] Unknown file type in fileInfo.files.", INTEGRATION_LOG_PATH);
                return;
            }
        }
        break;

    default:
        logError(L"[Integration] Unexpected number of files. Expected 1 or 2.", INTEGRATION_LOG_PATH);
        return;
    }

    // Get reconId
    int reconId = (baseFile) ? baseFile->reconNumber : (dataFile) ? dataFile->reconNumber : 0;

    // last_ping
    std::wstring lastPingDateTimeStr;
    auto it = serverPings.find(reconId);
    if (it != serverPings.end()) {
        std::time_t pingTime = it->second;
        lastPingDateTimeStr = formatDateTime(pingTime);
    }

    // last_recon
    std::wstring lastReconDateTimeStr;
    if (expressFile && dataFile) {
        lastReconDateTimeStr = expressFile->date + L" " + expressFile->time;
    }

    // last_daily
    std::wstring lastDailyDateTimeStr;
    if (baseFile && baseFile->fileName.substr(0, 5) == L"DAILY") {
        lastDailyDateTimeStr = baseFile->date + L" " + baseFile->time;
    }

    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Integration] Failed to allocate SQL handle", INTEGRATION_LOG_PATH);
        return;
    }

    std::wstring checkQuery = LR"(
    SELECT 
        s.id, 
        (SELECT COUNT(*) FROM [ReconDB].[dbo].[logs] l WHERE l.recon_id = s.id) AS log_count
    FROM 
        [ReconDB].[dbo].[struct] s
    WHERE 
        s.recon_id = ?
)";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)checkQuery.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to prepare combined checkQuery ", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return;
    }

    SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &reconId, 0, nullptr);

    int idFromStruct = 0;
    int count = 0;

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute combined checkQuery ", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return;
    }

    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &idFromStruct, 0, nullptr);
        SQLGetData(hstmt, 2, SQL_C_SLONG, &count, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);


    std::wstringstream debugLog;
    // Composing an INSERT or UPDATE
    std::wstring query = L"";

    if (count > 0) {
        query += LR"(UPDATE[ReconDB].[dbo].[logs] SET )";
        if (lastPingDateTimeStr.size() > 0) {
            query += LR"(last_ping = ?, )";
        }
        
        if (filesCount > 1) {
            query += LR"(last_recon = ? )";
        } 
        else {
            if (lastDailyDateTimeStr.size() > 0) {
                query += LR"(last_daily = ? )";
            }
        }

        query += LR"(WHERE recon_id = ? )";
    }
    else {
        query = LR"(
            INSERT INTO [ReconDB].[dbo].[logs]
            ( 
        )";
        std::wstring values = L"VALUES (";
        bool first = true;
        if (lastPingDateTimeStr.size() > 0) {
            query += L"last_ping";
            values += L"?";
            first = false;
        }

        if (filesCount > 1) {
            if (!first) {
                query += L", ";
                values += L", ";
            }
            query += L"last_recon";
            values += L"?";
        }
        else {
            if (!first) {
                query += L", ";
                values += L", ";
            }
            query += L"last_daily";
            values += L"?";
        }

        query += L", recon_id) ";
        values += L", ?)";

        query += values;
    }

    debugLog << L"[DEBUG] SQL query: " << query;
    debugLog << L"\n[DEBUG] filesCount = " << filesCount << L", count = " << count;
    ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate handle for insert/update", INTEGRATION_LOG_PATH);
        return;
    }

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to prepare insert/update", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return;
    }

    int paramIndex = 1;
    debugLog << L"[DEBUG] Binding SQL parameters:";


    // Bind last_ping
    if (lastPingDateTimeStr.size() != 0) {
        SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, lastPingDateTimeStr.size(), 0,
            (SQLWCHAR*)lastPingDateTimeStr.c_str(), 0, nullptr);
        debugLog << L"\n[" << paramIndex - 1 << L"] last_ping = " << lastPingDateTimeStr;
    }
    
    // If filesCount > 1 → insert last_recon
    if (filesCount > 1) {
        SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, lastReconDateTimeStr.size(), 0,
            (SQLWCHAR*)lastReconDateTimeStr.c_str(), 0, nullptr);
        debugLog << L"\n[" << paramIndex - 1 << L"] last_recon = " << lastReconDateTimeStr;
    }

    // If filesCount == 1 → insert last_daily
    if (filesCount == 1) {
        SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, lastDailyDateTimeStr.size(), 0,
            (SQLWCHAR*)lastDailyDateTimeStr.c_str(), 0, nullptr);
        debugLog << L"\n[" << paramIndex - 1 << L"] last_daily = " << lastDailyDateTimeStr;
    }
    
    // Bind recon_id
    SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0,
        &idFromStruct, 0, nullptr);
    debugLog << L"\n[" << paramIndex - 1 << L"] idFromStruct = " << idFromStruct;
    logError(debugLog.str(), INTEGRATION_LOG_PATH);
    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute insert/update logs", hstmt, SQL_HANDLE_STMT);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}


// Integration of information into the database
void Integration::fileIntegrationDB(SQLHDBC dbc, const FileInfo& fileInfo, std::atomic_bool& mailingIsActive, std::atomic_bool& dbIsFull) {
    try {
        //logIntegrationError(L"[Integration] file integration was started");
        // Control DB connection
        if (dbc == nullptr) {
            logError(L"[Integration]: No connection to the database", INTEGRATION_LOG_PATH);
            return;
        }
        std::shared_ptr<BaseFile> file = nullptr;
        bool needDataProcess = false;
        // Identifying file types
        for (const auto& f : fileInfo.files) {
            if (auto ef = std::dynamic_pointer_cast<ExpressFile>(f)) {
                file = ef;
                needDataProcess = true;
            }
            else if (typeid(*f) == typeid(BaseFile)) {
                auto bf = std::dynamic_pointer_cast<BaseFile>(f);
                file = bf;
            }
        }

        if (!file) {
            //logIntegrationError(L"[Integration::fileIntegrationDB] No valid file found.");
            return;
        }

        if (file->date.size() < 10 )  // Date format should be like - 29/07/2024
            return;      

        // Insert into dbo.units (if it does not exist)    
        std::tuple<int, int, int, int> result = getRecordIDs(dbc, file, needDataProcess);
        int unit_id = std::get<0>(result);
        int struct_id = std::get<1>(result);
        int data_id = std::get<2>(result);
        int dataProcess_id = std::get<3>(result);

        if (unit_id == -1) {
            unit_id = insertIntoUnitTable(dbc, file);

            if (unit_id == -1)
                return;
        }

        // Insert into dbo.struct (if it does not exist)
        if (struct_id == -1) {
            struct_id = insertIntoStructTable(dbc, file);

            if (struct_id == -1)
                return;
        }

        // Insert into dbo.[struct_units] (if it does not exist)
        if (struct_id != -1) {
            std::wstringstream sqlCheckStructUnits;
            sqlCheckStructUnits << L"SELECT COUNT(*) FROM [ReconDB].[dbo].[struct_units] WHERE [unit_id] = " << unit_id
                << L" AND [struct_id] = " << struct_id << L";";
            int count = Database::executeSQLAndGetIntResult(dbc, sqlCheckStructUnits);
            if (count == 0) {
                std::wstringstream sqlStructUnits;
                sqlStructUnits << L"INSERT INTO [ReconDB].[dbo].[struct_units] ([unit_id], [struct_id]) VALUES ("
                    << unit_id << L", "     // [unit_id]
                    << struct_id << L");";  // [struct_id]

                if (!Database::executeSQL(dbc, sqlStructUnits)) {
                    return;
                }
            }
        }


        // Insert into dbo.data
        if (data_id == -1) {
            data_id = insertIntoDataTable(dbc, fileInfo, struct_id);

            if (data_id == -1) 
                return;

            // Loading users and sending emails
            auto users = loadUsersFromDatabase(dbc);

            auto it = users.find(wstringToString(file->substation));
            if (mailingIsActive && it != users.end()) {
                std::string configJson = wstringToUtf8(Database::getJsonConfigFromDatabase("mail", dbc));

                // Check for empty string
                if (!configJson.empty()) {
                    try {
                        // Parsing JSON
                        auto config = parseMailServerConfig(configJson);
                        logError(L"[Mail] Config file was received successfully.", EMAIL_LOG_PATH);
                        sendEmails(config, users, fileInfo);

                    }
                    catch (const std::exception& e) {
                        logError(L"[Mail] Failed to parse config JSON or send emails: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);

                    }
                }
                else {
                    logError(L"[Mail] Config JSON is empty. Skipping email sending.", EMAIL_LOG_PATH);
                }
            }
        }

        // Insert into dbo.logs
        if (struct_id != -1 && data_id != -1 && dbIsFull) {
            insertIntoLogsTable(dbc, fileInfo, struct_id);
        }

        // Insert Into dbo.data_process (connected with data)
        if (needDataProcess) {
            if (dataProcess_id == -1) {
                auto ef = std::dynamic_pointer_cast<ExpressFile>(file);
                if (!ef) {
                    logError(L"[insertIntoProcessTable] Invalid cast to ExpressFile", INTEGRATION_LOG_PATH);
                    return;
                }
                dataProcess_id = insertIntoProcessTable(dbc, ef, data_id);

                if (dataProcess_id == -1) 
                    return;
            }
        }
        //logIntegrationError(L"[Integration] file integration was finished");
    }
     
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in fileIntegrationDB: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

// Helper function for concatenating strings with a separator
std::wstring Integration::join(const std::vector<std::wstring>& parts, const std::wstring& delimiter) {
    std::wstring result;
    for (size_t i = 0; i < parts.size(); ++i) {
        result += parts[i];
        if (i < parts.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}

// Method for collecting information about files
void Integration::collectInfo(FileInfo &fileInfo, const fs::directory_entry& entry, std::wstring rootFolder, SQLHDBC dbc) {
    try {

        //logIntegrationError(L"[Integration] collect info was started");
        std::wstring fileName = entry.path().filename().wstring();              // Name of file ex. (RECON167.759)
        //if (fileName == L"RECON761.608" || fileName == L"REXPR761.608") {
        //    logError(L"", LOG_PATH);
        //}
        if (!isFileNameValid(fileName)) { return; }                             // Checking file name for validity
        std::wstring baseName = fileName.substr(5);                             // Recon number and file number ex. (167.759)     
        std::wstring pathToFile = entry.path().parent_path().wstring() + L"\\"; // Path to file
        std::wstring fullPath = entry.path().wstring();                         // Full path including file name 
        std::wstring reconNum = fileName.substr(5, 3);                          // Recon num
        std::wstring fileNum = fileName.substr(9, 3);                           // File num
    
        if (checkIsDataFile(fileName)) {
            auto dataFile = std::make_shared<DataFile>();
            dataFile->fileName = fileName;
            dataFile->parentFolderPath = pathToFile;
            dataFile->fullPath = fullPath;
            dataFile->fileNum = fileNum;
            dataFile->reconNumber = std::stoi(reconNum);
    
            dataFile->processFile();
            dataFile->processPath(rootFolder);
    
            std::wstring expressFileName = L"REXPR" + baseName;
            std::wstring expressFilePath = pathToFile + expressFileName;
    
            // If Express file is not exists, run ОМР-С programm 
            if (!fs::exists(expressFilePath)) {
                bool containsLetters = std::any_of(baseName.begin(), baseName.end(), [](wchar_t c) {
                    return std::iswalpha(c);
                    });

                std::wregex pattern(L"^recon\\d{3}\\.\\d{3}$", std::regex_constants::icase);

                if (!containsLetters) {
                    if (std::regex_match(fileName, pattern))    {
                        if (!runExternalProgramWithFlag(pathToOMPExecutable, fullPath)) {
                            logError(L"File: " + fileName + L" is broken.", LOG_PATH);
                            return;
                        }
                        return;
                    }
                }
                else { return; }
            }
    
            auto expressFile = std::make_shared<ExpressFile>();
            expressFile->fileName = expressFileName;
            expressFile->parentFolderPath = pathToFile;
            expressFile->fullPath = expressFilePath;
            expressFile->fileNum = fileNum;
            expressFile->reconNumber = std::stoi(reconNum);
    
            expressFile->processFile();
            expressFile->processPath(rootFolder);
    
            fileInfo.files.push_back(dataFile);
            fileInfo.files.push_back(expressFile);
    
        }
        else if (checkIsExpressFile(fileName)) {
            auto expressFile = std::make_shared<ExpressFile>();
            expressFile->fileName = fileName;
            expressFile->parentFolderPath = pathToFile;
            expressFile->fullPath = fullPath;
            expressFile->fileNum = fileNum;
            expressFile->reconNumber = std::stoi(reconNum);
    
            expressFile->processFile();
            expressFile->processPath(rootFolder);
            fileInfo.files.push_back(expressFile);
    
            auto dataFile = std::make_shared<DataFile>();
            std::wstring dataFileName = L"RECON" + baseName;
            std::wstring dataFilePath = pathToFile + dataFileName;
    
            if (fs::exists(dataFilePath)) {
                dataFile->fileName = dataFileName;
                dataFile->parentFolderPath = pathToFile;
                dataFile->fullPath = dataFilePath;
                dataFile->fileNum = fileNum;
                dataFile->reconNumber = std::stoi(reconNum);
    
                dataFile->processFile();
                dataFile->processPath(rootFolder);
    
                fileInfo.files.push_back(dataFile);
            }
        }
        else if (checkIsOtherFiles(fileName)) {
            auto baseFile = std::make_shared<BaseFile>();
            baseFile->fileName = fileName;
            baseFile->parentFolderPath = pathToFile;
            baseFile->fullPath = fullPath;
            baseFile->fileNum = fileNum;
            baseFile->reconNumber = std::stoi(reconNum);
    
            baseFile->processFile();
            baseFile->processPath(rootFolder);
    
            fileInfo.files.push_back(baseFile);
        }
        else { return; }
        //logIntegrationError(L"[Integration] collect info was finished");
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in collectFiles: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
    catch (...) {
        logError(L"[FTP] Unknown fatal error during collectFiles.", EXCEPTION_LOG_PATH);
    }
}

// Method of sorting files into folders by date
void Integration::sortFiles(const FileInfo& fileInfo) {
    //logIntegrationError(L"[Integration] Sort Files was started");
    try {
        std::shared_ptr<ExpressFile> expressFile = nullptr;
        std::shared_ptr<DataFile> dataFile = nullptr;
        std::shared_ptr<BaseFile> baseFile = nullptr;

        for (const auto& file : fileInfo.files) {
            if (auto ef = std::dynamic_pointer_cast<ExpressFile>(file)) {
                expressFile = ef;
            }
            else if (auto df = std::dynamic_pointer_cast<DataFile>(file)) {
                dataFile = df;
            }
            else {
                baseFile = file;
            }
        }

        if (!expressFile && dataFile) {
            logError(L"[Integration] ExpressFile for " + dataFile->fileName + L" is not found, sorting is not possible.", INTEGRATION_LOG_PATH);
            return;
        }

        if (expressFile) {
            //logIntegrationError(L"[Integration] Processing ExpressFile: " + expressFile->fileName);
            if (expressFile->date.size() < 6) {
                logError(L"[Integration] ExpressFile date too short: " + expressFile->date, INTEGRATION_LOG_PATH);
                return;
            }

            std::wstring year = expressFile->date.substr(6, 4);
            std::wstring month = expressFile->date.substr(3, 2);
            std::wstring newFolder = expressFile->parentFolderPath;

            if (!expressFile->inSortedFolder) {
                newFolder = expressFile->parentFolderPath + L"\\" + year + L"_" + month;
                try {
                    if (!fs::exists(newFolder)) {
                        logError(L"[Integration] Creating new folder: " + newFolder, INTEGRATION_LOG_PATH);
                        fs::create_directory(newFolder);
                    }
                }
                catch (const std::exception& e) {
                    logError(L"[Integration] Exception creating folder " + newFolder + L": " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                    return;
                }
            }
            else {
                //logIntegrationError(L"[Integration] ExpressFile already in sorted folder, skipping.");
                return;
            }

            // Перемещаем express файл
            if (expressFile->hasExpressFile) {
                std::wstring expressPath = expressFile->parentFolderPath + L"\\" + expressFile->fileName;
                std::wstring newExpressPath = newFolder + L"\\" + expressFile->fileName;

                try {
                    if (fs::exists(expressPath)) {
                        if (fs::exists(newExpressPath)) {
                            //logIntegrationError(L"[Integration] Removing duplicate express file: " + expressPath);
                            fs::remove(expressPath);
                        }
                        else {
                            //logIntegrationError(L"[Integration] Moving express file from " + expressPath + L" to " + newExpressPath);
                            fs::copy(expressPath, newExpressPath, fs::copy_options::overwrite_existing);
                            fs::remove(expressPath);
                        }
                    }
                    else {
                        //logIntegrationError(L"[Integration] Express file not found: " + expressPath);
                    }
                }
                catch (const std::exception& e) {
                    logError(L"[Integration] Exception moving express file: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                    return;
                }
            }

            // Перемещаем data файл
            if (dataFile && dataFile->hasDataFile) {
                std::wstring dataPath = dataFile->parentFolderPath + L"\\" + dataFile->fileName;
                std::wstring newDataPath = newFolder + L"\\" + dataFile->fileName;

                try {
                    if (fs::exists(dataPath)) {
                        if (fs::exists(newDataPath)) {
                            //logIntegrationError(L"[Integration] Removing duplicate data file: " + dataPath);
                            fs::remove(dataPath);
                        }
                        else {
                            //logIntegrationError(L"[Integration] Moving data file from " + dataPath + L" to " + newDataPath);
                            fs::copy(dataPath, newDataPath, fs::copy_options::overwrite_existing);
                            fs::remove(dataPath);
                        }
                    }
                    else {
                        //logIntegrationError(L"[Integration] Data file not found: " + dataPath);
                        return;
                    }
                }
                catch (const std::exception& e) {
                    logError(L"[Integration] Exception moving data file: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                    return;
                }
            }
        }
        else if (baseFile && baseFile->date.size() >= 6) {
            //logIntegrationError(L"[Integration] Processing BaseFile: " + baseFile->fileName);
            std::wstring year = baseFile->date.substr(6, 4);
            std::wstring month = baseFile->date.substr(3, 2);
            std::wstring newFolder = baseFile->parentFolderPath;

            if (!baseFile->inSortedFolder) {
                newFolder = baseFile->parentFolderPath + L"\\" + year + L"_" + month;
                try {
                    if (!fs::exists(newFolder)) {
                        logError(L"[Integration] Creating new folder: " + newFolder, INTEGRATION_LOG_PATH);
                        fs::create_directory(newFolder);
                    }
                }
                catch (const std::exception& e) {
                    logError(L"[Integration] Exception creating folder " + newFolder + L": " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                    return;
                }
            }
            else {
                //logIntegrationError(L"[Integration] BaseFile already in sorted folder, skipping.");
                return;
            }

            std::wstring basePath = baseFile->parentFolderPath + L"\\" + baseFile->fileName;
            std::wstring newBasePath = newFolder + L"\\" + baseFile->fileName;

            try {
                if (fs::exists(basePath)) {
                    if (fs::exists(newBasePath)) {
                        logError(L"[Integration] Removing duplicate base file: " + basePath, INTEGRATION_LOG_PATH);
                        fs::remove(basePath);
                    }
                    else {
                        logError(L"[Integration] Moving base file from " + basePath + L" to " + newBasePath, INTEGRATION_LOG_PATH);
                        fs::copy(basePath, newBasePath, fs::copy_options::overwrite_existing);
                        fs::remove(basePath);
                    }
                }
                else {
                    logError(L"[Integration] Base file not found: " + basePath, INTEGRATION_LOG_PATH);
                    return;
                }
            }
            catch (const std::exception& e) {
                logError(L"[Integration] Exception moving base file: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
                return;
            }
        }
        else {
            //logError(L"[Integration] No suitable file for sorting found.", INTEGRATION_LOG_PATH);
        }

        //logIntegrationError(L"[Integration] Sort Files was finished successfully");
    }
    catch (const std::exception& e) {
        logError(L"[Integration] Fatal exception in sortFiles: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}

std::wstring Integration::getPathByRNumber(int recon_id,SQLHDBC dbc)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const wchar_t* query = LR"(SELECT d.local_path
        FROM[ReconDB].[dbo].[struct] s 
        JOIN[ReconDB].[dbo].[FTP_Directories] d ON d.struct_id = s.id 
        WHERE s.recon_id = ?)";

    ret = SQLPrepareW(stmt, (SQLWCHAR*)query, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::wstring();
    }

    ret = SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,
        const_cast<int*>(&recon_id), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter recon_id", stmt, SQL_HANDLE_STMT);

    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute SQL query in function 'getPathByRNumber' ", stmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::wstring();
    }

    std::wstring localPath;
    wchar_t pathBuffer[512];
    SQLLEN pathLen = 0;

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLGetData(stmt, 1, SQL_C_WCHAR, pathBuffer, sizeof(pathBuffer), &pathLen);
        if (pathLen != SQL_NULL_DATA) {
            localPath = pathBuffer;  
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return localPath;
}




