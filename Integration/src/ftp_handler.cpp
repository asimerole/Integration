#include "ftp_handler.h"
#include "db_connection.h"
#include "integration_handler.h"
#include "utils.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>


#define _CRT_SECURE_NO_WARNINGS 
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef UNICODE
#define SQLTCHAR SQLWCHAR
#define SQLTSTR  SQLWCHAR
#define SQLTEXT(str)  L##str
#else
#define SQLTCHAR SQLCHAR
#define SQLTSTR  SQLCHAR
#define SQLTEXT(str)  str
#endif

using json = nlohmann::json;

// function for encoding URL
std::string Ftp::encodeURL(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (curl) {
        char* encodedURL = curl_easy_escape(curl, url.c_str(), static_cast<int>(url.length()));
        std::string result;

        if (encodedURL) {
            result = encodedURL;
            curl_free(encodedURL);
        } 

        curl_easy_cleanup(curl);
        return result;
    }
    return std::string();
}

// Callback function for writing data to a file
size_t Ftp::write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    if (written != size * nmemb) {
        logError(L"[FTP1]: Error writing to file. Written: " + stringToWString(std::to_string(written)), FTP_LOG_PATH);
    }
    return written;
}

// Callback function to display the file table
size_t Ftp::write_list_stream(void* buffer, size_t size, size_t nmemb, void* userp) {
    if (!buffer || !userp) {
        logError(L"write_list_stream: NULL pointer detected!", FTP_LOG_PATH);
        return 0;
    }

    FtpTransferContext* context = static_cast<FtpTransferContext*>(userp);
    const size_t totalSize = size * nmemb;
    const char* data = static_cast<char*>(buffer);

    size_t processed = 0;
    while (processed < totalSize && context->processedFiles < context->maxFilesPerSession) {
        const char* end = static_cast<const char*>(memchr(data + processed, '\n', totalSize - processed));
        if (!end) break;

        size_t lineLength = end - (data + processed);
        std::string filename(data + processed, lineLength);
        processed += lineLength + 1;

        filename.erase(filename.find_last_not_of("\r\n") + 1);
        if (filename.empty()) continue;

        // We pass the entire context to the handler
        Ftp::getInstance().processSingleFile(filename, *context);
    }

    return totalSize;
}

bool startsWithValidPrefix(const std::string& fileName) {
    std::vector<std::string> validPrefixes = {
    "REXPR", "RECON", "RNET", "RPUSK", "DAILY", "DIAGN"
    };

    return std::any_of(validPrefixes.begin(), validPrefixes.end(),
        [&](const std::string& prefix) {
            return fileName.rfind(prefix, 0) == 0;
        });
}

