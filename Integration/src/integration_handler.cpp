#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _CRT_SECURE_NO_WARNINGS

#ifndef SQL_SS_LENGTH_UNLIMITED
#define SQL_SS_LENGTH_UNLIMITED 0xFFFFFFFF
#endif


#include "integration_handler.h"
#include <db_connection.h>
#include "utils.h"
#include "mail_handler.h"

namespace fs = boost::filesystem;

std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

const std::wstring pathToOMPExecutable = L"C:\\Recon\\WinRec-BS\\OMP_C"; // Путь к OMP_C программе

// Метод запуска внешней программы с флагом и ожидания ее завершения
bool Integration::runExternalProgramWithFlag(const std::wstring& programPath, const std::wstring& inputFilePath) {
    try {
        // Создание консольной команды с флагом -N
        std::wstring command = L"\"" + programPath + L"\" \"" + inputFilePath + L"\" -N";

        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION pi = {};

        // Создаём процесс
        if (!CreateProcessW(NULL, &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            //logError("[Integration]: Failed to create process for command: " + wstringToUtf8(command));
            return false;
        }

        // Ожидание завершения внешней программы
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 1500); // Ждем 1.5 секунды
        if (waitResult == WAIT_TIMEOUT) {
            // Если процесс завис, принудительно закрыть его
            TerminateProcess(pi.hProcess, 1);
            //logError("[Integration]: The process has expired and was terminated with the file path specified.:" + wstringToString(inputFilePath));
            return false;
        }
        else {
            // Закрываем процесс и дескрипторы потоков
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            return true;
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in runExternalProgramWithFlag: ") + stringToWString(e.what()));
        return false;
    }
}

// Метод для проверки, является ли файл файлом данных
bool Integration::checkIsDataFile(const std::wstring& fileName) {
    return fileName.size() >= 5 && (fileName.substr(0, 5) == L"RECON" || fileName.substr(0, 5) == L"recon");
}

// Метод для проверки, является ли файл экспресс файлом
bool Integration::checkIsExpressFile(const std::wstring& fileName) {
    return fileName.size() >= 5 && (fileName.substr(0, 5) == L"REXPR" || fileName.substr(0, 5) == L"rexpr");
}

// Метод для проверки, является ли папка сортированной
bool Integration::isSortedFolder(const std::wstring& folderName) {
    try {
        if (folderName.size() == 7 && folderName[4] == L'_') {
            std::wstring year = folderName.substr(0, 4);  // ex. '2024'
            std::wstring month = folderName.substr(5, 2); // ex. '02'

            // Проверка того, что год и месяц содержат только цифры
            if (std::all_of(year.begin(), year.end(), ::isdigit) &&
                std::all_of(month.begin(), month.end(), ::isdigit)) {
                return true;
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in isSortedFolder: ") + stringToWString(e.what()));
    }
    return false;
}

// Функция для извлечения значения из строки параметров
std::wstring Integration::extractParamValue(const std::wstring& content, const std::wstring& marker) {
    std::size_t pos = content.find(marker);

    if (pos != std::wstring::npos) {
        std::size_t startPos = pos + marker.length();
        std::size_t endPos = content.find(L'$', startPos);

        if (endPos != std::wstring::npos) {
            return content.substr(startPos, endPos - startPos);
        }
        else {
            return content.substr(startPos);
        }
    }
    return L"";
}

std::string Integration::readFileContent(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        //logError(stringToWString("[Integration]: Error opening file: ") + filePath + stringToWString(" to read date and time"));
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return buffer.str();
}

// Функция для извлечения значения с помощью регулярного выражения
std::wstring Integration::extractValueWithRegex(const std::wstring& content, const std::wregex& regex) {
    std::wsmatch match;
    if (std::regex_search(content, match, regex) && match.size() > 1) {
        return match.str(1);
    }
    return L"";
}

std::wstring cleanWideContent(const std::wstring& input) {
    std::wstring result;

    for (wchar_t ch : input) {
        // Пропускаем символы переноса строки и возврата каретки
        if (ch != L'\n') {
            result += ch;
        }
    }

    // Удаляем лишние пробелы по краям и между словами
    result.erase(std::unique(result.begin(), result.end(),
        [](wchar_t a, wchar_t b) {
            return std::iswspace(a) && std::iswspace(b);
        }),
        result.end());

    return result;
}

void Integration::readDataFromFile(const std::wstring& filePath, FilePair& pair) {
    try {
        std::string fileContent = readFileContent(filePath);
        std::string utf8Content = cp866_to_utf8(fileContent);
        std::wstring wideFileContent = stringToWString(utf8Content);
        std::wstring cleanedContent = cleanWideContent(wideFileContent);

        pair.expressFile = fileContent;
        
        std::map<std::wstring, std::wregex> regexMap = {
            {L"date", std::wregex(stringToWString("Дата:\\s*(\\d{2}/\\d{2}/\\d{4})"))},
            {L"time", std::wregex(stringToWString("Время\\s+пуска:\\s*(\\d{2}:\\d{2}:\\d{2}\\.\\d{3})"))},
            {L"reconObject", std::wregex(stringToWString("Объект:\\s*(.*)"))},
            {L"factor", std::wregex(stringToWString("Фактор пуска:\\s*(.*)"))},
            {L"typeKz", std::wregex(stringToWString("Повреждение.*:\\s*(.*)"))},
            {L"damagedLine", std::wregex(stringToWString("Поврежденная линия, предположительно:\\s*(.*)"))}
        };

        pair.date = extractValueWithRegex(wideFileContent, regexMap[L"date"]);
        pair.time = extractValueWithRegex(wideFileContent, regexMap[L"time"]);
        pair.factor = extractValueWithRegex(wideFileContent, regexMap[L"factor"]);
        pair.typeKz = extractValueWithRegex(wideFileContent, regexMap[L"typeKz"]);
        pair.damagedLine = extractValueWithRegex(wideFileContent, regexMap[L"damagedLine"]);
        //pair.recon.reconNumber = std::stoi(extractParamValue(wideFileContent, L"$RN="));
        
        if (pair.date.empty() || pair.time.empty() || pair.factor.empty() || pair.damagedLine.empty()) {
            if (pair.date.empty()) {
                pair.date = extractParamValue(wideFileContent, L"$DP=");
            }
            if (pair.time.empty()) {
                pair.time = extractParamValue(wideFileContent, L"$TP=");
            }
            if (pair.factor.empty()) {
                pair.factor = extractParamValue(wideFileContent, L"$SF=");
            }
            if (pair.typeKz.empty()) {
                pair.typeKz = extractParamValue(wideFileContent, L"$LF=");
                if (pair.typeKz == L"1" || pair.typeKz == L"2" || pair.typeKz == L"3" || pair.typeKz == L"4") {
                    std::string templateStr = " фазное КЗ";
                    pair.typeKz += stringToWString(templateStr);
                }
                else {
                    pair.typeKz = L" ";
                }
            }
        }

    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in readDataFromFile: ") + pair.expressFileName + L" " + stringToWString(e.what()));
    }
}

// Сборка путей к файлам
void Integration::collectRootPaths(std::set<std::wstring>& parentFolders, const std::wstring rootFolder) {
    try {
        std::wstring fullPath;
        std::wstring currentDir;

        if (!fs::exists(rootFolder) || !fs::is_directory(rootFolder)) {
            logError(stringToWString("The folder does not exist or is inaccessible: ") + stringToWString(converter.to_bytes(rootFolder)));
            return;
        }

        for (auto it = fs::recursive_directory_iterator(rootFolder); it != fs::recursive_directory_iterator(); ++it) {
            if (fs::is_directory(*it)) {
                fullPath = it->path().wstring();
                if (fullPath != currentDir) {
                    currentDir = fullPath;
                }
            }
            else {
                std::wstring fileName = it->path().filename().wstring(); 
                std::size_t pos = it->path().wstring().find(fileName);      

                std::wstring path = it->path().parent_path().wstring();     // Получаем родительский путь

                // Добавляем проверку, чтобы игнорировать папку "Cache"
                if (path.find(L"\\Cache") != std::wstring::npos || path.find(L"/Cache") != std::wstring::npos) {
                    continue;
                }
                if (parentFolders.find(path) == parentFolders.end()) {      // Проверка на добавление путя      
                    parentFolders.insert(path);
                }
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in collectRootPaths: ") + stringToWString(e.what()));
    }
}

std::tuple<int, int, int, int> Integration::getRecordIDs(SQLHDBC dbc, const FilePair& pair) {
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle");
        return { -1, -1, -1, -1 };
    }

    const wchar_t* sqlQuery = LR"(
        SELECT 
            u.id AS unit_id,
            s.id AS struct_id,
            d.id AS data_id,
            dp.id AS dataProcess_id
        FROM [ReconDB].[dbo].[units] u
        LEFT JOIN [ReconDB].[dbo].[struct] s 
            ON s.recon_id = ? AND s.object = ?
        LEFT JOIN [ReconDB].[dbo].[data] d 
            ON d.struct_id = s.id AND d.file_num = ? AND d.time = CAST(? AS TIME(3))
        LEFT JOIN [ReconDB].[dbo].[data_process] dp
            ON dp.id = d.id
        WHERE u.unit = ? AND u.substation = ?;
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return { -1, -1, -1, -1 };
    }

    // Привязываем параметры
    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,
        const_cast<int*>(&pair.recon.reconNumber), 0, nullptr);   

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.recon.object.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_VARCHAR, 3, 0,
        (SQLWCHAR*)pair.fileNum.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 4, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 30, 0,
        (SQLWCHAR*)pair.time.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 5, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.recon.unit.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 6, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.recon.substation.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return { -1, -1, -1, -1 };
    }

    int unit_id = -1, struct_id = -1, data_id = -1, dataProcess_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &unit_id, 0, nullptr);
        SQLGetData(hstmt, 2, SQL_C_SLONG, &struct_id, 0, nullptr);
        SQLGetData(hstmt, 3, SQL_C_SLONG, &data_id, 0, nullptr);
        SQLGetData(hstmt, 4, SQL_C_SLONG, &dataProcess_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return std::make_tuple(unit_id, struct_id, data_id, dataProcess_id);
}

static void logSQLError(const std::string& message, SQLHANDLE handle, SQLSMALLINT type) {
    SQLWCHAR sqlState[6], messageText[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT textLength;

    SQLRETURN ret = SQLGetDiagRec(type, handle, 1, sqlState, &nativeError, messageText, SQL_MAX_MESSAGE_LENGTH, &textLength);
    if (SQL_SUCCEEDED(ret)) {
        std::wstring errorMessage = L"[ERROR] " + std::wstring(message.begin(), message.end()) +
            L" SQLState: " + sqlState +
            L", Error Message: " + messageText +
            L", NativeError: " + std::to_wstring(nativeError);
        //logError(std::string(errorMessage.begin(), errorMessage.end())); 
        logError(stringToWString(converter.to_bytes(errorMessage)));
    }
    else {
        logError(stringToWString(message) + L" - No additional diagnostic information available.");
    }
}

int Integration::insertIntoUnitTable(SQLHDBC dbc, const FilePair& pair)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(stringToWString("Failed to allocate SQL statement handle: insertInUnitTable"));
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        INSERT INTO [ReconDB].[dbo].[units] ([unit], [substation]) 
        OUTPUT INSERTED.id
        VALUES(?,?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1 ;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.recon.unit.c_str(), 0, nullptr);
    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.recon.substation.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int unit_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &unit_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return unit_id;
}

int Integration::insertIntoStructTable(SQLHDBC dbc, const FilePair& pair)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoStructTable");
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        INSERT INTO [ReconDB].[dbo].[struct] ([recon_id], [object], [files_path]) 
        OUTPUT INSERTED.id
        VALUES(?,?,?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,      // [recon_id]
        const_cast<int*>(&pair.recon.reconNumber), 0, nullptr);

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,    // [object]
        (SQLWCHAR*)pair.recon.object.c_str(), 0, nullptr);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,    // [files_path]
        (SQLWCHAR*)pair.parentFolderPath.c_str(), 0, nullptr);

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int struct_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &struct_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return struct_id;
}

