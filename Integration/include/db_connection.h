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
	// ����������� � ����
	void connectToDatabase();											

	// ���������� SQL ������� 
	static bool executeSQL(SQLHDBC dbc, const std::wstringstream& sql);	

	// ������� ��������� �����
	static bool insertBinaryFileToDatabase(SQLHDBC dbc, const std::string& filePath, int structId);

	// ������� ������ � ��������� ��� ���� 
	static int executeSQLAndGetIntResult(SQLHDBC dbc, const std::wstringstream& query);

	// ��������� ������� �����
	static std::wstring getRootFolder(SQLHDBC dbc);

	// ��������� ����������� ���������� 
	SQLHDBC getConnectionHandle() const;

	// ��������� ������� �� ������� �� ��� 
	static int getCycleTimeFromDB(SQLHDBC dbc);

	// ��������� ������ ������� � Json ������� 
	static std::wstring getJsonConfigFromDatabase(std::string name, SQLHDBC dbc);

	// �������� ����������� � ���� 
	bool isConnected();

	// ���������� ����
	void disconnectFromDatabase();

private:
	SQLHENV env = SQL_NULL_HENV;
	SQLHDBC dbc = SQL_NULL_HDBC;

	// ����� ������ ����� � �������� ��������
	std::string findConfigFile();
};


#endif // DBCONNECTION_H