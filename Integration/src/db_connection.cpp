#include "db_connection.h"
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

namespace fs = boost::filesystem;

Database::Database(){}

// Метод поиска конфиг файла
std::string Database::findConfigFile() {
    // Перебираем файлы в текущем каталоге
    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        if (entry.is_regular_file() && entry.path().extension() == ".conf") {
            return entry.path().string();  // Возвращаем первый попавшийся файл
        }
    }
    throw std::runtime_error("No .conf file found in the current directory");
}

// Получение корневой папки из базы
std::wstring Database::getRootFolder(SQLHDBC dbc) {
    std::wstringstream queryStream;
    queryStream << L"SELECT [value] FROM [ReconDB].[dbo].[access_settings] WHERE [name] = 'root_directory'";
    std::wstring queryStr = queryStream.str();

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate get root folder SQL statement handle");
        return L"";
    }

    ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        logError(L"Failed to execute SQL query: " + queryStr);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return L"";
    }

    std::wstring rootFolder = L"";
    if (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLWCHAR result[256]{};
        SQLLEN indicator;
        // Получаем данные из первой колонки
        SQLGetData(stmt, 1, SQL_C_WCHAR, result, sizeof(result), &indicator);
        if (indicator != SQL_NULL_DATA) {
            rootFolder = result;
        }
    }

    SQLCloseCursor(stmt);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return rootFolder;
}

SQLHDBC Database::getConnectionHandle() const{
    return dbc;
}

// Метод для получения времени цикла из базы данных
int Database::getCycleTimeFromDB(SQLHDBC dbc)
{
    std::wstringstream queryStream;
    queryStream << L"SELECT [value] FROM [ReconDB].[dbo].[access_settings] WHERE [name] = 'feeding_cycle'";
    
    int time = executeSQLAndGetIntResult(dbc, queryStream);

    return time;
}

std::wstring Database::getJsonConfigFromDatabase(std::string name, SQLHDBC dbc)
{
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle");
        return {};
    }

    const wchar_t* sqlQuery = LR"(
        SELECT [value] AS value
        FROM [ReconDB].[dbo].[access_settings]
        WHERE [name] = ?
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to prepare SQL statement");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Привязка параметра для подстановки значения переменной `name`
    wchar_t wName[256];
    size_t convertedChars = 0;
    mbstowcs_s(&convertedChars, wName, name.c_str(), _TRUNCATE);


    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, wName, 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to bind parameter");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Выполнение запроса
    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to execute SQL statement");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    wchar_t valueBuffer[1024]{}; // Используем wchar_t для хранения Unicode данных
    SQLLEN indicator = 0;

    ret = SQLBindCol(hstmt, 1, SQL_C_WCHAR, valueBuffer, sizeof(valueBuffer), &indicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to bind result column");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    std::wstring result;
    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        if (indicator != SQL_NULL_DATA) {
            result = valueBuffer; // Прямое присваивание строки
        }
    }

    if (SQL_SUCCEEDED(ret)) { 
        SQLCloseCursor(hstmt);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return result;
}

bool Database::isConnected()
{
    return dbc != nullptr;
}