int Integration::insertIntoDataTable(SQLHDBC dbc, const FilePair& pair, int struct_id)
{
    // Преобразование даты из формата DD.MM.YYYY в YYYY-MM-DD
    std::wstring formattedDate = pair.date.substr(6, 4) + L"-" + pair.date.substr(3, 2) + L"-" + pair.date.substr(0, 2);

    // Предполагается, что time уже в формате HH:MM:SS, поэтому просто используем его как есть
    std::wstring formattedTime = pair.time;

    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoDataTable");
        return -1;
    }

    std::wstring sqlQuery = LR"(
    INSERT INTO [ReconDB].[dbo].[data] 
    ([struct_id], [date], [time], [file_num]
    )";

    std::wstring values = LR"(
    OUTPUT INSERTED.id
    VALUES(?, ?, ?, ?)";
    std::vector<SQLLEN> bindLengths; // Список длин для параметров

    // Добавляем параметры, если файлы не пустые
    if (!pair.dataFile.empty()) {
        sqlQuery += L", [data_file]"; // Добавляем в запрос колонку
        values += L", ?"; // И добавляем параметр для этого столбца
        bindLengths.push_back(pair.dataFile.size());
    }

    if (!pair.expressFile.empty()) {
        sqlQuery += L", [express_file]"; // Добавляем в запрос колонку
        values += L", ?"; // И добавляем параметр для этого столбца
        bindLengths.push_back(pair.expressFile.size());
    }

    // Завершаем запрос
    sqlQuery += L") " + values + L");";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery.c_str(), SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }
    int paramIndex = 1;

    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,               // [struct_id]
        const_cast<int*>(&struct_id), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 1", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,                     // [date]                                     
        formattedDate.size(), 0,
        (SQLWCHAR*)formattedDate.c_str(),
        (formattedDate.size() + 1) * sizeof(wchar_t),
        nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 2", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,                     // [time]
        formattedTime.size(), 0,
        (SQLWCHAR*)formattedTime.c_str(),
        (formattedTime.size() + 1) * sizeof(wchar_t),
        nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 3", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,             // [file_num]
        (SQLWCHAR*)pair.fileNum.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 4", hstmt, SQL_HANDLE_STMT);

    SQLLEN dataSize = static_cast<SQLLEN>(pair.dataFile.size());
    SQLLEN expressSize = static_cast<SQLLEN>(pair.expressFile.size());

    if (!pair.dataFile.empty()) {
        ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY,           // [data_file] (Если есть)
            pair.dataFile.size(), 0,
            (void*)pair.dataFile.c_str(),
            pair.dataFile.size(),
            &dataSize);
        if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 5", hstmt, SQL_HANDLE_STMT);
    }

    if (!pair.expressFile.empty()) {
        ret = SQLBindParameter(hstmt, paramIndex++, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY,           // [express_file] (Если есть)
            pair.expressFile.size(), 0,
            (void*)pair.expressFile.c_str(),
            pair.expressFile.size(),
            &expressSize);
        if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 6", hstmt, SQL_HANDLE_STMT);
    }

    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute SQL query in function 'insertIntoDataTable' ", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int data_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &data_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return data_id;
}

