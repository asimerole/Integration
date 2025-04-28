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
};

class Ftp {
public:
    // protocol (ftp or ftps)
    static const std::wstring& protocol() {
        static const std::wstring p = L"ftp://";
        return p;
    }

    static std::string encodeURL(const std::string& url);

    static size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream);

    static size_t write_list(void* buffer, size_t size, size_t nmemb, void* userp);

    static void collectServers(std::vector<ServerInfo>& servers, SQLHDBC dbc);

    static void setFileTime(const std::string& filePath, const std::string& timestamp);

    static bool downloadFile(const std::string& fileName, const ServerInfo& server, const std::string url, const std::wstring& ftpCacheDirPath);

    static int deleteFile(const std::string& filename, const ServerInfo& server, const std::string& url);

    static bool checkConnection(const std::string& url, const std::string login, const std::string pass);

    static bool isServerActive(const ServerInfo& server, SQLHDBC dbc);

    static void createLocalDirectoryTree(ServerInfo& server, std::string rootFolder);

    static void fileTransfer(const ServerInfo& server, const std::string& url, const std::wstring& oneDrivePath, std::atomic_bool& ftpIsActive, SQLHDBC dbc, const std::wstring& ftpCacheDirPath);


private:
    std::vector<ServerInfo> servers;
};

#endif // FTP_H