#ifndef DBCONNECTION_H
#define DBCONNECTION_H

#ifdef UNICODE
#define SQLTCHAR SQLWCHAR
#define SQLTSTR SQLWCHAR
#define SQLTEXT(str) L##str
#else
#define SQLTCHAR SQLCHAR
#define SQLTSTR SQLCHAR
#define SQLTEXT(str) str
#endif

// Includes
#include <Windows.h>
#include <iostream>
#include <shellapi.h>
#include <set>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <sqlext.h>
#include <sqltypes.h>
#include <sql.h>

class Database {
public:
	Database();
	~Database();

	// Methods for work with Database
	// Подключение к базе
	void connectToDatabase();											

	// Выполнение SQL запроса 
	static bool executeSQL(SQLHDBC dbc, const std::wstringstream& sql);	

	// Вставка бинарного файла
	static bool insertBinaryFileToDatabase(SQLHDBC dbc, const std::string& filePath, int structId);

	// Вставка данных с возвратом его айди 
	static int executeSQLAndGetIntResult(SQLHDBC dbc, const std::wstringstream& query);

	// Получение корнево папки
	static std::wstring getRootFolder(SQLHDBC dbc);

	// Получение дескриптора соединения 
	SQLHDBC getConnectionHandle() const;

	// Получение времени на запросы по фтп 
	static int getCycleTimeFromDB(SQLHDBC dbc);

	// Получение строки конфига в Json формате 
	static std::wstring getJsonConfigFromDatabase(std::string name, SQLHDBC dbc);

	// Проверка подключения к базе 
	bool isConnected();

	// Отключение базы
	void disconnectFromDatabase();

private:
	SQLHENV env = SQL_NULL_HENV;
	SQLHDBC dbc = SQL_NULL_HDBC;

	// Поиск конфиг файла в каталоге програмы
	std::string findConfigFile();
};


#endif // DBCONNECTION_H