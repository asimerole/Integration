#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <shellapi.h>
#include <mailio/message.hpp>
#include <mailio/smtp.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <curl/curl.h>
#include <cstring>
#include <thread>
#include <atomic>
#include "mail_handler.h"
#include <nlohmann/json.hpp>


namespace fs = boost::filesystem;
using json = nlohmann::json;

// Парсинг конфигурации почтового сервера из JSON строки
MailServerConfig parseMailServerConfig(const std::string& jsonString) {
    try {
        // Парсинг JSON
        json configJson = json::parse(jsonString);

        // Логгирование успешного парсинга
        //logError("Successfully parsed JSON. Extracting fields...");

        MailServerConfig config;

        // Извлечение SMTP сервера
        if (configJson.contains("SMTP server") && !configJson["SMTP server"].is_null()) {
            config.smtp_server = utf8_to_wstring(configJson["SMTP server"].get<std::string>());
            //logError("SMTP server: " + wstringToUtf8(config.smtp_server));
        }
        else {
            logError(L"[Mail] Field 'SMTP server' is missing or null.");
        }

        // Извлечение необходимости аутентификации
        if (configJson.contains("auth") && !configJson["auth"].is_null()) {
            config.auth_required = configJson["auth"].get<bool>();
            //logError("Auth required: " + std::to_string(config.auth_required));
        }
        else {
            config.auth_required = false; // Значение по умолчанию
            //logError("Field 'auth' is missing or null. Defaulting to false.");
        }

        // Извлечение логина
        if (config.auth_required && configJson.contains("login") && !configJson["login"].is_null()) {
            config.auth_login = utf8_to_wstring(configJson["login"].get<std::string>());
            //logError("Auth login: " + wstringToUtf8(config.auth_login));
        }
        else if (config.auth_required) {
            logError(L"[Mail] Field 'login' is missing or null, but auth is required.");
        }

        // Извлечение пароля
        if (config.auth_required && configJson.contains("password") && !configJson["password"].is_null()) {
            config.auth_password = utf8_to_wstring(configJson["password"].get<std::string>());
            //logError("Auth password: [hidden for security]");
        }
        else if (config.auth_required) {
            logError(L"[Mail] Field 'password' is missing or null, but auth is required.");
        }

        // Извлечение email отправителя
        if (configJson.contains("email_sender") && !configJson["email_sender"].is_null()) {
            config.email_sender = utf8_to_wstring(configJson["email_sender"].get<std::string>());
            //logError("Email sender: " + wstringToUtf8(config.email_sender));
        }
        else {
            logError(L"[Mail] Field 'email_sender' is missing or null.");
        }

        // Извлечение port 
        if (configJson.contains("port") && !configJson["port"].is_null()) {
            config.port = utf8_to_wstring(configJson["port"].get<std::string>());
            //logError("Port: " + wstringToUtf8(config.port));
        }
        else {
            logError(L"[Mail] Field 'port' is missing or null.");
        }

        // Извлечение шаблона сообщения
        if (configJson.contains("msg_template") && !configJson["msg_template"].is_null()) {
            config.message_template = utf8_to_wstring(configJson["msg_template"].get<std::string>());
            //logError("Message template: " + wstringToUtf8(config.message_template));
        }
        else {
            logError(L"[Mail] Field 'msg_template' is missing or null.");
        }

        // Извлечение имени отправителя
        if (configJson.contains("name_sender") && !configJson["name_sender"].is_null()) {
            config.name_sender = utf8_to_wstring(configJson["name_sender"].get<std::string>());
            //logError("Name sender: " + wstringToUtf8(config.name_sender));
        }
        else {
            logError(L"[Mail] Field 'name_sender' is missing or null.");
        }

        // Извлечение использования SSL
        if (configJson.contains("ssl") && !configJson["ssl"].is_null()) {
            config.use_ssl = configJson["ssl"].get<bool>();
            //logError("Use SSL: " + std::to_string(config.use_ssl));
        }
        else {
            config.use_ssl = false; // Значение по умолчанию
            logError(L"[Mail] Field 'ssl' is missing or null. Defaulting to false.");
        }

        return config;

    }
    catch (const json::exception& e) {
        logError(L"[Mail] JSON parsing error: " + stringToWString(e.what()));
        throw; // Пробрасываем исключение дальше
    }
    catch (const std::exception& e) {
        logError(L"[Mail] Unexpected error in parseMailServerConfig: " + stringToWString(e.what()));
        throw;
    }
}

std::map<std::string, std::vector<std::string>> loadUsersFromDatabase(SQLHDBC dbc) {
    std::map<std::string, std::vector<std::string>> substationUsers;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to allocate SQL statement handle");
        return {};
    }

    const wchar_t* sqlQuery = LR"(
        SELECT 
            us.[login] AS login,
            un.[substation] AS substation
        FROM [ReconDB].[dbo].[users] us
        JOIN [ReconDB].[dbo].[users_units] uu ON us.[id] = uu.[user_id]
        JOIN [ReconDB].[dbo].[units] un ON uu.[unit_id] = un.[id]
        WHERE us.[status] = 1 AND us.[send_mail] = 1
    )";

    // Подготавливаем запрос
    ret = SQLExecDirectW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to execute SQL query");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Буфер для хранения извлечённого логина
    wchar_t loginBuffer[256];
    wchar_t substationBuffer[256];
    SQLLEN loginIndicator, substationIndicator;

    ret = SQLBindCol(hstmt, 1, SQL_C_WCHAR, loginBuffer, sizeof(loginBuffer), &loginIndicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to bind login column");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    ret = SQLBindCol(hstmt, 2, SQL_C_WCHAR, substationBuffer, sizeof(substationBuffer), &substationIndicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to bind substation column");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Извлекаем строки результата
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        if (loginIndicator != SQL_NULL_DATA && substationIndicator != SQL_NULL_DATA) {
            std::string login = wstringToUtf8(loginBuffer);
            std::string substation = wstringToUtf8(substationBuffer);
            substationUsers[substation].push_back(login);
        }
    }

    // Освобождаем ресурсы
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return substationUsers;
}

