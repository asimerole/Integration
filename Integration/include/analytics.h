#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <atomic>

#include "utils.h"
#include "ftp_handler.h"
#include "db_connection.h"
#include "mail_handler.h"

class Analytics {
public:
	static Analytics& getInstance() {
		static Analytics instance;
		return instance;
	}

	// prohibit copying
	Analytics(const Analytics&) = delete;
	void operator=(const Analytics&) = delete;

	void AnalyticsScheduler();

private:
	Analytics() {}
	std::vector<std::string> GetAdminsFromDb(SQLHDBC dbc);

	bool SendServersAnalytics(const MailServerConfig& config, SQLHDBC dbc);

	void SaveToRegistryParams();

	std::vector<std::wstring> GetUnreachableServers(std::vector<ServerInfo> servers, SQLHDBC dbc);
};

