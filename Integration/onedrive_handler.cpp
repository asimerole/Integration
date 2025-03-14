#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "onedrive_handler.h"
#include <nlohmann/json.hpp>


using json = nlohmann::json;

OneDriveConfig parseOneDriveConfig(const std::string& jsonString)
{
    try {
        // Парсинг JSON
        json configJson = json::parse(jsonString);

        OneDriveConfig config;

        // Извлечение SMTP сервера
        if (configJson.contains("months") && !configJson["months"].is_null()) {
            config.monthCount = configJson["months"].get<int>();
        }
        else {
            logError(stringToWString("Field 'months' is missing or null."));
        }

        // Извлечение необходимости аутентификации
        if (configJson.contains("path") && !configJson["path"].is_null()) {
            config.oneDrivePath = utf8_to_wstring(configJson["path"].get<std::string>());
        }
        else {
            logError(stringToWString("Field 'path' is missing or null."));
        }

        return config;

    }
    catch (const json::exception& e) {
        logError(stringToWString("JSON parsing error: ") + stringToWString(e.what()));
        throw; // Пробрасываем исключение дальше
    }
    catch (const std::exception& e) {
        logError(stringToWString("Unexpected error in parseMailServerConfig: ") + stringToWString(e.what()));
        throw;
    }
}

std::tm parseDate(const std::wstring& dateStr)
{
    std::wistringstream ss(dateStr);
    std::tm date = {};
    ss >> std::get_time(&date, L"%d/%m/%Y");

    if (ss.fail()) {
        std::wstring errorMsg = L"Failed to parse date: " + dateStr;
        throw std::runtime_error(wstringToUtf8(errorMsg));
    }
    return date;
}

int monthsBetween(const std::tm& fromDate, const std::tm& toDate)
{
    int yearDiff = toDate.tm_year - fromDate.tm_year;
    int monthDiff = toDate.tm_mon - fromDate.tm_mon;
    return yearDiff * 12 + monthDiff;
}

void deleteOldFiles(const std::wstring& path, int maxMonths, std::wstring dateStr) {
    try {
        if (dateStr.empty())
            return;
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                std::tm fileDate = parseDate(dateStr);

                // Текущая дата
                auto now = std::chrono::system_clock::now();
                std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
                std::tm currentDate = *std::localtime(&nowTimeT);

                int monthDiff = monthsBetween(fileDate, currentDate);

                if (monthDiff > maxMonths) {
                    std::wcout << L"Deleting file: " << entry.path().wstring() << L"\n";
                    fs::remove(entry.path());
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }
}

std::string checkFileDate(const fs::directory_entry& entry, const std::wstring& path, std::wstring rootFolder, SQLHDBC dbc)
{
    return std::string();
}


