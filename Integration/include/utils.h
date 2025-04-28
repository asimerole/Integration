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
// Method for logging error messages
void logError(const std::wstring& message);				

// Method to convert "utf8" string to "wstring" format
std::wstring utf8_to_wstring(const std::string& str);	

// Method to convert "wstring" string to "utf8" format
std::string wstringToUtf8(const std::wstring& wstr);

// Method to convert "string" string to "wstring" format
std::wstring stringToWString(const std::string& str);	

// Method to convert "wstring" string to "string" format
std::string wstringToString(const std::wstring& wstr);	

// Method to convert from "cp866" to "utf8" encoding format
std::string cp866_to_utf8(const std::string& cp866_str);	

// Base64 encoding according to MIME header format.
std::string base64Encode(const std::string& input);

// Method for skipping single parentheses in parameters from a file
std::wstring escapeSingleQuotes(const std::wstring& input);	

// Displays a dialog box with a Yes/No button.
bool showConfirmationDialog(const std::wstring& message, const std::wstring& title); 

std::vector<std::uint8_t> decryptData(const std::string& configPath, const unsigned char* key, const unsigned char* iv);

bool readConfigFile(std::string& configPath, std::string& serverName, std::string& databaseName, std::string& username, std::string& password);

std::wstring getCurrentDate();

bool parseDate(const std::wstring& dateString, std::tm& tm);

bool isWithinLastNDays(const std::wstring& fileDate, int days);


#endif // UTILS_H