void Ftp::processSingleFile(const std::string& fileName, FtpTransferContext& context) {
    if (!startsWithValidPrefix(fileName) ||
        !context.ftpIsActive->load(std::memory_order_acquire)) {
        return;
    }

    try {
        if (downloadFile(fileName, *context.server, context.url + fileName, context.ftpCacheDirPath)) {
            deleteFile(fileName, *context.server, context.url);

            // Forming a path for OneDrive
            std::wstring unitW = context.server->unit;
            std::wstring firstPart, secondPart;

            size_t separatorPos = unitW.find(L" - ");
            if (separatorPos != std::wstring::npos) {
                firstPart = unitW.substr(0, separatorPos);
                secondPart = unitW.substr(separatorPos + 3);
            }
            else {
                firstPart = unitW;
            }

            std::wstring oneDriveFullPathToFile = context.oneDrivePath + L"/" + firstPart + L"/" +
                (secondPart.empty() ? L"" : secondPart + L"/") +
                context.server->substation + L"/" + context.server->object + L"/" +
                stringToWString(fileName);

            if (context.oneDriveIsActive->load(std::memory_order_acquire)) {
                if (oneDriveFullPathToFile.length() >= 260) {
                    logError(L"[OneDrive] Path too long, file skipped: " + oneDriveFullPathToFile, ONEDRIVE_LOG_PATH);
                    return;
                }

                if (!fs::exists(oneDriveFullPathToFile)) {
                    fs::copy_file(fs::path(context.ftpCacheDirPath) / fileName,
                        oneDriveFullPathToFile,
                        fs::copy_options::overwrite_existing);
                    logError(L"[OneDrive] File copied successfully: " + oneDriveFullPathToFile, ONEDRIVE_LOG_PATH);
                }
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::wstring errorMessage = L"[FTP] Filesystem error processing " +
            stringToWString(fileName) + L": " +
            utf8_to_wstring(e.what());
        logError(errorMessage, EXCEPTION_LOG_PATH);
    }
    catch (const std::exception& e) {
        logError(stringToWString("[FTP] Error processing ") +
            stringToWString(fileName) + L": " +
            stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
}


// Method of collecting servers from a database
void Ftp::collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc) {
    //logFtpError(L"[FTP] Starting collectServers...");
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    try {
        if (stmt == SQL_NULL_HSTMT) {
            SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        }
        else {
            SQLFreeStmt(stmt, SQL_CLOSE);
        }

        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"[FTP2]: Failed to allocate SQL statement handle in collectServers", FTP_LOG_PATH);
            return;
        }

        std::wstring query = L"SELECT u.unit, u.substation, s.object, fs.IP_addr, fs.login, fs.password, "
            L"fs.status, d.remote_path, d.local_path, d.isFourDigits, s.recon_id "
            L"FROM [ReconDB].[dbo].[units] u "
            L"JOIN [ReconDB].[dbo].[struct_units] su ON u.id = su.unit_id "
            L"JOIN [ReconDB].[dbo].[struct] s ON su.struct_id = s.id "
            L"JOIN [ReconDB].[dbo].[FTP_servers] fs ON fs.unit_id = u.id "
            L"JOIN [ReconDB].[dbo].[FTP_Directories] d ON d.struct_id = s.id "
            L"WHERE fs.status = 1 AND d.isActiveDir = 1";

        //logFtpError(L"[FTP] Executing SQL query...");
        ret = SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"[FTP]: Не удалось выполнить SQL запрос сбора серверов!", FTP_LOG_PATH);
            goto cleanup;
        }

        while (SQLFetch(stmt) == SQL_SUCCESS) {
            ServerInfo server;
            wchar_t unit[256]{}, substation[256]{}, object[256]{}, ip[256]{}, login[256]{}, pass[256]{}, remoteFolderPath[256]{}, localFolderPath[256]{};
            int status = 0, isFourDigits = 0, reconId = 0;
            SQLLEN unitLen, substationLen, objectLen, ipLen, loginLen, passLen, statusLen, isFourDigitsLen, remoteFolderPathLen, localFolderPathLen, reconIdLen;

            if (SQLGetData(stmt, 1, SQL_C_WCHAR, unit, sizeof(unit), &unitLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 2, SQL_C_WCHAR, substation, sizeof(substation), &substationLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 3, SQL_C_WCHAR, object, sizeof(object), &objectLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 4, SQL_C_WCHAR, ip, sizeof(ip), &ipLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 5, SQL_C_WCHAR, login, sizeof(login), &loginLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 6, SQL_C_WCHAR, pass, sizeof(pass), &passLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 7, SQL_C_SLONG, &status, sizeof(status), &statusLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 8, SQL_C_WCHAR, remoteFolderPath, sizeof(remoteFolderPath), &remoteFolderPathLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 9, SQL_C_WCHAR, localFolderPath, sizeof(localFolderPath), &localFolderPathLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 10, SQL_C_SLONG, &isFourDigits, sizeof(isFourDigits), &isFourDigitsLen) != SQL_SUCCESS) continue;
            if (SQLGetData(stmt, 11, SQL_C_SLONG, &reconId, sizeof(reconId), &reconIdLen) != SQL_SUCCESS) continue;

            server.ip = (ipLen != SQL_NULL_DATA) ? ip : L"";
            server.login = (loginLen != SQL_NULL_DATA) ? login : L"";
            server.pass = (passLen != SQL_NULL_DATA) ? pass : L"";
            server.reconId = (reconIdLen != SQL_NULL_DATA) ? reconId : 0;

            if (isFourDigitsLen != SQL_NULL_DATA && isFourDigits == 1) {
                size_t len = wcslen(remoteFolderPath);

                if (len + 1 < 256) {
                    for (size_t i = len + 1; i > 1; --i) {
                        remoteFolderPath[i] = remoteFolderPath[i - 1];
                    }

                    remoteFolderPath[1] = L'1';
                }
            }

            server.remoteFolderPath = (remoteFolderPathLen != SQL_NULL_DATA) ? remoteFolderPath : L"";

            //logFtpError(L"[FTP] Checking connection for IP: " + server.ip + L"/" + server.remoteFolderPath + L"/");
            if (status == 1) {
                std::wstring url = Ftp::protocol() + server.ip + L"/" + server.remoteFolderPath + L"/";
                if (!checkConnection(wstringToString(url), wstringToString(server.login), wstringToString(server.pass))) {
                    //logFtpError(L"[FTP] Connection failed for " + server.ip + L"/" + server.remoteFolderPath + L"/");
                    continue;
                }
                auto now = std::chrono::system_clock::now();
                server.lastPingTime = std::chrono::system_clock::to_time_t(now); 
                Ftp::getInstance().pingServer(server.reconId, server.lastPingTime);
				logError(L"[FTP] Connection successful for " + server.ip + L"/" + server.remoteFolderPath + L"/", FTP_LOG_PATH);
            }

            server.unit = (unitLen != SQL_NULL_DATA) ? unit : L"";
            server.substation = (substationLen != SQL_NULL_DATA) ? substation : L"";
            server.object = (objectLen != SQL_NULL_DATA) ? object : L"";
            server.status = (statusLen != SQL_NULL_DATA) ? status : -1;
            server.localFolderPath = (localFolderPathLen != SQL_NULL_DATA) ? localFolderPath : L"";

            //logFtpError(L"[FTP] Adding server with IP: " + server.ip);
            servers.push_back(server);
        }
    }
    catch (const std::exception& e) {
        logError(L"[FTP] Exception in collectServers: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
    catch (...) {
        logError(L"[FTP] Unknown exception in collectServers!", EXCEPTION_LOG_PATH);
    }

cleanup:
    //logFtpError(L"[FTP] Cleaning up SQL statement...");
    SQLFreeStmt(stmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    //logFtpError(L"[FTP] collectServers completed.");
}

void Ftp::setFileTime(const std::string& filePath, const std::string& timestamp)
{
    SYSTEMTIME sysTime = {};
    FILETIME fileTime = {};

    // Parse the timestamp string (format: "YYYY-MM-DD HH:MM:SS")
    if (sscanf(timestamp.c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
        &sysTime.wYear, &sysTime.wMonth, &sysTime.wDay,
        &sysTime.wHour, &sysTime.wMinute, &sysTime.wSecond) != 6)
    {
        logError(L"Invalid timestamp format: " + stringToWString(timestamp), FTP_LOG_PATH);
        return;
    }


    // Convert SYSTEMTIME to FILETIME
    if (!SystemTimeToFileTime(&sysTime, &fileTime))
    {
        logError(L"Failed to convert system time to file time", FTP_LOG_PATH);
        return;
    }

    // Open the file
    HANDLE hFile = CreateFileA(filePath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        logError(L"Failed to open file for setting time: " + stringToWString(filePath), FTP_LOG_PATH);
        return;
    }

    // We set only the file modification time
    if (!SetFileTime(hFile, nullptr, nullptr, &fileTime))
    {
        logError(L"Failed to set file modification time: " + stringToWString(filePath), FTP_LOG_PATH);
    }

    CloseHandle(hFile);
}

static size_t throw_away(void* ptr, size_t size, size_t nmemb, void* data)
{
    (void)ptr;
    (void)data;
    return (size_t)(size * nmemb);
}

// Downloading file from server
bool Ftp::downloadFile(const std::string& fileName, const ServerInfo& server, const std::string url, const std::wstring& ftpCacheDirPath)
{
    // logFtpError("[FTP]: Starting file download for " + fileName + " from URL: " + url);

    CURL* curl;
    FILE* file = nullptr;
    CURLcode res;
    long filetime = -1;
    std::wstring dataFile;

    curl = curl_easy_init();
    if (curl) {
        // Construct the file path and convert it to a wide string for _wfopen
        dataFile = ftpCacheDirPath + L"/" + stringToWString(fileName);
        // logFtpError("[FTP]: Constructed file path for download: " + dataFile);

        // Set URL and credentials for the download
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());

        // Try to open the file for writing with _wfopen
        file = _wfopen(dataFile.c_str(), L"wb");
        if (file == nullptr) {
            logError(L"[FTP3]: Error opening file for writing: " + stringToWString(fileName), FTP_LOG_PATH);
            logError(L"[FTP4]: Full file path: " + dataFile, FTP_LOG_PATH);
            perror("fopen");  // Outputs error to stderr for debugging
            curl_easy_cleanup(curl);
            return false;
        }
        // logFtpError("[FTP]: File opened successfully for writing: " + dataFile);

        // Set the write function and data handler for the download
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        // Perform the file download
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            logError(L"[FTP5]: Error during file download for " + stringToWString(fileName) + L": " + stringToWString(curl_easy_strerror(res)), FTP_LOG_PATH);
            fclose(file);
            curl_easy_cleanup(curl);
            return false;
        }
        // Get file time 
        curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, throw_away);
        curl_easy_setopt(curl, CURLOPT_HEADER, 0L);

        res = curl_easy_perform(curl);
        fclose(file);

        if (CURLE_OK == res) {
            res = curl_easy_getinfo(curl, CURLINFO_FILETIME, &filetime);
            if ((CURLE_OK == res) && (filetime >= 0)) {

                std::time_t rawTime = static_cast<std::time_t>(filetime);
                std::tm* timeinfo = std::gmtime(&rawTime);

                char buffer[40];    // Here contains time
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

                std::string timestamp = std::string(buffer); 
                setFileTime(wstringToString(dataFile), timestamp);

                //logFtpError(L"[FTP]: File timestamp (UTC): " + stringToWString(timestamp));
            }
        }
        else {
            logError(L"Failed to get file time!", FTP_LOG_PATH);
            curl_easy_cleanup(curl);
        }

        // logFtpError("[FTP]: File downloaded successfully: " + fileName);

        // Cleanup after download
        curl_easy_cleanup(curl);
    }
    else {
        logError(L"[FTP6]: Failed to initialize CURL for downloading: " + stringToWString(fileName), FTP_LOG_PATH);
    }

    // The next rows might be for deleting 
    json j = {
        {"targetPath", wstringToUtf8(server.localFolderPath)}
    };

    std::string jsonInfo = j.dump(4);

    std::ofstream metaFile(wstringToString(ftpCacheDirPath) + "/" + fileName + ".meta");

    if (metaFile.is_open()) {
        metaFile << jsonInfo;
        metaFile.close();
    }
    // If the file was open, close it before returning
    if (file) {
        fclose(file);
    }

    //logFtpError(L"[FTP]: Finished attempting to download file: " + stringToWString(fileName));
    return true;
}

