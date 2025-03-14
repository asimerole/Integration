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

// Размер и имя файла ошибок 
const std::string LOG_FILE = "Errors.txt";
const size_t MAX_LOG_SIZE = 150 * 1024;  // Максимальный размер в байтах (150 KB)
const size_t MAX_LOG_LINES = 500;       // Максимальное количество строк в логе


// Размер ключа и IV
constexpr int KEY_SIZE = 32; // 256-битный ключ
constexpr int IV_SIZE = 16;  // 128-битный IV

// Фиксированные значения ключа и IV (например, генерировать их вручную и хранить)
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

void trimLogFile() {
    std::ifstream file(LOG_FILE);
    if (!file.is_open()) return;

    std::deque<std::string> lines;  // Используем deque для экономии памяти
    std::string line;

    // Читаем строки и храним только последние MAX_LOG_LINES
    while (std::getline(file, line)) {
        if (lines.size() >= MAX_LOG_LINES) {
            lines.pop_front();  // Удаляем старую строку, чтобы не переполнять память
        }
        lines.push_back(line);
    }
    file.close();  // Закрываем перед открытием на запись

    // Перезаписываем файл только если он действительно стал больше лимита
    if (lines.size() == MAX_LOG_LINES) {
        std::ofstream outFile(LOG_FILE, std::ios::trunc);
        if (!outFile.is_open()) return;

        for (const auto& l : lines) {
            outFile << l << '\n';
        }
    }
}

void logError(const std::wstring& message) {
    // Проверяем размер файла
    std::ifstream file(LOG_FILE, std::ios::ate | std::ios::binary);
    if (file.is_open() && file.tellg() > MAX_LOG_SIZE) {
        file.close();
        trimLogFile();  // Если файл слишком большой, подчищаем
    }

    std::wofstream logFile(LOG_FILE, std::ios::app);
    if (!logFile.is_open()) return;

    try {
        logFile.imbue(std::locale("C.UTF-8"));
    }
    catch (const std::exception& e) {
        std::wcerr << L"Ошибка установки локали: " << utf8_to_wstring(e.what()) << L"\n";
    }

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    logFile << std::put_time(std::localtime(&now_time), L"%Y-%m-%d %H:%M:%S")
        << L" - " << message << std::endl;

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
        logError(L"Ошибка конвертации строки: " + std::to_wstring(GetLastError()));
        return L"";
    }

    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);

    return wstr;
}

//std::wstring stringToWString(const std::string& str) {
//    try {
//        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
//        return converter.from_bytes(str);
//    }
//    catch (const std::range_error& e) {
//        logError(L"Range error caught in stringToWString: " + stringToWString(e.what()));
//        logError(stringToWString(str));
//        throw;
//    }
//    catch (const std::exception& e) {
//        logError(L"General exception caught in stringToWString: " + stringToWString(e.what()));
//        throw;
//    }
//}

std::string wstringToString(const std::wstring& wstr)
{
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(wstr);
    }
    catch (const std::range_error& e) {
        logError(L"Range error caught in wstringToString: bad conversion" + stringToWString(e.what()));
        throw;
    }
    catch (const std::exception& e) {
        logError(L"General exception caught in wstringToString: " + stringToWString(e.what()));
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

    // Установить флаг для отключения переноса строк
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    // Записать данные
    BIO_write(bio, input.data(), static_cast<int>(input.size()));
    BIO_flush(bio);

    // Получить закодированные данные
    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string encodedData(bufferPtr->data, bufferPtr->length);

    // Освободить ресурсы
    BIO_free_all(bio);

    return encodedData;
}

// Чтобы избежать проблем с одинарными кавычками в SQL-запросе
std::wstring escapeSingleQuotes(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    for (wchar_t ch : input) {
        if (ch == L'\'') {
            result += L"''"; // Замена одинарных кавычек на двойные
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

// Расшифровка данных из конфиг файла
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

    // Преобразуем расшифрованные данные в строку
    std::string decryptedString(decryptedData.begin(), decryptedData.end());

    // Разбираем конфигурацию построчно
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
        logError(L"Failed to parse file date: " + fileDate);
        return false; 
    }

    std::time_t now = std::time(nullptr);
    std::tm currentTm = *std::localtime(&now);

    std::time_t fileTime = std::mktime(&fileTm);
    std::time_t currentTime = std::mktime(&currentTm);

    double difference = std::difftime(currentTime, fileTime) / (60 * 60 * 24);
    return difference <= days;
}