int Integration::insertIntoProcessTable(SQLHDBC dbc, const FilePair& pair, int data_id)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle: insertIntoProcessTable");
        return -1;
    }

    const wchar_t* sqlQuery = LR"(
        SET TRANSACTION ISOLATION LEVEL READ COMMITTED;
        INSERT INTO [ReconDB].[dbo].[data_process] ([id], [damaged_line], [trigger], [event_type]) 
        OUTPUT INSERTED.id
        VALUES(?, ?, ?, ?);
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,                          
        const_cast<int*>(&data_id), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 1", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,      
        (SQLWCHAR*)pair.damagedLine.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 2", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.factor.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 3", hstmt, SQL_HANDLE_STMT);

    ret = SQLBindParameter(hstmt, 4, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0,
        (SQLWCHAR*)pair.typeKz.c_str(), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter 4", hstmt, SQL_HANDLE_STMT);

    const int maxRetries = 3;
    int attempt = 0;
    while (attempt < maxRetries) {
        ret = SQLExecute(hstmt);
        if (SQL_SUCCEEDED(ret)) break; // Успешное выполнение
        if (ret == SQL_ERROR) {
            SQLINTEGER nativeError;
            SQLCHAR sqlState[6], errorMsg[256];
            SQLSMALLINT textLength;
            SQLGetDiagRecA(SQL_HANDLE_STMT, hstmt, 1, sqlState, &nativeError, errorMsg, sizeof(errorMsg), &textLength);

            if (nativeError == 1205) { // Deadlock
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Пауза перед повтором
                attempt++;
                continue;
            }
        }
        logSQLError("Failed to execute SQL query in function 'insertIntoProcessTable", hstmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    int dataProcess_id = -1;
    ret = SQLFetch(hstmt);
    if (ret == SQL_SUCCESS) {
        SQLGetData(hstmt, 1, SQL_C_SLONG, &dataProcess_id, 0, nullptr);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return dataProcess_id;
}

// Интеграция информации в базу 
void Integration::fileIntegrationDB(SQLHDBC dbc, const FilePair& pair, std::atomic_bool& mailingIsActive) {
    try {
        // Проверяем подключение к БД
        if (dbc == nullptr) {
            logError(L"[Integration]: No connection to the database");
            return;
        }

        if (pair.date.size() < 10 )  // Date format should be like - 29/07/2024
            return;      

        // Вставка в dbo.units (если не существует)
        std::tuple<int, int, int, int> result = getRecordIDs(dbc, pair);
        int unit_id = std::get<0>(result);
        int struct_id = std::get<1>(result);
        int data_id = std::get<2>(result);
        int dataProcess_id = std::get<3>(result);

        if (unit_id == -1) {
            unit_id = insertIntoUnitTable(dbc, pair);

            if (unit_id == -1)
                return;
        }

        // Вставка в dbo.struct (если не существует)
        if (struct_id == -1) {
            struct_id = insertIntoStructTable(dbc, pair);

            if (struct_id == -1)
                return;
        }

        // Вставка в dbo.[struct_units] (если не существует)
        if (struct_id != -1) {
            // Проверяем есть ли уже такая запись
            std::wstringstream sqlCheckStructUnits;
            sqlCheckStructUnits << L"SELECT COUNT(*) FROM [ReconDB].[dbo].[struct_units] WHERE [unit_id] = " << unit_id
                << L" AND [struct_id] = " << struct_id << L";";
            int count = Database::executeSQLAndGetIntResult(dbc, sqlCheckStructUnits);
            if (count == 0) {
                // Если записи с такой комбинацией unit_id и struct_id нет, то вставляем
                std::wstringstream sqlStructUnits;
                sqlStructUnits << L"INSERT INTO [ReconDB].[dbo].[struct_units] ([unit_id], [struct_id]) VALUES ("
                    << unit_id << L", "     // [unit_id]
                    << struct_id << L");";  // [struct_id]

                if (!Database::executeSQL(dbc, sqlStructUnits)) {
                    return;
                }
            }
        }

        // Вставка в dbo.data
        if (data_id == -1) {
            data_id = insertIntoDataTable(dbc, pair, struct_id);

            // Загрузка пользователей и отправка писем
            auto users = loadUsersFromDatabase(dbc);

            auto it = users.find(wstringToString(pair.recon.substation));
            if (mailingIsActive && it != users.end()) {
                std::string configJson = wstringToUtf8(Database::getJsonConfigFromDatabase("mail", dbc));

                // Проверка на пустую строку
                if (!configJson.empty()) {
                    try {
                        // Парсинг JSON
                        auto config = parseMailServerConfig(configJson);
                        sendEmails(config, users, pair);

                    }
                    catch (const std::exception& e) {
                        logError(L"Failed to parse config JSON or send emails: " + stringToWString(e.what()));

                    }
                }
                else {
                    logError(L"Config JSON is empty. Skipping email sending.");
                }
            }

            if (data_id == -1) 
                return;
        }

        // Вставка в dbo.data_process (связанная с data)
        if (dataProcess_id == -1) {
            dataProcess_id = insertIntoProcessTable(dbc, pair, data_id);

            if (dataProcess_id == -1) 
                return;
        }
    }
     
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in fileIntegrationDB: ") + stringToWString(e.what()) + stringToWString(" With File: ") + pair.expressFileName);
    }
}

// Вспомогательная функция для объединения строк с разделителем
std::wstring Integration::join(const std::vector<std::wstring>& parts, const std::wstring& delimiter) {
    std::wstring result;
    for (size_t i = 0; i < parts.size(); ++i) {
        result += parts[i];
        if (i < parts.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}

// Обработка 
void Integration::processPath(const fs::path& fullPath, FilePair& pair, std::wstring rootFolder)
{
    // Получаем путь к родительской папке
    fs::path parentPath = fullPath.parent_path();
    std::wstring parentFolderName = parentPath.filename().wstring();

    // Проверяем, находится ли файл в сортированной папке
    if (isSortedFolder(parentFolderName)) {
        pair.inSortedFolder = true;
        parentPath = parentPath.parent_path(); // Обрезаем на 1 уровень вверх
    }
    pair.parentFolderPath = parentPath.wstring();
    // Относительный путь от rootFolder до файла
    fs::path relativePath = fs::relative(parentPath, rootFolder);
    std::vector<std::wstring> pathParts;

    for (const auto& part : relativePath) {
        pathParts.push_back(part.wstring());
    }

    Recon recon;
    if (pathParts.size() >= 3) {
        recon.object = pathParts.back();        // Последняя папка — object
        pathParts.pop_back();                   // Удаляем последний элемент (object)

        recon.substation = pathParts.back();    // Предпоследняя — substation
        pathParts.pop_back();                   // Удаляем предпоследний элемент (substation)

        // Остальное объединяем в unit
        recon.unit = join(pathParts, L" - ");
    }
    int reconNum;
    if (fullPath.filename().wstring().size() >= 8) {
        reconNum  = std::stoi(fullPath.filename().wstring().substr(5, 3));
    }
    
    recon.reconNumber = reconNum;
    pair.recon = recon;
}

// Метод по сбору информации о файлах
void Integration::collectInfo(FilePair& pair, const fs::directory_entry& entry, const std::wstring& path, std::wstring rootFolder, SQLHDBC dbc) {
    try {
        try {
            try {
                std::wstring fileName = entry.path().filename().wstring();              // Имя файла ex. (RECON167.759)
                if (fileName.size() < 12) { return; }                                   // 12 потому что длина имени  == 12 (RECON167.759)
                std::wstring baseName = fileName.substr(5);                             // Номер рекона и номер файла ex. (167.759)     
                std::wstring pathToFile = entry.path().parent_path().wstring() + L"\\"; // Путь к файлу
                std::wstring fullPath = entry.path().wstring();                         // Полный путь включая файл
                std::wstring reconNum = fileName.substr(5, 3);                          // Номер рекона 
                std::wstring fileNum = fileName.substr(9, 3);                           // Номер файла

                // TODO: Сделать правильное чтение файлов дата учитывая правильный путь
                if (checkIsDataFile(fileName)) {
                    pair.dataFileName = fileName;
                    pair.hasDataFile = true;

                    std::wstring dataFilePath = pathToFile + fileName;
                    pair.dataFile = readFileContent(dataFilePath);

                    std::wstring expressFileName = L"REXPR" + baseName;
                    std::wstring expressFilePath = pathToFile + expressFileName;

                    // Если экспресс файл не был найден, запускаем програму ОМР-С
                    if (!fs::exists(expressFilePath)) {
                        bool containsLetters = std::any_of(baseName.begin(), baseName.end(), [](wchar_t c) {
                            return std::iswalpha(c);
                        });

                        if (!containsLetters) {
                            if (!runExternalProgramWithFlag(pathToOMPExecutable, fullPath)) {
                                return;
                            }
                        }
                        else { return; }
                    }

                    pair.expressFileName = expressFileName;
                    pair.hasExpressFile = true;
                    readDataFromFile(expressFilePath, pair);
                }
                else if (checkIsExpressFile(fileName)) {
                    pair.expressFileName = fileName;
                    pair.hasExpressFile = true;

                    std::wstring expressFilePath = pathToFile + fileName;
                    readDataFromFile(expressFilePath, pair);

                    std::wstring dataFileName = L"RECON" + baseName;
                    std::wstring dataFilePath = pathToFile + dataFileName;
                    if (!fs::exists(dataFilePath)) {
                        pair.hasDataFile = false;
                        pair.dataFileName = L"-";
                    }
                    else {
                        pair.dataFileName = dataFileName;
                        pair.dataFile = readFileContent(dataFilePath);
                        pair.hasDataFile = true;
                    }
                }
                else { return; }

                pair.fileNum = fileNum;
                processPath(fullPath, pair, rootFolder);
                pair.recon.reconNumber = std::stoi(reconNum);
            }
            catch (const std::exception& e) {
                logError(stringToWString("Exception caught while processing file entry: ") + stringToWString(e.what()));
            }
        }
        catch (const std::exception& e) {
            logError(stringToWString("Exception caught while iterating directory: ") + stringToWString(e.what()));
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in collectFiles: ") + stringToWString(e.what()));
    }
}

// Метод сортировки файлов по папкам по дате
void Integration::sortPair(FilePair& pair) {
    try {
        try {
            if (pair.date.size() < 6){ return; }

            std::wstring year = pair.date.substr(6, 4);      // ex. 2024
            std::wstring month = pair.date.substr(3, 2);     // ex. 11
            std::wstring newFolder = pair.parentFolderPath;

            if (!pair.inSortedFolder) {
                newFolder = pair.parentFolderPath + L"\\" + year + L"_" + month;
                if (!fs::exists(newFolder)) {
                    fs::create_directory(newFolder);
                }
            }
            else { return; }

            // Перемещение express-файлов в сортированную папку
            if (pair.hasExpressFile) {
                std::wstring expressPath = pair.parentFolderPath + L"\\" + pair.expressFileName;
                std::wstring newExpressPath = newFolder + L"\\" + pair.expressFileName;
                if (fs::exists(expressPath)) {
                    if (fs::exists(newExpressPath)) {
                        fs::remove(expressPath);
                    }
                    else {
                        fs::copy(expressPath, newExpressPath, fs::copy_options::overwrite_existing);
                        fs::remove(expressPath);
                    }
                }
                else {
                    logError(L"[Integration]: Express file not found: " + expressPath);
                }
            }
            // Перемещение data-файлов в сортированную папку
            if (pair.hasDataFile) {
                std::wstring dataPath = pair.parentFolderPath + L"\\" + pair.dataFileName;
                std::wstring newDataPath = newFolder + L"\\" + pair.dataFileName;
                if (fs::exists(dataPath)) {
                    if (fs::exists(newDataPath)) {
                        fs::remove(dataPath);
                    }
                    else {
                        fs::copy(dataPath, newDataPath, fs::copy_options::overwrite_existing);
                        fs::remove(dataPath);
                    }
                }
                else {
                    logError(L"[Integration]: Data file not found: " + dataPath);
                    return;
                }
            }
            pair.parentFolderPath = newFolder;
        }
        catch (const std::exception& e) {
            logError(stringToWString("Exception caught while processing file pair: ") + stringToWString(e.what()));
            return;
        }

    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in sortFiles: ") + stringToWString(e.what()));
        return;
    }
}

std::wstring Integration::getPathByRNumber(int recon_id,SQLHDBC dbc)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

    const wchar_t* query = LR"(SELECT d.local_path
        FROM[ReconDB].[dbo].[struct] s 
        JOIN[ReconDB].[dbo].[FTP_Directories] d ON d.struct_id = s.id 
        WHERE s.recon_id = ?)";

    ret = SQLPrepareW(stmt, (SQLWCHAR*)query, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::wstring();
    }

    ret = SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 255, 0,
        const_cast<int*>(&recon_id), 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) logSQLError("Failed to bind parameter recon_id", stmt, SQL_HANDLE_STMT);

    ret = SQLExecute(stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logSQLError("Failed to execute SQL query in function 'getPathByRNumber' ", stmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return std::wstring();
    }

    std::wstring localPath;
    wchar_t pathBuffer[512];
    SQLLEN pathLen = 0;

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLGetData(stmt, 1, SQL_C_WCHAR, pathBuffer, sizeof(pathBuffer), &pathLen);
        if (pathLen != SQL_NULL_DATA) {
            localPath = pathBuffer;  // Всегда перезаписываем, беря последнюю запись
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return localPath;
}




