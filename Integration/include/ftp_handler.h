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
#include <map>

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
    static Ftp& getInstance() {
        static Ftp instance;
        return instance;
    }

    // prohibit copying
    Ftp(const Ftp&) = delete;
    void operator=(const Ftp&) = delete;

    // protocol (ftp or ftps)
    const std::wstring& protocol() {
        static const std::wstring p = L"ftp://";
        return p;
    }

	// Coding URL
    std::string encodeURL(const std::string& url);

	// Writing data to a file
    static size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream);

    // Function for working with ftp file
    static size_t write_list_stream(void* buffer, size_t size, size_t nmemb, void* userp);

	// Processing a single file
    void processSingleFile(const std::string& fileName, FtpTransferContext& context);

	// Collect ftp servers from the database
    void collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc);

	// Method to set time for files
    void setFileTime(const std::string& filePath, const std::string& timestamp);

	// Downloads a file from the server
    bool downloadFile(const std::string& fileName, const ServerInfo& server, const std::string url, const std::wstring& ftpCacheDirPath);

	// Deletes a file from the server
    int deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url);

	// Checks if the server is reachable
    bool checkConnection(const std::string& url, const std::string login, const std::string pass);

	// Checks if the server is active
    bool isServerActive(const ServerInfo& server, SQLHDBC dbc);

	// Creates a local directory tree based on server information
    void createLocalDirectoryTree(ServerInfo& server, std::string rootFolder);

	// Transferring files from the server
    void fileTransfer(const ServerInfo& server, const std::string& url, const std::wstring& oneDrivePath, std::atomic_bool& ftpIsActive, std::atomic_bool& oneDriveIsActive, SQLHDBC dbc, const std::wstring& ftpCacheDirPath);

	// Method to save the last ping time for a server
    void pingServer(int serverId, time_t time) {
        serverLastPing[serverId] = time;
    }

	// Method to get the last ping time for a server
    const time_t getLastPing(int serverId) const {
        auto it = serverLastPing.find(serverId);
        return it != serverLastPing.end() ? it->second : 0;
    }



private:
	// Private constructor to prevent instantiation
    Ftp() {}

	// Vector to store server information
    std::vector<ServerInfo> servers;
   
    // Storing last server pings for Logs table 
    std::map<int, std::time_t> serverLastPing;
};

#endif // FTP_H