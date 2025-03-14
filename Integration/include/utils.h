#ifndef UTILS_H
#define UTILS_H

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

// Includes
#include <iostream>
#include <Windows.h>
#include <shellapi.h>
#include <thread>
#include <vector>
#include <set>
#include <deque>
#include <map>
#include <algorithm>
#include <atomic>
#include <string>
#include <sstream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <sqlext.h>
#include <sqltypes.h>
#include <sql.h>
#include <codecvt>
#include <locale>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <unordered_set>
#include <mutex>
#include <iconv.h>
#include <regex>
#include <cwctype>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>

void trimLogFile();
void logError(const std::wstring& message);					// Метод для регистрации сообщений об ошибках
std::wstring utf8_to_wstring(const std::string& str);		// Метод для конвертации строки "utf8" в формат "wstring"
std::string wstringToUtf8(const std::wstring& wstr);		// Метод для конвертации строки "wstring" в формат "utf8"
std::wstring stringToWString(const std::string& str);		// Метод для конвертации строки "string" в формат "wstring"
std::string wstringToString(const std::wstring& wstr);		// Метод для конвертации строки "wstring" в формат "string"
std::string cp866_to_utf8(const std::string& cp866_str);	// Метод для конвертации из кодировк "cp866" в "utf8"
std::string base64Encode(const std::string& input);			// Кодировка Base64 в соответствии с MIME-форматом заголовков.
std::wstring escapeSingleQuotes(const std::wstring& input);	// Метод пропуска одинарных скобок в параметрах из файла
bool showConfirmationDialog(const std::wstring& message, const std::wstring& title); // Выводит диалоговое окно с кнопкай Да/Нет
std::vector<std::uint8_t> decryptData(const std::string& configPath, const unsigned char* key, const unsigned char* iv);
bool readConfigFile(std::string& configPath, std::string& serverName, std::string& databaseName, std::string& username, std::string& password);
std::wstring getCurrentDate();
bool parseDate(const std::wstring& dateString, std::tm& tm);
bool parseMetaFile();
bool isWithinLastNDays(const std::wstring& fileDate, int days);


#endif // UTILS_H