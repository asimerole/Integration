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

// Parsing mail server configuration from JSON string
MailServerConfig parseMailServerConfig(const std::string& jsonString) {
    try {
        // Parsing JSON
        json configJson = json::parse(jsonString);

        //logEmailError("Successfully parsed JSON. Extracting fields...");
        
        MailServerConfig config;

        // Extract SMTP server
        if (configJson.contains("SMTP server") && !configJson["SMTP server"].is_null()) {
            config.smtp_server = utf8_to_wstring(configJson["SMTP server"].get<std::string>());
            //logEmailError("SMTP server: " + wstringToUtf8(config.smtp_server));
        }
        else {
            logError(L"[Mail] Field 'SMTP server' is missing or null.", EMAIL_LOG_PATH);
        }

        // Extracting the need for authentication
        if (configJson.contains("auth") && !configJson["auth"].is_null()) {
            config.auth_required = configJson["auth"].get<bool>();
            //logEmailError("Auth required: " + std::to_string(config.auth_required));
        }
        else {
            config.auth_required = false; // Default value
            //logEmailError("Field 'auth' is missing or null. Defaulting to false.");
        }

        // Extract login
        if (config.auth_required && configJson.contains("login") && !configJson["login"].is_null()) {
            config.auth_login = utf8_to_wstring(configJson["login"].get<std::string>());
            //logEmailError("Auth login: " + wstringToUtf8(config.auth_login));
        }
        else if (config.auth_required) {
            logError(L"[Mail] Field 'login' is missing or null, but auth is required.", EMAIL_LOG_PATH);
        }

        // Extract password
        if (config.auth_required && configJson.contains("password") && !configJson["password"].is_null()) {
            config.auth_password = utf8_to_wstring(configJson["password"].get<std::string>());
            //logEmailError("Auth password: [hidden for security]");
        }
        else if (config.auth_required) {
            logError(L"[Mail] Field 'password' is missing or null, but auth is required.", EMAIL_LOG_PATH);
        }

        // Extracting Sender's Email
        if (configJson.contains("email_sender") && !configJson["email_sender"].is_null()) {
            config.email_sender = utf8_to_wstring(configJson["email_sender"].get<std::string>());
            //logEmailError("Email sender: " + wstringToUtf8(config.email_sender));
        }
        else {
            logError(L"[Mail] Field 'email_sender' is missing or null.", EMAIL_LOG_PATH);
        }

        // Extract port
        if (configJson.contains("port") && !configJson["port"].is_null()) {
            config.port = utf8_to_wstring(configJson["port"].get<std::string>());
            //logEmailError("Port: " + wstringToUtf8(config.port));
        }
        else {
            logError(L"[Mail] Field 'port' is missing or null.", EMAIL_LOG_PATH);
        }

        // Retrieving a message template
        if (configJson.contains("msg_template") && !configJson["msg_template"].is_null()) {
            config.message_template = utf8_to_wstring(configJson["msg_template"].get<std::string>());
            //logEmailError("Message template: " + wstringToUtf8(config.message_template));
        }
        else {
            logError(L"[Mail] Field 'msg_template' is missing or null.", EMAIL_LOG_PATH);
        }

        // Extracting Sender Name
        if (configJson.contains("name_sender") && !configJson["name_sender"].is_null()) {
            config.name_sender = utf8_to_wstring(configJson["name_sender"].get<std::string>());
            //logEmailError("Name sender: " + wstringToUtf8(config.name_sender));
        }
        else {
            logError(L"[Mail] Field 'name_sender' is missing or null.", EMAIL_LOG_PATH);
        }

        // Extract SSL usage
        if (configJson.contains("ssl") && !configJson["ssl"].is_null()) {
            config.use_ssl = configJson["ssl"].get<bool>();
            //logEmailError("Use SSL: " + std::to_string(config.use_ssl));
        }
        else {
            config.use_ssl = false; // default value
            logError(L"[Mail] Field 'ssl' is missing or null. Defaulting to false.", EMAIL_LOG_PATH);
        }

        return config;

    }
    catch (const json::exception& e) {
        logError(L"[Mail] JSON parsing error: " + stringToWString(e.what()), EMAIL_LOG_PATH);
        throw; 
    }
    catch (const std::exception& e) {
        logError(L"[Mail] Unexpected error in parseMailServerConfig: " + stringToWString(e.what()), EMAIL_LOG_PATH);
        throw;
    }
}

