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
	// Connecting to Database
	bool connectToDatabase();	

	// Executing SQL query
	static bool executeSQL(SQLHDBC dbc, const std::wstringstream& sql);	

	// Inserting data with return of its ID
	static int executeSQLAndGetIntResult(SQLHDBC dbc, const std::wstringstream& query);

	// Getting the root folder
	static std::wstring getPathFromDbByName(SQLHDBC dbc, std::wstring name);

	// Getting a connection handle
	SQLHDBC getConnectionHandle() const;

	// Getting time for FTP requests
	static int getCycleTimeFromDB(SQLHDBC dbc);

	// Getting config string in Json format
	static std::wstring getJsonConfigFromDatabase(std::string name, SQLHDBC dbc);

	// Checking if the database is connected
	bool isConnected();

	// Disconnecting from Database
	void disconnectFromDatabase();

	static void setConfigFileName(const std::wstring& name) {
		configFileName = name;
	}

	static std::wstring getConfigFileName() {
		return configFileName;
	}

private:
	SQLHENV env = SQL_NULL_HENV;
	SQLHDBC dbc = SQL_NULL_HDBC;

	// Finding the configuration file
	std::string findConfigFile();

	static std::wstring configFileName;
};


#endif // DBCONNECTION_H