// Метод подключения к БД
bool Database::connectToDatabase() {
    try {
        // Найти конфигурационный файл
        std::string configFilePath = findConfigFile();
        std::string server, database, username, password;
        readConfigFile(configFilePath, server, database, username, password);

        if (server.empty() || database.empty() || username.empty() || password.empty()) {
            logError(L"Check for missing params: server:" + stringToWString(server) +
                L"\ndatabase: " + stringToWString(database) +
                L"\nusername: " + stringToWString(username) +
                L"\npassword: " + stringToWString(password));
            return false;
        }

        // Инициализация ODBC окружения
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
        if (!SQL_SUCCEEDED(ret)) {
            return false;
        }

        ret = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            env = SQL_NULL_HENV;
            return false;
        }

        ret = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            env = SQL_NULL_HENV;
            return false;
        }

        std::string connectionString =
            "DRIVER={SQL Server};"
            "SERVER=" + server +
            ";DATABASE=" + database +
            ";UID=" + username +
            ";PWD=" + password +
            ";Encrypt=no;TrustServerCertificate=yes;";

        std::wstring connectionWString(connectionString.begin(), connectionString.end());
        SQLTCHAR* connStr = (SQLTCHAR*)connectionWString.c_str();

        SQLTCHAR outStr[1024];
        SQLSMALLINT outStrLen;

        ret = SQLDriverConnect(dbc, NULL, connStr, SQL_NTS, outStr, sizeof(outStr) / sizeof(SQLWCHAR), &outStrLen, SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret)) {
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            dbc = SQL_NULL_HDBC;
            env = SQL_NULL_HENV;
            return false;
        }

        return true; // Успешное подключение
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in connectToDatabase: ") + stringToWString(e.what()));

        if (dbc) {
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            dbc = SQL_NULL_HDBC;
        }
        if (env) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            env = SQL_NULL_HENV;
        }

        return false;
    }
}

// Метод выполнения запроса в БД
bool Database::executeSQL(SQLHDBC dbc, const std::wstringstream& sql) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    bool success = false;
    try {
        // Allocating a handle to execute an SQL query
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"Failed to allocate SQL statement handle");
            return false;
        }
        std::wstring queryStr = sql.str();
        ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            success = true;
        }
        else {
            std::wstring errorMessage = L"Error executing SQL query: " + sql.str();
            logError(errorMessage);

            // Retrieve and log the error details
            SQLWCHAR sqlState[1024];
            SQLWCHAR messageText[1024];
            SQLINTEGER nativeError;
            SQLSMALLINT textLength;

            SQLSMALLINT recNum = 1;
            while (SQLGetDiagRec(SQL_HANDLE_STMT, stmt, recNum, sqlState, &nativeError, messageText, sizeof(messageText) / sizeof(SQLWCHAR), &textLength) == SQL_SUCCESS) {
                std::wstring detailedErrorMessage = L"SQL Error [State: " + std::wstring(sqlState) + L"]: " + std::wstring(messageText) + L"\n";
                logError(detailedErrorMessage);
                recNum++;
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in executeSQL: ") + stringToWString(e.what()));
    }
    // Freeing a SQL Query Handle
    if (stmt != SQL_NULL_HSTMT) {
        SQLRETURN ret = SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"Failed to free SQL statement handle");
        }
    }
    return success;
}

bool Database::insertBinaryFileToDatabase(SQLHDBC dbc, const std::string& filePath, int structId)
{

    return false;
}

// Метод получения интового результата
int Database::executeSQLAndGetIntResult(SQLHDBC dbc, const std::wstringstream& query) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle");
        return -1;
    }

    std::wstring queryStr = query.str();
    ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        logError(L"Failed to execute SQL query: " + queryStr);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return -1;
    }

    int result = -1;
    // Получение данных после выполнения запроса
    if (SQLFetch(stmt) == SQL_SUCCESS) {
        // Чтение первого значения, которое мы ожидаем - это id вставленной записи
        SQLGetData(stmt, 1, SQL_C_SLONG, &result, sizeof(result), NULL);
    }

    SQLCloseCursor(stmt); 
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}

// Метод отключения от БД
void Database::disconnectFromDatabase() {
    try {
        if (dbc != SQL_NULL_HDBC) {
            SQLRETURN ret = SQLDisconnect(dbc);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to disconnect from database");
            }
            ret = SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to free ODBC connection handle");
            }
            else {
                dbc = SQL_NULL_HDBC;
            }
        }

        if (env != SQL_NULL_HENV) {
            SQLRETURN ret = SQLFreeHandle(SQL_HANDLE_ENV, env);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to free ODBC environment handle");
            }
            else {
                env = SQL_NULL_HENV;
            }
        }
    }
    catch (const std::exception& e) {
        logError(L"Exception caught in disconnectFromDatabase: " + stringToWString(e.what()));
    }
}

Database::~Database() {
    disconnectFromDatabase();
}