bool sendEmails(const MailServerConfig& config, const std::map<std::string, std::vector<std::string>>& users, const FilePair& pair) {
    try {
        // Найти пользователей для указанной подстанции
        auto it = users.find(wstringToString(pair.recon.substation));
        if (it == users.end()) {
            logError(L"[Mail] No users found for substation: " + pair.recon.substation);
            return false; // Если пользователей нет, выходим
        }

        //if (!isWithinLastNDays(pair.date, 3)) {
        //    return false;
        //}

        const std::vector<std::string>& recipientsList = it->second;

        logError(L"[Mail] Initializing CURL for email sending...");

        CURLcode res;
        CURL* curl = curl_easy_init();
        if (!curl) {
            logError(L"[Mail] CURL initialization failed.");
            return false;
        }

        logError(L"[Mail] CURL initialized successfully");

        // Настройка соединения с SMTP-сервером
        std::string protocol = config.use_ssl ? "smtps://" : "smtp://";
        std::string url = protocol + wstringToString(config.smtp_server) + ":" + wstringToString(config.port);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        logError(L"[Mail] Set SMTP server URL to: " + stringToWString(url));

        // Включение проверки сертификатов (для SSL)
        if (config.use_ssl) {
            curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); // Проверка хоста сертификата
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // Проверка цепочки сертификатов
            logError(L"[Mail] SSL verification enabled.");
        }

        // Аутентификация (если требуется)
        if (config.auth_required) {
            curl_easy_setopt(curl, CURLOPT_USERNAME, wstringToString(config.auth_login).c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, wstringToString(config.auth_password).c_str());
            logError(L"[Mail] Authentication enabled for user: " + config.auth_login);
        }

        // Отправитель
        std::string mailFrom = "<" + wstringToString(config.email_sender) + ">";
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailFrom.c_str());
        logError(L"[Mail] Set MAIL FROM to: " + stringToWString(mailFrom));

        // Настройка получателей
        struct curl_slist* recipients = nullptr;
        for (const std::string& recipient : recipientsList) {
            recipients = curl_slist_append(recipients, ("<" + recipient + ">").c_str());
            logError(L"[Mail] Added recipient: " + stringToWString(recipient));
        }
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // Формирование заголовков письма
        std::string nameSender = wstringToUtf8(config.name_sender);
        std::string fromHeader = "From: \"" + nameSender + "\" <" + wstringToString(config.email_sender) + ">";
        std::wstring subject = pair.recon.unit + L": " + pair.recon.substation + L" (" + pair.recon.object + L")";
        std::string subjectHeader = "Subject: " + wstringToUtf8(subject);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, fromHeader.c_str());
        headers = curl_slist_append(headers, subjectHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        logError(L"[Mail] Headers set: " + stringToWString(fromHeader) + L"; " + stringToWString(subjectHeader));

        curl_mime* mime = curl_mime_init(curl);

        // Добавление текста сообщения
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_data(part, wstringToUtf8(config.message_template).c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/plain; charset=UTF-8");
        logError(L"[Mail] Email text body set successfully.");

        // Добавление дата файла
        if (pair.hasDataFile) {
            std::string dataFilePath = wstringToString(pair.parentFolderPath + L"\\" + pair.dataFileName);
            logError(L"[Mail] Attaching data file: " + stringToWString(dataFilePath));

            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, dataFilePath.c_str());                         // путь к первому файлу
            curl_mime_filename(part, wstringToString(pair.dataFileName).c_str());   // имя файла в письме
            logError(L"[Mail] Data file attached successfully: " + pair.dataFileName);
        }

        // Добавление экспресс файла
        if (pair.hasExpressFile) {
            std::string expressFilePath = wstringToString(pair.parentFolderPath + L"\\" + pair.expressFileName);
            logError(L"[Mail] Attaching express file: " + stringToWString(expressFilePath));

            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, expressFilePath.c_str());                      // путь ко второму файлу
            curl_mime_filename(part, wstringToString(pair.expressFileName).c_str());// имя файла в письме
            logError(L"[Mail] Express file attached successfully: " + pair.expressFileName);
        }

        logError(L"[Mail] CURL upload setup completed.");

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        // Отправка сообщения
        logError(L"[Mail] Starting email transmission...");
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            logError(L"[Mail] Failed to send email. CURL error: " + stringToWString(curl_easy_strerror(res)));
        }
        else {
            logError(L"[Mail] Email sent successfully.");
        }

        // Освобождение ресурсов
        curl_slist_free_all(recipients);
        curl_slist_free_all(headers);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);

        logError(L"[Mail] CURL resources cleaned up.\n");

        // Задержка перед отправкой следующего письма
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return true;
    }
    catch (const std::exception& ex) {
        logError(L"[Mail] General failure in sendEmails: " + stringToWString(ex.what()));
        return false;
    }
    catch (...) {
        logError(L"[Mail] An unknown error occurred in sendEmails.");
        return false;
    }
}






