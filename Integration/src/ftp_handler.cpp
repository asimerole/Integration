#include "ftp_handler.h"
#include "db_connection.h"
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

// функция для кодирования URL
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

// Callback функция для записи данных в файл
size_t Ftp::write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    if (written != size * nmemb) {
        logFtpError(L"[FTP1]: Ошибка записи в файл. Записано: " + stringToWString(std::to_string(written)));
    }
    return written;
}

// Сallback функция для отображения таблицы файлов
size_t Ftp::write_list(void* buffer, size_t size, size_t nmemb, void* userp) {
    if (!buffer || !userp) {
        logFtpError(L"write_list: NULL pointer detected!");
        return 0;
    }

    std::string* fileList = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;

    std::string chunk(static_cast<char*>(buffer), totalSize);
    logFtpError(L"[FTP] Received chunk: " + stringToWString(chunk));

    fileList->append(chunk);
    return totalSize;
}


// Метод сбора серверов из базы данных
void Ftp::collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc) {
    logFtpError(L"[FTP] Starting collectServers...");
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
            logFtpError(L"[FTP2]: Failed to allocate SQL statement handle in collectServers");
            return;
        }

        std::wstring query = L"SELECT u.unit, u.substation, s.object, fs.IP_addr, fs.login, fs.password, "
            L"fs.status, d.remote_path, d.local_path, d.isFourDigits "
            L"FROM [ReconDB].[dbo].[units] u "
            L"JOIN [ReconDB].[dbo].[struct_units] su ON u.id = su.unit_id "
            L"JOIN [ReconDB].[dbo].[struct] s ON su.struct_id = s.id "
            L"JOIN [ReconDB].[dbo].[FTP_servers] fs ON fs.unit_id = u.id "
            L"JOIN [ReconDB].[dbo].[FTP_Directories] d ON d.struct_id = s.id "
            L"WHERE fs.status = 1";

        logFtpError(L"[FTP] Executing SQL query...");
        ret = SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
            logFtpError(L"[FTP]: Не удалось выполнить SQL запрос сбора серверов!");
            goto cleanup;
        }

        while (SQLFetch(stmt) == SQL_SUCCESS) {
            ServerInfo server;
            wchar_t unit[256]{}, substation[256]{}, object[256]{}, ip[256]{}, login[256]{}, pass[256]{}, remoteFolderPath[256]{}, localFolderPath[256]{};
            int status = 0, isFourDigits = 0;
            SQLLEN unitLen, substationLen, objectLen, ipLen, loginLen, passLen, statusLen, isFourDigitsLen, remoteFolderPathLen, localFolderPathLen;

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

            server.ip = (ipLen != SQL_NULL_DATA) ? ip : L"";
            server.login = (loginLen != SQL_NULL_DATA) ? login : L"";
            server.pass = (passLen != SQL_NULL_DATA) ? pass : L"";

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

            logFtpError(L"[FTP] Checking connection for IP: " + server.ip + L"/" + server.remoteFolderPath + L"/");
            if (status == 1) {
                std::wstring url = Ftp::protocol() + server.ip + L"/" + server.remoteFolderPath + L"/";
                if (!checkConnection(wstringToString(url), wstringToString(server.login), wstringToString(server.pass))) {
                    logFtpError(L"[FTP] Connection failed for " + server.ip + L"/" + server.remoteFolderPath + L"/");
                    continue;
                }
            }

            server.unit = (unitLen != SQL_NULL_DATA) ? unit : L"";
            server.substation = (substationLen != SQL_NULL_DATA) ? substation : L"";
            server.object = (objectLen != SQL_NULL_DATA) ? object : L"";
            server.status = (statusLen != SQL_NULL_DATA) ? status : -1;
            server.localFolderPath = (localFolderPathLen != SQL_NULL_DATA) ? localFolderPath : L"";

            logFtpError(L"[FTP] Adding server with IP: " + server.ip);
            servers.push_back(server);
        }
    }
    catch (const std::exception& e) {
        logFtpError(L"[FTP] Exception in collectServers: " + stringToWString(e.what()));
    }
    catch (...) {
        logFtpError(L"[FTP] Unknown exception in collectServers!");
    }

cleanup:
    logFtpError(L"[FTP] Cleaning up SQL statement...");
    SQLFreeStmt(stmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    logFtpError(L"[FTP] collectServers completed.");
}

void Ftp::setFileTime(const std::string& filePath, const std::string& timestamp)
{
    SYSTEMTIME sysTime = {};
    FILETIME fileTime = {};

    // Разбираем строку timestamp (формат: "YYYY-MM-DD HH:MM:SS")
    if (sscanf(timestamp.c_str(), "%hd-%hd-%hd %hd:%hd:%hd",
        &sysTime.wYear, &sysTime.wMonth, &sysTime.wDay,
        &sysTime.wHour, &sysTime.wMinute, &sysTime.wSecond) != 6)
    {
        logFtpError(L"Invalid timestamp format: " + stringToWString(timestamp));
        return;
    }


    // Преобразуем SYSTEMTIME в FILETIME
    if (!SystemTimeToFileTime(&sysTime, &fileTime))
    {
        logFtpError(L"Failed to convert system time to file time");
        return;
    }

    // Открываем файл
    HANDLE hFile = CreateFileA(filePath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        logFtpError(L"Failed to open file for setting time: " + stringToWString(filePath));
        return;
    }

    // Устанавливаем только время модификации файла
    if (!SetFileTime(hFile, nullptr, nullptr, &fileTime))
    {
        logFtpError(L"Failed to set file modification time: " + stringToWString(filePath));
    }

    CloseHandle(hFile);
}