// Deleting a file from the server
int Ftp::deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url)
{
    CURL* curl;
    CURLcode res;
    std::string fullRemotePath = url;

    curl = curl_easy_init();
    if (!curl) {
        logError(L"[FTP9]: Error generating CURL.", FTP_LOG_PATH);
        return -1;
    }

    std::string deleCommand = "DELE " + filename;

    curl_easy_setopt(curl, CURLOPT_URL, fullRemotePath.c_str());  
    curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, deleCommand.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code == 250) {
        //logFtpError(stringToWString("[FTP]: File successfully deleted: ") + stringToWString(fullRemotePath + "/" + filename));
        curl_easy_cleanup(curl);
        return 1;    
    }
    else {
        if (res != CURLE_FTP_COULDNT_RETR_FILE) {
            logError(stringToWString("[FTP]: Error deleting file: ") + stringToWString(curl_easy_strerror(res)), FTP_LOG_PATH);
            curl_easy_cleanup(curl);
            return 1;
        }
        curl_easy_cleanup(curl);
        return 1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

// Checking for a successful connection to the FTP server
bool Ftp::checkConnection(const std::string& url, const std::string login, const std::string pass)
{
    CURL* curl;
    CURLcode res;
    bool connectionSuccessful = false;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (login + ":" + pass).c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CWD");
        curl_easy_setopt(curl, CURLOPT_PORT, 21);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            connectionSuccessful = true;
        }
        else {
            //logFtpError(L"[FTP10]: Не удалось установить соединение " + stringToWString(url) + L": " + stringToWString(curl_easy_strerror(res)));
            connectionSuccessful = false;
        }

        curl_easy_cleanup(curl);
        return connectionSuccessful;
    }
    else {
        logError(stringToWString("[FTP11]: Ошибка инициализации libcurl"), FTP_LOG_PATH);
        return false;
    }
}

