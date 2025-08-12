#include "analytics.h"

std::vector<std::wstring> Analytics::GetUnreachableServers(std::vector<ServerInfo> servers, SQLHDBC dbc)
{
	std::vector<std::wstring> unreachableServers;
	for (const auto& server : servers) {
		std::wstring url = Ftp::getInstance().protocol() + server.ip;
		if (!Ftp::getInstance().checkConnection(wstringToString(url), wstringToString(server.login), wstringToString(server.pass))) {
			unreachableServers.push_back(server.unit + L" - " + server.substation + L" (" + server.ip + L")");
		}
	}
	return unreachableServers;
}

std::vector<std::string> Analytics::GetAdminsFromDb(SQLHDBC dbc)
{
	std::vector<std::string> admins;

	SQLHSTMT hstmt = SQL_NULL_HSTMT;
	SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

	if (!SQL_SUCCEEDED(ret)) {
		logError(L"[Mail] Failed to allocate SQL statement handle", EMAIL_LOG_PATH);
		return {};
	}

	const wchar_t* sqlQuery = LR"(
        SELECT 
            [login] AS login
        FROM [ReconDB].[dbo].[users] 
        WHERE [status] = 1 AND [type] = 'Адмін'
    )";

	ret = SQLExecDirectW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
	if (!SQL_SUCCEEDED(ret)) {
		logError(L"[Mail] Failed to execute SQL query", EMAIL_LOG_PATH);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		return {};
	}

	// Buffer for storing the extracted login
	wchar_t loginBuffer[256];
	SQLLEN loginIndicator;

	ret = SQLBindCol(hstmt, 1, SQL_C_WCHAR, loginBuffer, sizeof(loginBuffer), &loginIndicator);
	if (!SQL_SUCCEEDED(ret)) {
		logError(L"[Mail] Failed to bind login column", EMAIL_LOG_PATH);
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		return {};
	}

	// Extract the result rows
	while (SQLFetch(hstmt) == SQL_SUCCESS) {
		if (loginIndicator != SQL_NULL_DATA) {
			std::string login = wstringToUtf8(loginBuffer);
			admins.push_back(login);
		}
	}

	// Freeing up resources
	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

	return admins;
} 


bool Analytics::SendServersAnalytics(const MailServerConfig& config, SQLHDBC dbc)
{
	CURL* curl = nullptr;
	struct curl_slist* recipients = nullptr;
	struct curl_slist* headers = nullptr;

	bool result = false;

	curl_mime* mime = nullptr;
    try {
		std::vector<std::wstring> unreachableServers = GetUnreachableServers(Ftp::getInstance().getServers(), dbc);
		
		std::wstring mailTemplate = L"Кількість недоступних серверів: " + std::to_wstring(unreachableServers.size());
		mailTemplate += L"\n\nСписок недоступних серверів:\n";
		for (const auto& server : unreachableServers) {
			mailTemplate += server + L"\n";
		}

		const std::vector<std::string> adminRecipientsList = GetAdminsFromDb(dbc);

		curl = curl_easy_init();
		if (!curl) {
			logError(L"[Analytics] Failed to initialize CURL", EXCEPTION_LOG_PATH);
			goto cleanup;
		}

		std::string protocol = config.use_ssl ? "smtps://" : "smtp://";
		std::string url = protocol + wstringToString(config.smtp_server) + ":" + wstringToString(config.port);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

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

		// Setting up recipients
		for (const std::string& recipient : adminRecipientsList) {
			recipients = curl_slist_append(recipients, ("<" + recipient + ">").c_str());
			logError(L"[Mail] Added recipient: " + stringToWString(recipient), EMAIL_LOG_PATH);
		}
		curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

		std::string nameSender = wstringToUtf8(config.name_sender);
		std::string fromHeader = "From: \"" + nameSender + "\" <" + wstringToString(config.email_sender) + ">";

		std::wstring subjectUtf8 = L"Щоденний звіт про стан каналів зв'язку";
		std::string subjectHeader = "Subject: " + wstringToUtf8(subjectUtf8);

		headers = curl_slist_append(headers, fromHeader.c_str());
		headers = curl_slist_append(headers, subjectHeader.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		mime = curl_mime_init(curl);
		curl_mimepart* part = curl_mime_addpart(mime);
		curl_mime_data(part, wstringToUtf8(mailTemplate).c_str(), CURL_ZERO_TERMINATED);
		curl_mime_type(part, "text/plain; charset=UTF-8");

		curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

		// Sending a message
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			logError(L"[Mail] Failed to send email. CURL error: " + stringToWString(curl_easy_strerror(res)), EMAIL_LOG_PATH);
			goto cleanup;
		}

		result = true;
    }
    catch (const std::exception& ex) {
		logError(L"[Analytics] Error sending analytics: " + stringToWString(ex.what()), EXCEPTION_LOG_PATH);
    }
	catch (...) {
		logError(L"[Analytics] Unknown error while sending analytics", EXCEPTION_LOG_PATH);
		return false;
	}
cleanup:
	{
		if (recipients) curl_slist_free_all(recipients);
		if (headers) curl_slist_free_all(headers);
		if (mime) curl_mime_free(mime);
		if (curl) curl_easy_cleanup(curl);
		
		return result;
	}
}

void Analytics::AnalyticsScheduler()
{
	bool alreadySendToday = false;
	while (true) {
		std::time_t now = std::time(nullptr);
		std::tm localTime{};
#ifdef _WIN32
		localtime_s(&localTime, &now);
#else
		localtime_r(&now, &localTime);
#endif
		// Check if it's a new day
		if (localTime.tm_hour == 0 && localTime.tm_min == 0) {
			if (!alreadySendToday) {
				try {
					Database db;
					db.connectToDatabase();
					if (!db.isConnected()) {
						logError(L"[Integration] Failed to connect to the database.", INTEGRATION_LOG_PATH);
						throw;
					}
					SQLHDBC dbc = db.getConnectionHandle();
					std::string configJson = wstringToUtf8(Database::getJsonConfigFromDatabase("mail", dbc));

					// Check for empty string
					if (!configJson.empty()) {
						try {
							// Parsing JSON
							auto config = parseMailServerConfig(configJson);
							if (SendServersAnalytics(config, dbc)) {
								alreadySendToday = true; // Mark that analytics has been sent today
							}
						}
						catch (const std::exception& e) {
							logError(L"[Mail] Failed to parse config JSON or send emails in analytics function: " + stringToWString(e.what()), EXCEPTION_LOG_PATH);

						}
					}
					else {
						logError(L"[Mail] Config JSON is empty. Skipping email sending.", EMAIL_LOG_PATH);
					}
					db.disconnectFromDatabase();
				}
				catch (const std::exception& ex) {
					logError(L"[Analytics] Error sending analytics: " + stringToWString(ex.what()), EXCEPTION_LOG_PATH);
				}
			}

		}
		else {
			alreadySendToday = false; // Reset for the next day
		}
		std::this_thread::sleep_for(std::chrono::minutes(1)); // Check every minute
	}
}

void Analytics::SaveToRegistryParams()
{

}
