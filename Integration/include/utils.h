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
#include <openssl/sha.h>
#include <string>
#include <sstream>
#include <iomanip>

// Names for log files
const std::string LOG_PATH = "Errors.txt";
const std::string FTP_LOG_PATH = "ErrorsFTP.txt";
const std::string INTEGRATION_LOG_PATH = "ErrorsIntegration.txt";
const std::string EMAIL_LOG_PATH = "ErrorsEmail.txt";
const std::string ONEDRIVE_LOG_PATH = "ErrorsOneDrive.txt";
const std::string EXCEPTION_LOG_PATH = "Exceptions.txt";

// TODO: meybe should some with this log functions doing
void trimLogFile(const std::string& filePath);

// Method to log messages to a file
void logError(const std::wstring& message, const std::string& filePath);

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

// Base64 decoding according to MIME header format.
std::vector<uint8_t> base64Decode(const std::string& input);

// Method for skipping single parentheses in parameters from a file
std::wstring escapeSingleQuotes(const std::wstring& input);	

// Displays a dialog box with a Yes/No button.
bool showConfirmationDialog(const std::wstring& message, const std::wstring& title); 

// Decodes a Base64 encoded string.
std::vector<std::uint8_t> decryptData(const std::string& configPath, const unsigned char* key, const unsigned char* iv);

// Reads a configuration file and extracts the database connection parameters.
bool readConfigFile(std::string& configPath, std::string& serverName, std::string& databaseName, std::string& username, std::string& password, std::string& port);

// Gets the current date in the format "YYYY-MM-DD".
std::wstring getCurrentDate();

// Parses a date string in the format "YYYY-MM-DD" and fills a std::tm structure.
bool parseDate(const std::wstring& dateString, std::tm& tm);

// Check if a file date is within the last N days.
bool isWithinLastNDays(const std::wstring& fileDate, int days);

std::vector<uint8_t> encryptData(const std::vector<uint8_t>& data, const unsigned char* key, const unsigned char* iv);

std::vector<uint8_t> decryptDataFromMemory(const std::vector<uint8_t>& encryptedData, const unsigned char* key, const unsigned char* iv);

void saveCredentailsToHash(std::wstring &login, std::wstring &password);

void deleteCredentialsFromRegistry();

void loadCredentials(std::wstring& login, std::wstring& password);

std::wstring getFileDate(std::wstring fullPath);


#endif // UTILS_H