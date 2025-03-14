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

const std::wstring pathToOMPExecutable = L"C:\\Recon\\WinRec-BS\\OMP_C"; // ���� � OMP_C ���������

// ������� json ������ ������� �� ����
OneDriveConfig parseOneDriveConfig(const std::string& jsonString);	

// ������� ���� �� ����� 
std::tm parseDate(const std::wstring& dateStr);

// ���������� ������� ����� ����������� ����� � ����� �����
int monthsBetween(const std::tm& fromDate, const std::tm& toDate);

// �������� ������ ������
void deleteOldFiles(const std::wstring& path, int maxMonths, std::wstring dateStr);

// �������� ���� �������� ����� 
std::string checkFileDate(const fs::directory_entry& entry, const std::wstring& path, std::wstring rootFolder, SQLHDBC dbc);