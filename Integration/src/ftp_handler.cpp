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
        if (encodedURL) {
            std::string result(encodedURL);
            curl_free(encodedURL);
            return result;
        }
        curl_easy_cleanup(curl);
    }
    return std::string();
}

// Callback функция для записи данных в файл
size_t Ftp::write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    if (written != size * nmemb) {
        logError(L"[FTP]: Ошибка записи в файл. Записано: " + stringToWString(std::to_string(written)));
    }
    return written;
}

// Сallback функция для отображения таблицы файлов
size_t Ftp::write_list(void* buffer, size_t size, size_t nmemb, void* userp) {
    std::ostringstream* fileList = static_cast<std::ostringstream*>(userp);
    fileList->write(static_cast<char*>(buffer), size * nmemb);
    return size * nmemb;
}

// Метод сбора серверов из базы данных
void Ftp::collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc) {
    static SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (stmt == SQL_NULL_HSTMT) {
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    }
    else {
        SQLFreeStmt(stmt, SQL_CLOSE); // Закрываем предыдущее выполнение
    }

    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[FTP]: Failed to allocate SQL statement handle in collectServers");
        return;
    }

    std::wstring query = L"SELECT u.unit, u.substation, s.object, fs.IP_addr, fs.login, fs.password, "
        L"fs.status, d.remote_path, d.local_path, s.recon_id "
        L"FROM [ReconDB].[dbo].[units] u "
        L"JOIN [ReconDB].[dbo].[struct_units] su ON u.id = su.unit_id "
        L"JOIN [ReconDB].[dbo].[struct] s ON su.struct_id = s.id "
        L"JOIN [ReconDB].[dbo].[FTP_servers] fs ON fs.unit_id = u.id "
        L"JOIN [ReconDB].[dbo].[FTP_Directories] d ON d.struct_id = s.id "
        L"WHERE fs.status = 1";

    ret = SQLExecDirect(stmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[FTP]: Не удалось выполнить SQL запрос сбора серверов!");
        goto cleanup;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ServerInfo server;
        wchar_t unit[256]{}, substation[256]{}, object[256]{}, ip[256]{}, login[256]{}, pass[256]{}, remoteFolderPath[256]{}, localFolderPath[256]{};
        int status = 0, reconId = -1;
        SQLLEN unitLen, substationLen, objectLen, ipLen, loginLen, passLen, statusLen, remoteFolderPathLen, localFolderPathLen, reconIdLen;

        if (SQLGetData(stmt, 1, SQL_C_WCHAR, unit, sizeof(unit), &unitLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 2, SQL_C_WCHAR, substation, sizeof(substation), &substationLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 3, SQL_C_WCHAR, object, sizeof(object), &objectLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 4, SQL_C_WCHAR, ip, sizeof(ip), &ipLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 5, SQL_C_WCHAR, login, sizeof(login), &loginLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 6, SQL_C_WCHAR, pass, sizeof(pass), &passLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 7, SQL_C_SLONG, &status, sizeof(status), &statusLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 8, SQL_C_WCHAR, remoteFolderPath, sizeof(remoteFolderPath), &remoteFolderPathLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 9, SQL_C_WCHAR, localFolderPath, sizeof(localFolderPath), &localFolderPathLen) != SQL_SUCCESS) continue;
        if (SQLGetData(stmt, 10, SQL_C_SLONG, &reconId, sizeof(reconId), &reconIdLen) != SQL_SUCCESS) continue;

        server.ip = (ipLen != SQL_NULL_DATA) ? ip : L"";
        server.login = (loginLen != SQL_NULL_DATA) ? login : L"";
        server.pass = (passLen != SQL_NULL_DATA) ? pass : L"";

        if (status == 1) {
            std::wstring url = Ftp::protocol() + server.ip + L":21";
            if (!checkConnection(wstringToString(url), wstringToString(server.login), wstringToString(server.pass))) {
                continue;
            }
        }

        server.unit = (unitLen != SQL_NULL_DATA) ? unit : L"";
        server.substation = (substationLen != SQL_NULL_DATA) ? substation : L"";
        server.object = (objectLen != SQL_NULL_DATA) ? object : L"";
        server.status = (statusLen != SQL_NULL_DATA) ? status : -1;
        server.remoteFolderPath = (remoteFolderPathLen != SQL_NULL_DATA) ? remoteFolderPath : L"";
        server.localFolderPath = (localFolderPathLen != SQL_NULL_DATA) ? localFolderPath : L"";
        server.reconId = (reconIdLen != SQL_NULL_DATA) ? reconId : -1;

        servers.erase(std::remove_if(servers.begin(), servers.end(),
            [&](const ServerInfo& s) {
                return s.reconId == reconId && s.ip == ip;
            }),
            servers.end());

        servers.push_back(server);

        //auto it = std::remove_if(servers.begin(), servers.end(), [&](const ServerInfo& s) {
        //    return s.reconId == reconId && s.ip == ip;
        //    });

        //servers.erase(it, servers.end());  // Удаляем все старые записи с таким же reconId и IP
        //servers.push_back(server);         // Добавляем новую запись
    }

