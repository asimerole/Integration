#ifndef MAIL_HANDLER_H
#define MAIL_HANDLER_H

#include <string>
#include <vector>
#include <map>
#include "utils.h"
#include "db_connection.h" 
#include "base_file.h"
#include "integration_handler.h"


// Structure for mail server parameters
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

// Parsing json string config from database
MailServerConfig parseMailServerConfig(const std::string& jsonString);              

// Getting users from the database
std::map<std::string, std::vector<std::string>> loadUsersFromDatabase(SQLHDBC dbc); 

// Sending mail
bool sendEmails(const MailServerConfig& config, const std::map<std::string,         
    std::vector<std::string>>& users, const FileInfo& fileInfo);


#endif // MAIL_HANDLER_H