bool Ftp::isServerActive(const ServerInfo& server, SQLHDBC dbc)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    // We construct a query assuming that the IP address is stored in server.ip
    std::wstring query = L"SELECT [status] FROM [ReconDB].[dbo].[FTP_servers] WHERE [IP_addr] = '" + std::wstring(server.ip.begin(), server.ip.end()) + L"'";

    ret = SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (SQL_SUCCEEDED(ret)) {
        int status = 0;
        SQLLEN statusLen = 0;

        // Getting status data
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            SQLGetData(stmt, 1, SQL_C_SLONG, &status, sizeof(status), &statusLen);

            // Checking if we received the data
            if (statusLen != SQL_NULL_DATA) {
                SQLFreeHandle(SQL_HANDLE_STMT, stmt);
                return (status == 1);  // If the status is 1, the server is active.
            }
        }
    }
    else {
        std::wstring errorMsg = stringToWString("[FTP]: Не удалось выполнить SQL запрос для проверки состояния сервера!");
        logError(errorMsg, FTP_LOG_PATH);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return false;  // If unable to get status or server not found
}

std::wstring sanitizeFileName(std::wstring name) {
    const std::wstring invalidChars = L"<>:\"/\\|?*";
    for (wchar_t& ch : name) {
        if (invalidChars.find(ch) != std::wstring::npos) {
            ch = L'_';  // Replace prohibited characters with '_'
        }
    }
    return name;
}

