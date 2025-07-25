#ifndef INTEGRATION_H
#define INTEGRATION_H

#include <set>
#include <map>
#include <string>
#include <regex>
#include <thread>
#include <Windows.h>
#include <shellapi.h>
#include <sstream>
#include <boost/filesystem.hpp>
#include <db_connection.h>
#include <file_info.h>
#include <utils.h>
#include <cwctype>
#include <boost/algorithm/string/predicate.hpp>
#include "ftp_handler.h"

namespace fs = boost::filesystem;

struct RecordsInfoFromDB {
	bool hasDataBinary = false;     // Data file binary exists
	bool hasExpressBinary = false;  // Express file binary exists
	bool hasOtherBinary = false;    // Other file binary exists
	bool needDataProcess = false;   // Need insert into precess table
	int data_id = -1;               // ID in data table
	int struct_id = -1;             // ID in struct table
	int unit_id = -1;               // ID in units table
	int dataProcess_id = -1;        // ID in data_process table
};

class Integration {
public:
    // Run OMP_C program
    static bool runExternalProgramWithFlag(const std::wstring& programPath, const std::wstring& inputFilePath);

    // Collect paths to files 
    static void collectRootPaths(std::unordered_set<std::wstring>& parentFolders, const std::wstring rootFolder);

    // General integration method
    static void fileIntegrationDB(SQLHDBC dbc, const FileInfo& fileInfo, std::atomic_bool& mailingIsActive, std::atomic_bool& dbIsFull);

    // Method for sorting by folders
    static void sortFiles(const FileInfo& fileInfo);

    // General method of collecting information and a pair of files
    static void collectInfo(FileInfo& fileInfo, const fs::directory_entry& entry, std::wstring rootFolder, const std::wstring pathToOMPExecutable, SQLHDBC dbc);

    // Method to get path for file by recon number
    static std::wstring getPathByRNumber(int recon_id, SQLHDBC dbc);

    //Getting values ​​by markers in a file
    static std::wstring extractParamValue(const std::wstring& content, const std::wstring& marker);

    // Getting values ​​by regular expressions
    static std::wstring extractValueWithRegex(const std::wstring& content, const std::wregex& regex);

    // Helper function for concatenating strings with a separator
    static std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& delimiter);

    // Checking a folder for sorted name
    static bool isSortedFolder(const std::wstring& folderName);

private:
    // Checking file name for validity
    static bool isFileNameValid(const std::wstring& fileName);
 
    // Checking file name starting with 'RECON'
    static bool checkIsDataFile(const std::wstring& fileName);

    // Checking file name starting with 'REXPR'
    static bool checkIsExpressFile(const std::wstring& fileName);

    // Checking file for type rnet. prpusk. daily и diagn
    static bool checkIsOtherFiles(const std::wstring& fileName);

    // Getting id's from tables: data, units, struct 
    static void getRecordInfo(SQLHDBC dbc, std::shared_ptr<BaseFile> file, RecordsInfoFromDB &recordsInfo);

    // Insert into units
    static int insertIntoUnitTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file);

    // Insert into struct
    static int insertIntoStructTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file);

    // Insert into data
    static int insertIntoDataTable(SQLHDBC dbc, const FileInfo& fileInfo, RecordsInfoFromDB recordsInfo);

    // Insert into data_process
    static int insertIntoProcessTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file, int data_id);

    // Insert into logs
    static void insertIntoLogsTable(SQLHDBC dbc, const FileInfo& fileInfo, int struct_id);

    // Storing file paths
    std::unordered_set<std::wstring> parentFolders;
	
    // Path to root folder
    std::wstring rootFolder;

};

#endif // INTEGRATION_H
