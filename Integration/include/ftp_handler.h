#ifndef FTP_H
#define FTP_H

#define _CRT_SECURE_NO_WARNINGS 
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <boost/filesystem.hpp>
#include <curl/curl.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sql.h>
#include <sys/stat.h>
#include <sys/utime.h>  

namespace fs = boost::filesystem;

// Structure for storing server information
struct ServerInfo {
    std::wstring unit;
    std::wstring substation;
    std::wstring object;
    std::wstring ip;
    std::wstring login;
    std::wstring pass;
    std::wstring remoteFolderPath;   // Folder on the server (FROM)
    std::wstring localFolderPath;    // Folder on the PC     (TO)
    int status = 0;
    int reconId = 0;
    std::time_t lastPingTime;
};

// Structure for transferring context information during FTP operations
struct FtpTransferContext {
    const ServerInfo* server;
    std::string url;
    std::wstring oneDrivePath;
    std::atomic_bool* ftpIsActive;
    std::atomic_bool* oneDriveIsActive;
    SQLHDBC dbc;
    std::wstring ftpCacheDirPath;

    std::atomic<size_t> processedFiles{ 0 };
    size_t maxFilesPerSession = 500;
};

class Ftp {
public:
    // protocol (ftp or ftps)
    static const std::wstring& protocol() {
        static const std::wstring p = L"ftp://";
        return p;
    }

	// Coding URL
    static std::string encodeURL(const std::string& url);

	// Writing data to a file
    static size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream);

    // Function for working with ftp file
    static size_t write_list_stream(void* buffer, size_t size, size_t nmemb, void* userp);

	// Processing a single file
    static void processSingleFile(const std::string& fileName, FtpTransferContext& context);

	// Collect ftp servers from the database
    static void collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc);

	// Method to set time for files
    static void setFileTime(const std::string& filePath, const std::string& timestamp);

	// Downloads a file from the server
    static bool downloadFile(const std::string& fileName, const ServerInfo& server, const std::string url, const std::wstring& ftpCacheDirPath);

	// Deletes a file from the server
    static int deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url);

	// Checks if the server is reachable
    static bool checkConnection(const std::string& url, const std::string login, const std::string pass);

	// Checks if the server is active
    static bool isServerActive(const ServerInfo& server, SQLHDBC dbc);

	// Creates a local directory tree based on server information
    static void createLocalDirectoryTree(ServerInfo& server, std::string rootFolder);

	// Transferring files from the server
    static void fileTransfer(const ServerInfo& server, const std::string& url, const std::wstring& oneDrivePath, std::atomic_bool& ftpIsActive, std::atomic_bool& oneDriveIsActive, SQLHDBC dbc, const std::wstring& ftpCacheDirPath);


private:
	// Vector to store server information
    std::vector<ServerInfo> servers;
};

#endif // FTP_H