cleanup:
    SQLFreeStmt(stmt, SQL_CLOSE);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return;
}

// Загрузка файла с сервера 
bool Ftp::downloadFile(const std::string& fileName, const ServerInfo& server, const std::string url, const std::wstring& ftpCacheDirPath)
{
    // logError("[FTP]: Starting file download for " + fileName + " from URL: " + url);

    CURL* curl;
    FILE* file = nullptr;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        // Construct the file path and convert it to a wide string for _wfopen
        std::wstring dataFile = ftpCacheDirPath + L"/" + stringToWString(fileName);
        // logError("[FTP]: Constructed file path for download: " + dataFile);

        // Set URL and credentials for the download
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());

        // Try to open the file for writing with _wfopen
        file = _wfopen(dataFile.c_str(), L"wb");
        if (file == nullptr) {
            logError(L"[FTP]: Error opening file for writing: " + stringToWString(fileName));
            logError(L"[FTP]: Full file path: " + dataFile);
            perror("fopen");  // Outputs error to stderr for debugging
            curl_easy_cleanup(curl);
            return false;
        }
        // logError("[FTP]: File opened successfully for writing: " + dataFile);

        // Set the write function and data handler for the download
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        // Perform the file download
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            logError(L"[FTP]: Error during file download for " + stringToWString(fileName) + L": " + stringToWString(curl_easy_strerror(res)));
            fclose(file);
            curl_easy_cleanup(curl);
            return false;
        }
        // logError("[FTP]: File downloaded successfully: " + fileName);

        // Cleanup after download
        fclose(file);
        curl_easy_cleanup(curl);
    }
    else {
        logError(L"[FTP]: Failed to initialize CURL for downloading: " + stringToWString(fileName));
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

    //logError(L"[FTP]: Finished attempting to download file: " + stringToWString(fileName));
    return true;
}

// Проверка на существованеи файла на сервере 
bool Ftp::checkIfFileExists(const ServerInfo& server, const std::string url)
{
    CURL* curl;
    CURLcode res;
    bool fileExists = false;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            fileExists = true;
        }
        else {
            logError(stringToWString("[FTP]: Проверка существования файла не удалась: ") + stringToWString(curl_easy_strerror(res)));
        }
        curl_easy_cleanup(curl);
    }

    return fileExists;
}