static size_t throw_away(void* ptr, size_t size, size_t nmemb, void* data)
{
    (void)ptr;
    (void)data;
    return (size_t)(size * nmemb);
}

// Загрузка файла с сервера 
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
            logFtpError(L"[FTP3]: Error opening file for writing: " + stringToWString(fileName));
            logFtpError(L"[FTP4]: Full file path: " + dataFile);
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
            logFtpError(L"[FTP5]: Error during file download for " + stringToWString(fileName) + L": " + stringToWString(curl_easy_strerror(res)));
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
            logFtpError(L"Failed to get file time!");
            curl_easy_cleanup(curl);
        }

        // logFtpError("[FTP]: File downloaded successfully: " + fileName);

        // Cleanup after download
        curl_easy_cleanup(curl);
    }
    else {
        logFtpError(L"[FTP6]: Failed to initialize CURL for downloading: " + stringToWString(fileName));
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

// Удаление файла с сервера
int Ftp::deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url)
{
    CURL* curl;
    CURLcode res;
    std::string fullRemotePath = url;

    curl = curl_easy_init();
    if (!curl) {
        logFtpError(L"[FTP9]: Error generating CURL.");
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
        logFtpError(stringToWString("[FTP]: File successfully deleted: ") + stringToWString(fullRemotePath));
        curl_easy_cleanup(curl);
        return 1;    
    }
    else {
        if (res != CURLE_FTP_COULDNT_RETR_FILE) {
            logFtpError(stringToWString("[FTP]: Error deleting file: ") + stringToWString(curl_easy_strerror(res)));
            curl_easy_cleanup(curl);
            return 1;
        }
        curl_easy_cleanup(curl);
        return 1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

// Проверка на успешное подключение к ФТП серверу
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
        logFtpError(stringToWString("[FTP11]: Ошибка инициализации libcurl"));
        return false;
    }
}

bool Ftp::isServerActive(const ServerInfo& server, SQLHDBC dbc)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    // Строим запрос, предполагая, что IP-адрес хранится в server.ip
    std::wstring query = L"SELECT [status] FROM [ReconDB].[dbo].[FTP_servers] WHERE [IP_addr] = '" + std::wstring(server.ip.begin(), server.ip.end()) + L"'";

    ret = SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (SQL_SUCCEEDED(ret)) {
        int status = 0;
        SQLLEN statusLen = 0;

        // Получаем данные о статусе
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            SQLGetData(stmt, 1, SQL_C_SLONG, &status, sizeof(status), &statusLen);

            // Проверяем, получили ли мы данные
            if (statusLen != SQL_NULL_DATA) {
                SQLFreeHandle(SQL_HANDLE_STMT, stmt);
                return (status == 1);  // Если статус 1, сервер активен
            }
        }
    }
    else {
        std::wstring errorMsg = stringToWString("[FTP]: Не удалось выполнить SQL запрос для проверки состояния сервера!");
        logFtpError(errorMsg);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return false;  // Если не удалось получить статус или сервер не найден
}

std::wstring sanitizeFileName(std::wstring name) {
    const std::wstring invalidChars = L"<>:\"/\\|?*";
    for (wchar_t& ch : name) {
        if (invalidChars.find(ch) != std::wstring::npos) {
            ch = L'_';  // Заменяем запрещённые символы на '_'
        }
    }
    return name;
}


// Функция для удаления пробелов в начале и конце строки
std::wstring trim(const std::wstring& str) {
    auto start = str.begin();
    while (start != str.end() && std::iswspace(*start)) {
        start++;
    }

    auto end = str.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::iswspace(*end));

    return std::wstring(start, end + 1);
}

