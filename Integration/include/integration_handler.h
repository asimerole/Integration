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

namespace fs = boost::filesystem;

class Integration {
public:
    // Run OMP_C program
    static bool runExternalProgramWithFlag(const std::wstring& programPath, const std::wstring& inputFilePath);

    // Checking file name for validity
    static bool isFileNameValid(const std::wstring& fileName);
 
    // Checking file name starting with 'RECON'
    static bool checkIsDataFile(const std::wstring& fileName);

    // Checking file name starting with 'REXPR'
    static bool checkIsExpressFile(const std::wstring& fileName);

    // Checking file for type rnet. prpusk. daily и diagn
    static bool checkIsOtherFiles(const std::wstring& fileName);

    // Checking a folder for sorted name
    static bool isSortedFolder(const std::wstring& folderName);

    //Getting values ​​by markers in a file
    static std::wstring extractParamValue(const std::wstring& content, const std::wstring& marker);

    // Getting values ​​by regular expressions
    static std::wstring extractValueWithRegex(const std::wstring& content, const std::wregex& regex);

    // Collect paths to files 
    static void collectRootPaths(std::set<std::wstring>& parentFolders, const std::wstring rootFolder);

    // Getting id's from tables: data, units, struct 
    static std::tuple<int, int, int, int> getRecordIDs(SQLHDBC dbc, std::shared_ptr<BaseFile> file, bool needDataProcess);

    // Insert into units
    static int insertIntoUnitTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file);

    // Insert into struct
    static int insertIntoStructTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file);

    // Insert into data
    static int insertIntoDataTable(SQLHDBC dbc, const FileInfo& fileInfo, int struct_id);

    // Insert into data_process
    static int insertIntoProcessTable(SQLHDBC dbc, const std::shared_ptr<BaseFile> file, int data_id);

    // General integration method
    static void fileIntegrationDB(SQLHDBC dbc, const FileInfo& fileInfo, std::atomic_bool& mailingIsActive);

    // Helper function for concatenating strings with a separator
    static std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& delimiter); 

    // General method of collecting information and a pair of files
    static void collectInfo(FileInfo &fileInfo, const fs::directory_entry& entry, std::wstring rootFolder, SQLHDBC dbc);

    // Method for sorting by folders
    static void sortFiles(const FileInfo& fileInfo);

    // Method to get path for file by recon number
    static std::wstring getPathByRNumber(int recon_id, SQLHDBC dbc);


private:
    // Storing file paths
    std::set<std::wstring> parentFolders;
	
    // Path to root folder
    std::wstring rootFolder;

};

#endif // INTEGRATION_H