// Удаление файла с сервера
int Ftp::deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url)
{
    CURL* curl;
    CURLcode res;
    std::string fullRemotePath = url + filename;

    // Проверка существования файла перед удалением
    if (!checkIfFileExists(server, fullRemotePath)) {  
        logError(stringToWString("[FTP]: Файл не существует: ") + stringToWString(fullRemotePath));
        return -1;
    }

    curl = curl_easy_init();
    if (!curl) {
        logError(L"[FTP]: Ошибка инициализации CURL.");
        return -1;
    }

    std::string deleCommand = "DELE " + filename;

    curl_easy_setopt(curl, CURLOPT_URL, fullRemotePath.c_str());  
    curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, deleCommand.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, nullptr);

    res = curl_easy_perform(curl);
    bool fileStillExists = checkIfFileExists(server, fullRemotePath); 

    if (res == CURLE_OK) {
        if (!fileStillExists) {
            //logError(stringToWString("[FTP]: Файл успешно удален: ") + stringToWString(fullRemotePath));
            curl_easy_cleanup(curl);
            return 1;
        }
        else {
            //logError(stringToWString("[FTP]: Файл не был удален (ошибка при проверке): ") + stringToWString(fullRemotePath));
        }
    }
    else {
        //logError(stringToWString("[FTP]: Ошибка удаления файла: ") + stringToWString(curl_easy_strerror(res)));

        if (!fileStillExists) {
            //logError(stringToWString("[FTP]: Однако, файл больше не существует на сервере: ") + stringToWString(fullRemotePath));
            curl_easy_cleanup(curl);
            return 1;
        }
    }

    curl_easy_cleanup(curl);
    return 0;
}


// Проверка на успешное подключение к ФТП серверу
bool Ftp::checkConnection(const std::string& url, const std::string login, const std::string pass)
{
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (login + ":" + pass).c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            return true;
        }
        else {
            logError(stringToWString("[FTP]: Не удалось установить соединение ") + stringToWString(url) + L": " + stringToWString(curl_easy_strerror(res)));
            return false;
        }
        curl_easy_cleanup(curl);
    }
    else {
        logError(stringToWString("[FTP]: Ошибка инициализации libcurl"));
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
        logError(errorMsg);
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



// Перенос файлов и удаление с сервера
void Ftp::fileTransfer(ServerInfo& server, const std::string& url, const std::wstring& oneDrivePath, std::atomic_bool& ftpIsActive, SQLHDBC dbc, const std::wstring& ftpCacheDirPath)
{
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        std::ostringstream fileListStr;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, (wstringToString(server.login) + ":" + wstringToString(server.pass)).c_str());
        curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_list);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fileListStr);
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            std::istringstream iss(fileListStr.str());
            std::string fileName;
            while (std::getline(iss, fileName)) {
                if (!ftpIsActive) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                if (fileName.rfind("DAILY", 0) == 0) {

                }
                if (!fileName.empty() && isServerActive(server, dbc)) {
                    fileName.erase(fileName.find_last_not_of("\r") + 1);

                    if (fileName.rfind("REXPR", 0) == 0 || fileName.rfind("RECON", 0) == 0) {
                        // Установка файла в корневую папку
                        if (downloadFile(fileName, server, url + fileName, ftpCacheDirPath)) {
                            // 1. Удаление файла с сервера
                            deleteFile(fileName, server, url); 

                            // 2. Перемещение файла на OneDrive
                            std::wstring unitW = (server.unit);
                            std::wstring firstPart, secondPart;

                            size_t separatorPos = unitW.find(L" - ");
                            if (separatorPos != std::wstring::npos) {
                                firstPart = unitW.substr(0, separatorPos);       // пр. "Південне ТУ"
                                secondPart = unitW.substr(separatorPos + 3);     // пр. "Запорізький РЦОМ"
                            }
                            else {
                                firstPart = unitW; // Если разделитель не найден, оставляем как есть
                            }

                            std::wstring oneDriveFullPathToFile = oneDrivePath + L"/" + firstPart + L"/" + secondPart + L"/" +
                                (server.substation) + L"/" + (server.object) + L"/" + stringToWString(fileName);

                            if (!fs::exists(oneDriveFullPathToFile)) {
                                fs::copy_file(fs::path(ftpCacheDirPath) / fileName, oneDriveFullPathToFile);
                            }
                        }
                    }
                }
            }
        }
        else {
            if (std::string(curl_easy_strerror(res)) == "Access denied to remote resource") {
                logError(stringToWString("[FTP]: Ошибка при получении списка файлов: Удаленная папка по адресу ") + stringToWString(url) + stringToWString(" не найдена."));
            }
            else {
                logError(stringToWString("[FTP]: Ошибка при получении списка файлов: ") + stringToWString(curl_easy_strerror(res)));
            }
        }
        curl_easy_cleanup(curl);
    }
}





