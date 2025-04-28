#include <string>
#include <vector>
#include <map>
#include "utils.h"
#include "db_connection.h" 
#include "base_file.h"
#include "integration_handler.h"


// Структура для параметров почтового сервера
struct MailServerConfig {
    std::wstring smtp_server;
    bool auth_required;
    std::wstring email_sender;
    std::wstring auth_login;
    std::wstring auth_password;
    std::wstring message_template;
    std::wstring name_sender;
    std::wstring port;
    bool use_ssl;
};

// Парсинг json строки конфига из базы
MailServerConfig parseMailServerConfig(const std::string& jsonString);              

// Получение пользователей из базы
std::map<std::string, std::vector<std::string>> loadUsersFromDatabase(SQLHDBC dbc); 

// Отправка почты 
bool sendEmails(const MailServerConfig& config, const std::map<std::string,         
    std::vector<std::string>>& users, const FileInfo& fileInfo);



