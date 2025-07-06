#include "utils.h"

#ifdef UNICODE
#define SQLTCHAR SQLWCHAR
#define SQLTSTR SQLWCHAR
#define SQLTEXT(str) L##str
#else
#define SQLTCHAR SQLCHAR
#define SQLTSTR SQLCHAR
#define SQLTEXT(str) str
#endif

const size_t MAX_LOG_SIZE = 150 * 1024;  // Maximum size in bytes (150 KB)
const size_t MAX_LOG_LINES = 500;       // Maximum number of lines in the log

namespace fs = boost::filesystem;

// Key size and IV
constexpr int KEY_SIZE = 32; // 256-bit key
constexpr int IV_SIZE = 16;  // 128-bit IV

// Fixed key and IV values ​​(eg generate them manually and store them)
unsigned char key[KEY_SIZE] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

unsigned char iv[IV_SIZE] = {
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30
};

void trimLogFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::deque<std::string> lines;  // Using deque to save memory
    std::string line;

    // Read lines and store only the last MAX_LOG_LINES
    while (std::getline(file, line)) {
        if (lines.size() >= MAX_LOG_LINES) {
            lines.pop_front();  // We delete the old line to avoid memory overflow
        }
        lines.push_back(line);
    }
    file.close();  // Close before opening for recording

    // We only overwrite the file if it actually becomes larger than the limit.
    if (lines.size() == MAX_LOG_LINES) {
        std::ofstream outFile(filePath, std::ios::trunc);
        if (!outFile.is_open()) return;

        for (const auto& l : lines) {
            outFile << l << '\n';
        }
    }
}

void logError(const std::wstring& message, const std::string& filePathUtf8) {
    if (filePathUtf8.empty()) return;

    std::wstring widePath = stringToWString(filePathUtf8);

    try {
        std::ifstream file(widePath, std::ios::ate | std::ios::binary);
        if (file.is_open() && file.good()) {
            std::uintmax_t fileSize = fs::file_size(widePath);
            if (fileSize != -1 && static_cast<size_t>(fileSize) > MAX_LOG_SIZE) {
                file.close();
                trimLogFile(filePathUtf8); 
            }
        }
    }
    catch (...) {
        return;
    }

    std::wofstream logFile(widePath, std::ios::app);
    if (!logFile.is_open()) return;

    try {
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm{};
        localtime_s(&local_tm, &now_time);

        std::wstringstream timeStream;
        timeStream << std::put_time(&local_tm, L"%Y-%m-%d %H:%M:%S")
            << L"." << std::setw(3) << std::setfill(L'0') << millis.count();

        logFile << timeStream.str() << L" - " << message << std::endl;
    }
    catch (...) {
        return;
    }

    logFile.close();
}


std::wstring utf8_to_wstring(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(str);
}

std::string wstringToUtf8(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

std::wstring stringToWString(const std::string& str) {
    if (str.empty()) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (size_needed <= 0) {
        logError(L"Error converting string: " + std::to_wstring(GetLastError()), LOG_PATH);
        return L"";
    }

    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);

    return wstr;
}

std::string wstringToString(const std::wstring& wstr)
{
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(wstr);
    }
    catch (const std::range_error& e) {
        logError(L"Range error caught in wstringToString: bad conversion" + stringToWString(e.what()), LOG_PATH);
        throw;
    }
    catch (const std::exception& e) {
        logError(L"General exception caught in wstringToString: " + stringToWString(e.what()), LOG_PATH);
        throw;
    }
}

std::string cp866_to_utf8(const std::string& cp866_str) {
    iconv_t conv = iconv_open("UTF-8", "CP866");
    if (conv == (iconv_t)-1) {
        return "";
    }
    size_t inBytesLeft = cp866_str.size();
    size_t outBytesLeft = inBytesLeft * 2; // UTF-8 can take up to 2 bytes for every CP866 byte
    std::vector<char> outBuf(outBytesLeft);
    char* inBuf = const_cast<char*>(cp866_str.data());
    char* outPtr = outBuf.data();
    if (iconv(conv, &inBuf, &inBytesLeft, &outPtr, &outBytesLeft) == (size_t)-1) {
        iconv_close(conv);
        return "";
    }
    iconv_close(conv);
    return std::string(outBuf.data(), outBuf.size() - outBytesLeft);
}