// Function to remove spaces from the beginning and end of a string
std::wstring trim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) ++start;
    size_t end = s.size();
    while (end > start && iswspace(s[end - 1])) --end;
    return s.substr(start, end - start);
}

// Split the string by hyphens (with or without spaces)
std::vector<std::wstring> splitByDash(const std::wstring& str) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    size_t pos = 0;

    while (pos < str.size()) {
        if (str[pos] == L'-') {
            std::wstring part = str.substr(start, pos - start);
            parts.push_back(trim(part));

            pos++;
            while (pos < str.size() && iswspace(str[pos])) pos++;
            start = pos;
        }
        else {
            pos++;
        }
    }

    if (start < str.size()) {
        parts.push_back(trim(str.substr(start)));
    }

    return parts;
}

// Creating a folder tree
void Ftp::createLocalDirectoryTree(ServerInfo& server, std::string rootFolder) {
    std::wstring rootFolderW = utf8_to_wstring(rootFolder);
   
    // Processing `server.unit`
    std::wstring unitW = sanitizeFileName(server.unit);

	std::vector<std::wstring> unitParts = splitByDash(unitW);
    
    fs::path fullPath = rootFolderW;

	for (const auto& part : unitParts) {
		fullPath /= sanitizeFileName(part);
	}

	fullPath /= sanitizeFileName(server.substation);
	fullPath /= sanitizeFileName(server.object);

    // Безопасно создаём все директории
    if (!fs::exists(fullPath)) {
        fs::create_directories(fullPath);
    }
}

// Transferring files and deleting from the server
void Ftp::fileTransfer(const ServerInfo& server, const std::string& url,
    const std::wstring& oneDrivePath,
    std::atomic_bool& ftpIsActive,
    std::atomic_bool& oneDriveIsActive,
    SQLHDBC dbc,
    const std::wstring& ftpCacheDirPath)
{
    try {
        // Initialize the context
        FtpTransferContext context{
            &server,
            url,
            oneDrivePath,
            &ftpIsActive,
            &oneDriveIsActive,
            dbc,
            ftpCacheDirPath
        };

        //logFtpError(L"[FTP] Starting file transfer for: " + stringToWString(url));

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
        if (!curl) {
            logError(L"[FTP] Failed to initialize CURL", FTP_LOG_PATH);
            return;
        }

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_USERPWD,
            (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
        curl_easy_setopt(curl.get(), CURLOPT_DIRLISTONLY, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_list_stream);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl.get(), CURLOPT_BUFFERSIZE, 32768); // 32KB буфер
        curl_easy_setopt(curl.get(), CURLOPT_TCP_KEEPALIVE, 0L);

        CURLcode res = curl_easy_perform(curl.get());
        //logFtpError(L"[FTP] curl_easy_perform() result: " + stringToWString(curl_easy_strerror(res)));

        if (res != CURLE_OK) {
            std::string error = curl_easy_strerror(res);
            logError(L"[FTP] Error: " + stringToWString(error), FTP_LOG_PATH);
        }

        //logFtpError(L"[FTP] File transfer completed.");
    }
    catch (const std::bad_alloc&) {
        logError(L"[FTP] Memory allocation failed", EXCEPTION_LOG_PATH);
    }
    catch (const std::exception& e) {
        logError(stringToWString("[FTP] Error: ") + stringToWString(e.what()), EXCEPTION_LOG_PATH);
    }
    catch (...) {
        logError(L"[FTP] Unknown error occurred", EXCEPTION_LOG_PATH);
    }
}






