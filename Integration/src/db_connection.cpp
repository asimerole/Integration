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

// Method of searching for config file
std::string Database::findConfigFile() {
    // We go through the files in the current directory
    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        if (entry.is_regular_file() && entry.path().extension() == ".conf") {
            return entry.path().string();  // We return the first file we come across
        }
    }
    throw std::runtime_error("No .conf file found in the current directory");
}

// Getting the root folder from the database
std::wstring Database::getPathFromDbByName(SQLHDBC dbc, std::wstring name) {
    std::wstringstream queryStream;
    queryStream << L"SELECT [value] FROM [ReconDB].[dbo].[access_settings] WHERE [name] = '" + name  + L"'";
    std::wstring queryStr = queryStream.str();

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate get root folder SQL statement handle", LOG_PATH);
        return L"";
    }

    ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        logError(L"Failed to execute SQL query: " + queryStr, LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return L"";
    }

    std::wstring rootFolder = L"";
    if (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLWCHAR result[256]{};
        SQLLEN indicator;
        // We get data from the first column
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

// Method to get cycle time from database
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
        logError(L"Failed to allocate SQL statement handle", LOG_PATH);
        return {};
    }

    const wchar_t* sqlQuery = LR"(
        SELECT [value] AS value
        FROM [ReconDB].[dbo].[access_settings]
        WHERE [name] = ?
    )";

    ret = SQLPrepareW(hstmt, (SQLWCHAR*)sqlQuery, SQL_NTS);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to prepare SQL statement", LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Binding a parameter to substitute the value of the `name` variable
    wchar_t wName[256];
    size_t convertedChars = 0;
    mbstowcs_s(&convertedChars, wName, name.c_str(), _TRUNCATE);


    ret = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, wName, 0, nullptr);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to bind parameter", LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    // Executing a request
    ret = SQLExecute(hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to execute SQL statement", LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    wchar_t valueBuffer[1024]{}; // Using wchar_t to store Unicode data
    SQLLEN indicator = 0;

    ret = SQLBindCol(hstmt, 1, SQL_C_WCHAR, valueBuffer, sizeof(valueBuffer), &indicator);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to bind result column", LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return {};
    }

    std::wstring result;
    if (SQLFetch(hstmt) == SQL_SUCCESS) {
        if (indicator != SQL_NULL_DATA) {
            result = valueBuffer; // Direct string assignment
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

// Method of connection to the database
bool Database::connectToDatabase() {
    try {
        // Find configuration file
        std::string configFilePath = findConfigFile();
        std::string server, database, username, password;
        readConfigFile(configFilePath, server, database, username, password);

        if (server.empty() || database.empty() || username.empty() || password.empty()) {
            logError(L"Check for missing params: server:" + stringToWString(server) +
                L"\ndatabase: " + stringToWString(database) +
                L"\nusername: " + stringToWString(username) +
                L"\npassword: " + stringToWString(password), LOG_PATH);
            return false;
        }

        // Initializing the ODBC environment
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

        return true; // Successful connection
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in connectToDatabase: ") + stringToWString(e.what()), LOG_PATH);

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

// Method for executing a query in a database
bool Database::executeSQL(SQLHDBC dbc, const std::wstringstream& sql) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    bool success = false;
    try {
        // Allocating a handle to execute an SQL query
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"Failed to allocate SQL statement handle", LOG_PATH);
            return false;
        }
        std::wstring queryStr = sql.str();
        ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            success = true;
        }
        else {
            std::wstring errorMessage = L"Error executing SQL query: " + sql.str();
            logError(errorMessage, LOG_PATH);

            // Retrieve and log the error details
            SQLWCHAR sqlState[1024];
            SQLWCHAR messageText[1024];
            SQLINTEGER nativeError;
            SQLSMALLINT textLength;

            SQLSMALLINT recNum = 1;
            while (SQLGetDiagRec(SQL_HANDLE_STMT, stmt, recNum, sqlState, &nativeError, messageText, sizeof(messageText) / sizeof(SQLWCHAR), &textLength) == SQL_SUCCESS) {
                std::wstring detailedErrorMessage = L"SQL Error [State: " + std::wstring(sqlState) + L"]: " + std::wstring(messageText) + L"\n";
                logError(detailedErrorMessage, LOG_PATH);
                recNum++;
            }
        }
    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in executeSQL: ") + stringToWString(e.what()), LOG_PATH);
    }
    // Freeing a SQL Query Handle
    if (stmt != SQL_NULL_HSTMT) {
        SQLRETURN ret = SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        if (!SQL_SUCCEEDED(ret)) {
            logError(L"Failed to free SQL statement handle", LOG_PATH);
        }
    }
    return success;
}

bool Database::insertBinaryFileToDatabase(SQLHDBC dbc, const std::string& filePath, int structId)
{

    return false;
}

// Method of obtaining an integral result
int Database::executeSQLAndGetIntResult(SQLHDBC dbc, const std::wstringstream& query) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(ret)) {
        logError(L"Failed to allocate SQL statement handle", LOG_PATH);
        return -1;
    }

    std::wstring queryStr = query.str();
    ret = SQLExecDirect(stmt, (SQLWCHAR*)queryStr.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        logError(L"Failed to execute SQL query: " + queryStr, LOG_PATH);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return -1;
    }

    int result = -1;
    // Getting data after executing a query
    if (SQLFetch(stmt) == SQL_SUCCESS) {
        // Reading the first value we expect is the id of the inserted record
        SQLGetData(stmt, 1, SQL_C_SLONG, &result, sizeof(result), NULL);
    }

    SQLCloseCursor(stmt); 
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return result;
}

// Method of disconnecting from the database
void Database::disconnectFromDatabase() {
    try {
        if (dbc != SQL_NULL_HDBC) {
            SQLRETURN ret = SQLDisconnect(dbc);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to disconnect from database", LOG_PATH);
            }
            ret = SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to free ODBC connection handle", LOG_PATH);
            }
            else {
                dbc = SQL_NULL_HDBC;
            }
        }

        if (env != SQL_NULL_HENV) {
            SQLRETURN ret = SQLFreeHandle(SQL_HANDLE_ENV, env);
            if (!SQL_SUCCEEDED(ret)) {
                logError(L"Failed to free ODBC environment handle", LOG_PATH);
            }
            else {
                env = SQL_NULL_HENV;
            }
        }
    }
    catch (const std::exception& e) {
        logError(L"Exception caught in disconnectFromDatabase: " + stringToWString(e.what()), LOG_PATH);
    }
}

Database::~Database() {
    disconnectFromDatabase();
}