// Создание дерева папок
void Ftp::createLocalDirectoryTree(ServerInfo& server, std::string rootFolder) {
    std::wstring rootFolderW = utf8_to_wstring(rootFolder);

    // Обрабатываем `server.unit`
    std::wstring unitW = sanitizeFileName(server.unit);
    std::wstring firstPart, secondPart;

    size_t separatorPos = unitW.find(L" - ");
    if (separatorPos != std::wstring::npos) {
        firstPart = unitW.substr(0, separatorPos);
        secondPart = trim(unitW.substr(separatorPos + 3));
    }
    else {
        firstPart = unitW;
    }

    // Формируем путь с защитой от запрещённых символов
    std::wstring path = rootFolderW + L"/" + sanitizeFileName(firstPart) + L"/" +
        sanitizeFileName(secondPart) + L"/" +
        sanitizeFileName(server.substation) + L"/" +
        sanitizeFileName(server.object) + L"/";

    // Безопасно создаём все директории
    if (!fs::exists(path)) {
        fs::create_directories(path);
    }
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

// Перенос файлов и удаление с сервера
void Ftp::fileTransfer(const ServerInfo& server, const std::string& url, const std::wstring& oneDrivePath, std::atomic_bool& ftpIsActive, SQLHDBC dbc, const std::wstring& ftpCacheDirPath)
{
    try {
        logFtpError(L"[FTP] Starting file transfer for: " + stringToWString(url));

        std::string fileListStr;
        CURL* curl;
        CURLcode res;
        curl = curl_easy_init();

        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
            curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_list);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fileListStr);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // секунд

            //logFtpError(L"[FTP] Executing curl_easy_perform...");
            res = curl_easy_perform(curl);
            logFtpError(L"[FTP] curl_easy_perform() result: " + stringToWString(curl_easy_strerror(res)));

            if (res == CURLE_OK) {
                std::istringstream iss(fileListStr);
                std::string fileName;
                //logFtpError(L"While is starting...");
                while (std::getline(iss, fileName)) {
                    try {
                        if (!ftpIsActive.load(std::memory_order_acquire)) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        size_t maxFilesPerSession = 500;
                        size_t processedFiles = 0;

                        if (++processedFiles > maxFilesPerSession) {
                            // logFtpError(L"[FTP] Max file limit reached, stopping early.");
                            break;
                        }

                        if (!fileName.empty()) {
                            fileName.erase(fileName.find_last_not_of("\r") + 1);

                            bool serverActive = false;
                            try {
                                //logFtpError(L"[FTP] Checking server activity...");
                                serverActive = isServerActive(server, dbc);
                                //logFtpError(L"[FTP] Server activity status: " + std::to_wstring(serverActive));
                            }
                            catch (const std::exception& e) {
                                logFtpError(stringToWString("[FTP] Exception in isServerActive: ") + stringToWString(e.what()));
                                continue;
                            }

                            if (serverActive) {
                                if (startsWithValidPrefix(fileName)) {
                                    if (downloadFile(fileName, server, url + fileName, ftpCacheDirPath)) {
                                        deleteFile(fileName, server, url);

                                        std::wstring unitW = (server.unit);
                                        std::wstring firstPart, secondPart;

                                        size_t separatorPos = unitW.find(L" - ");
                                        if (separatorPos != std::wstring::npos) {
                                            firstPart = unitW.substr(0, separatorPos);
                                            secondPart = unitW.substr(separatorPos + 3);
                                        }
                                        else {
                                            firstPart = unitW;
                                        }

                                        std::wstring oneDriveFullPathToFile = oneDrivePath + L"/" + firstPart + L"/" + secondPart + L"/" +
                                            (server.substation) + L"/" + (server.object) + L"/" + stringToWString(fileName);


                                        try {
                                            if (oneDriveFullPathToFile.length() >= 260) { 
                                                logOneDriveError(L"[OneDrive] Path too long, file skipped: " + oneDriveFullPathToFile);
                                                continue;
                                            }
                                            if (!fs::exists(oneDriveFullPathToFile)) {

                                                fs::copy_file(fs::path(ftpCacheDirPath) / fileName, oneDriveFullPathToFile, fs::copy_options::overwrite_existing);

                                                logOneDriveError(L"[OneDrive] File copied successfully: " + oneDriveFullPathToFile);
                                            }

                                        }
                                        catch (const fs::filesystem_error& e) {
                                            std::wstring path = fs::path(ftpCacheDirPath).wstring() + L"/" + stringToWString(fileName);

                                            std::wstring errorMessage = L"[OneDrive] Error copying file to OneDrive: ";
                                            errorMessage += utf8_to_wstring(e.what());
                                            errorMessage += L"\nSource file: " + path;
                                            errorMessage += L"\nTarget file: " + oneDriveFullPathToFile;
                                            logOneDriveError(errorMessage);

                                            continue; 
                                        }
                                    }
                                }
                            }
                            else {
                                logFtpError(L"[FTP] Server is not active, skipping file: " + stringToWString(fileName));
                                continue;
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        logFtpError(stringToWString("[FTP] Error during file processing: ") + stringToWString(e.what()));
                        continue;
                    }
                }
            }
            else {
                std::string errorMessage = curl_easy_strerror(res);
                if (errorMessage == "Access denied to remote resource") {
                    logFtpError(stringToWString("[FTP] Error retrieving file list: Remote folder not found at ") + stringToWString(url));
                }
                else {
                    logFtpError(stringToWString("[FTP] Error retrieving file list: ") + stringToWString(errorMessage));
                }
            }

            //logFtpError(L"[FTP] Cleaning up CURL...");
            curl_easy_cleanup(curl);
        }
        else {
            logFtpError(L"[FTP] Failed to initialize CURL.");
        }

        logFtpError(L"[FTP] File transfer completed.");
    }
    catch (const std::exception& e) {
        logFtpError(stringToWString("[FTP] Fatal error during fileTransfer: ") + stringToWString(e.what()));
    }
    catch (...) {
        logFtpError(L"[FTP] Unknown fatal error during fileTransfer.");
    }
}






