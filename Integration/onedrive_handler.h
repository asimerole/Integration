#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <chrono>
#include "utils.h"
#include "db_connection.h" 
#include "ftp_handler.h"

struct OneDriveConfig {
	int monthCount;
	std::wstring oneDrivePath;
};

const std::wstring pathToOMPExecutable = L"C:\\Recon\\WinRec-BS\\OMP_C"; // Путь к OMP_C программе

// Парсинг json строки конфига из базы
OneDriveConfig parseOneDriveConfig(const std::string& jsonString);	

// Парсинг даты из файла 
std::tm parseDate(const std::wstring& dateStr);

// Количество месяцев между сегодняшней датой и датой файла
int monthsBetween(const std::tm& fromDate, const std::tm& toDate);

// Удаление старых файлов
void deleteOldFiles(const std::wstring& path, int maxMonths, std::wstring dateStr);

// Проверка даты создания файла 
std::string checkFileDate(const fs::directory_entry& entry, const std::wstring& path, std::wstring rootFolder, SQLHDBC dbc);