std::map<std::string, std::vector<std::string>> loadUsersFromDatabase(SQLHDBC dbc) {
    std::map<std::string, std::vector<std::string>> substationUsers;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to allocate SQL statement handle", EMAIL_LOG_PATH);
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

    // Preparing a request
    ret = SQLExecDirectW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to execute SQL query", EMAIL_LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Buffer for storing the extracted login
    wchar_t loginBuffer[256];
    wchar_t substationBuffer[256];
    SQLLEN loginIndicator, substationIndicator;

    ret = SQLBindCol(hstmt, 1, SQL_C_WCHAR, loginBuffer, sizeof(loginBuffer), &loginIndicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to bind login column", EMAIL_LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    ret = SQLBindCol(hstmt, 2, SQL_C_WCHAR, substationBuffer, sizeof(substationBuffer), &substationIndicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"[Mail] Failed to bind substation column", EMAIL_LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Extract the result rows
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        if (loginIndicator != SQL_NULL_DATA && substationIndicator != SQL_NULL_DATA) {
            std::string login = wstringToUtf8(loginBuffer);
            std::string substation = wstringToUtf8(substationBuffer);
            substationUsers[substation].push_back(login);
        }
    }

    // Freeing up resources
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return substationUsers;
}

bool sendEmails(const MailServerConfig& config, const std::map<std::string, std::vector<std::string>>& users, const FileInfo& fileInfo) {
    try {

        std::shared_ptr<ExpressFile> expressFile = nullptr;
        std::shared_ptr<DataFile> dataFile = nullptr;
        std::shared_ptr<BaseFile> baseFile = nullptr;

        switch (fileInfo.files.size()) {
        case 1:
            if (typeid(*fileInfo.files[0]) == typeid(BaseFile)) {
                baseFile = std::dynamic_pointer_cast<BaseFile>(fileInfo.files[0]);
            }
            else {
                logError(L"[Mail] Expected only BaseFile, got derived type.", EMAIL_LOG_PATH);
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
                    logError(L"[Mail] Unknown file type in fileInfo.files.", EMAIL_LOG_PATH);
                }
            }
            break;

        default:
            logError(L"[Mail] Unexpected number of files. Expected 1 or 2.", EMAIL_LOG_PATH);
            break;
        }

        std::shared_ptr<BaseFile> file = expressFile ? expressFile : baseFile;

        // Find users for the specified substation
        auto it = users.find(wstringToString(file->substation));
        if (it == users.end()) {
            logError(L"[Mail] No users found for substation: " + file->substation, EMAIL_LOG_PATH);
            return false; // If there are no users, exit
        }

        if (!isWithinLastNDays(file->date, 3)) {
            return false;
        }

        const std::vector<std::string>& recipientsList = it->second;

        logError(L"[Mail] Initializing CURL for email sending...", EMAIL_LOG_PATH);

        CURLcode res;
        CURL* curl = curl_easy_init();
        if (!curl) {
            logError(L"[Mail] CURL initialization failed.", EMAIL_LOG_PATH);
            return false;
        }

        logError(L"[Mail] CURL initialized successfully", EMAIL_LOG_PATH);

        // Setting up a connection to the SMTP server
        std::string protocol = config.use_ssl ? "smtps://" : "smtp://";
        std::string url = protocol + wstringToString(config.smtp_server) + ":" + wstringToString(config.port);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        logError(L"[Mail] Set SMTP server URL to: " + stringToWString(url), EMAIL_LOG_PATH);

        // Enable certificate verification (for SSL)
        if (config.use_ssl) {
            curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); // Host Certificate Verification
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // Verifying the certificate chain
            logError(L"[Mail] SSL verification enabled.", EMAIL_LOG_PATH);
        }

        // Authentication (if required)
        if (config.auth_required) {
            curl_easy_setopt(curl, CURLOPT_USERNAME, wstringToString(config.auth_login).c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, wstringToString(config.auth_password).c_str());
            logError(L"[Mail] Authentication enabled for user: " + config.auth_login, EMAIL_LOG_PATH);
        }

        // Sender
        std::string mailFrom = "<" + wstringToString(config.email_sender) + ">";
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailFrom.c_str());
        logError(L"[Mail] Set MAIL FROM to: " + stringToWString(mailFrom), EMAIL_LOG_PATH);

        // Setting up recipients
        struct curl_slist* recipients = nullptr;
        for (const std::string& recipient : recipientsList) {
            recipients = curl_slist_append(recipients, ("<" + recipient + ">").c_str());
            logError(L"[Mail] Added recipient: " + stringToWString(recipient), EMAIL_LOG_PATH);
        }
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // Forming letter headers
        std::string nameSender = wstringToUtf8(config.name_sender);
        std::string fromHeader = "From: \"" + nameSender + "\" <" + wstringToString(config.email_sender) + ">";
        std::wstring subject = file->unit + L": " + file->substation + L" (" + file->object + L")";
        std::string subjectHeader = "Subject: " + wstringToUtf8(subject);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, fromHeader.c_str());
        headers = curl_slist_append(headers, subjectHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        logError(L"[Mail] Headers set: " + stringToWString(fromHeader) + L"; " + stringToWString(subjectHeader), EMAIL_LOG_PATH);

        curl_mime* mime = curl_mime_init(curl);
        if (!mime) {
            logError(L"[Mail] curl_mime_init failed Ч returned nullptr.", EMAIL_LOG_PATH);
            return false;
        }


        // Adding message text
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_data(part, wstringToUtf8(config.message_template).c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/plain; charset=UTF-8");
        logError(L"[Mail] Email text body set successfully.", EMAIL_LOG_PATH);

        // Adding a data file
        if (dataFile && dataFile->hasDataFile) {
            std::string dataFilePath = wstringToString(dataFile->parentFolderPath + L"\\" + dataFile->fileName);
            logError(L"[Mail] Attaching data file: " + stringToWString(dataFilePath), EMAIL_LOG_PATH);

            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, dataFilePath.c_str());                          // путь к первому файлу
            curl_mime_filename(part, wstringToString(dataFile->fileName).c_str());   // им€ файла в письме
            logError(L"[Mail] Data file attached successfully: " + dataFile->fileName, EMAIL_LOG_PATH);
        }

        // Adding an express file
        if (expressFile && expressFile->hasExpressFile) {
            std::string expressFilePath = wstringToString(expressFile->parentFolderPath + L"\\" + expressFile->fileName);
            logError(L"[Mail] Attaching express file: " + stringToWString(expressFilePath), EMAIL_LOG_PATH);

            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, expressFilePath.c_str());                          // путь ко второму файлу
            curl_mime_filename(part, wstringToString(expressFile->fileName).c_str());   // им€ файла в письме
            logError(L"[Mail] Express file attached successfully: " + expressFile->fileName, EMAIL_LOG_PATH);
        }

        // Processing a regular file
        if (baseFile) {
            std::string baseFilePath = wstringToString(baseFile->parentFolderPath + L"\\" + baseFile->fileName);
            logError(L"[Mail] Attaching base file: " + stringToWString(baseFilePath), EMAIL_LOG_PATH);

            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, baseFilePath.c_str());
            curl_mime_filename(part, wstringToString(baseFile->fileName).c_str());
            logError(L"[Mail] Base file attached successfully: " + baseFile->fileName, EMAIL_LOG_PATH);
        }


        logError(L"[Mail] CURL upload setup completed.", EMAIL_LOG_PATH);

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        // Sending a message
        logError(L"[Mail] Starting email transmission...", EMAIL_LOG_PATH);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            logError(L"[Mail] Failed to send email. CURL error: " + stringToWString(curl_easy_strerror(res)), EMAIL_LOG_PATH);
        }
        else {
            logError(L"[Mail] Email sent successfully.", EMAIL_LOG_PATH);
        }

        // Resource release
        curl_slist_free_all(recipients);
        curl_slist_free_all(headers);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);

        logError(L"[Mail] CURL resources cleaned up.\n", EMAIL_LOG_PATH);

        // Delay before sending the next letter
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return true;
    }
    catch (const std::exception& ex) {
        logError(L"[Mail] General failure in sendEmails: " + stringToWString(ex.what()), EMAIL_LOG_PATH);
        return false;
    }
    catch (...) {
        logError(L"[Mail] An unknown error occurred in sendEmails.", EMAIL_LOG_PATH);
        return false;
    }
}