std::string base64Encode(const std::string& input)
{
    BIO* bio = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    bio = BIO_push(bio, mem);

    // Set flag to disable line wrapping
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    // Write data
    BIO_write(bio, input.data(), static_cast<int>(input.size()));
    BIO_flush(bio);

    // Get encrypted data
    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string encodedData(bufferPtr->data, bufferPtr->length);

    // Free up resources
    BIO_free_all(bio);

    return encodedData;
}

// To avoid problems with single quotes in SQL query
std::wstring escapeSingleQuotes(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    for (wchar_t ch : input) {
        if (ch == L'\'') {
            result += L"''"; 
        }
        else {
            result += ch;
        }
    }
    return result;
}

bool showConfirmationDialog(const std::wstring& message, const std::wstring& title)
{
    int result = MessageBoxW(NULL, message.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
    return (result == IDYES);
}

// Decrypting data from config file
std::vector<std::uint8_t> decryptData(const std::string& configPath, const unsigned char* key, const unsigned char* iv)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + configPath);
        return {};
    }

    std::vector<unsigned char> ivBuffer(IV_SIZE);
    configFile.read(reinterpret_cast<char*>(ivBuffer.data()), IV_SIZE);
    if (configFile.gcount() != IV_SIZE) {
        return {}; 
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, ivBuffer.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {}; 
    }

    std::vector<unsigned char> decryptedData;
    std::vector<unsigned char> buffer(1024);
    std::vector<unsigned char> decryptedBuffer(1024 + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int outLen = 0;

    while (configFile.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || configFile.gcount() > 0) {
        size_t bytesRead = configFile.gcount();
        decryptedBuffer.resize(bytesRead + EVP_CIPHER_block_size(EVP_aes_256_cbc()));

        if (EVP_DecryptUpdate(ctx, decryptedBuffer.data(), &outLen, buffer.data(), static_cast<int>(bytesRead)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return {};
        }

        decryptedData.insert(decryptedData.end(), decryptedBuffer.begin(), decryptedBuffer.begin() + outLen);
    }

    if (EVP_DecryptFinal_ex(ctx, decryptedBuffer.data(), &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    decryptedData.insert(decryptedData.end(), decryptedBuffer.begin(), decryptedBuffer.begin() + outLen);

    EVP_CIPHER_CTX_free(ctx);
    configFile.close();
    return decryptedData;
}

bool readConfigFile(std::string& configPath, std::string& serverName, std::string& databaseName, std::string& username, std::string& password)
{
    std::vector<unsigned char> decryptedData = decryptData(configPath, key, iv);
    if (decryptedData.empty()) {
        return false; 
    }

    // Convert the decrypted data into a string
    std::string decryptedString(decryptedData.begin(), decryptedData.end());
    // Let's analyze the configuration line by line
    std::istringstream in(decryptedString);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("server=", 0) == 0) {
            serverName = line.substr(7);
        }
        else if (line.rfind("database=", 0) == 0) {
            databaseName = line.substr(9);
        }
        else if (line.rfind("username=", 0) == 0) {
            username = line.substr(9);
        }
        else if (line.rfind("password=", 0) == 0) {
            password = line.substr(9);
        }
    }

    return true;
}

std::wstring getCurrentDate()
{
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);

    std::wstringstream dateStream;
    dateStream << std::put_time(&tm, L"%d%m%Y");
    return dateStream.str();
}

bool parseDate(const std::wstring& dateString, std::tm& tm) {
    std::wistringstream dateStream(dateString);
    dateStream >> std::get_time(&tm, L"%d/%m/%Y");
    return !dateStream.fail();
}

bool isWithinLastNDays(const std::wstring& fileDate, int days)
{
    std::tm fileTm = {};

    if (!parseDate(fileDate, fileTm)) {
        logError(L"Failed to parse file date: " + fileDate, LOG_PATH);
        return false; 
    }

    std::time_t now = std::time(nullptr);
    std::tm currentTm = *std::localtime(&now);

    std::time_t fileTime = std::mktime(&fileTm);
    std::time_t currentTime = std::mktime(&currentTm);

    double difference = std::difftime(currentTime, fileTime) / (60 * 60 * 24);
    return difference <= days;
}

