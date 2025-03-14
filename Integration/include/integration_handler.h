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
#include <cwctype>

namespace fs = boost::filesystem;

struct Recon
{
    std::wstring unit = L"";
    std::wstring substation = L"";
    std::wstring object = L"";
    int reconNumber = 0;

    bool operator<(const Recon& other) const
    {
        if (reconNumber != other.reconNumber)
        {
            return reconNumber < other.reconNumber;
        }
        return false;
    }
};

struct FilePair
{
    std::wstring expressFileName = L"";  // REXPRxxx.xxx (ex. REXPR218.453) (can read)
    std::wstring dataFileName = L"";     // RECONxxx.xxx (ex. RECON218.453) (can not read)
    std::wstring date = L"";             // Data from express-file, ex. 08/04/2024
    std::wstring time = L"";             // Time from express-file, ex. 07:34:51.622.
    std::wstring parentFolderPath = L""; // The folder where the file is located
    std::wstring fileNum = L"";          // Number of file 

    // Тип Recon должен быть объявлен до использования, если используется в структуре
    Recon recon;

    bool hasDataFile = false;
    bool hasExpressFile = false;
    bool inSortedFolder = false;

    std::wstring typeKz = L"";
    std::wstring damagedLine = L"";
    std::wstring factor = L"";

    std::string expressFile;    // Binary data for express-file
    std::string dataFile;       // Binary data for data-file

    bool operator<(const FilePair& other) const
    {
        if (parentFolderPath != other.parentFolderPath)
        {
            return parentFolderPath < other.parentFolderPath;
        }
        if (dataFileName != other.dataFileName)
        {
            return dataFileName < other.dataFileName;
        }
        return expressFileName < other.expressFileName;
    }
};

class Integration {
public:
    // Запуск програмы OMP_C
    static bool runExternalProgramWithFlag(const std::wstring& programPath, const std::wstring& inputFilePath);

    // Проверка на то что файл начинаеться на 'RECON'
    static bool checkIsDataFile(const std::wstring& fileName);

    // Проверка на то что файл начинаеться на 'REXPR'
    static bool checkIsExpressFile(const std::wstring& fileName);

    // Проверка папки на ссортированное название 
    static bool isSortedFolder(const std::wstring& folderName);

    // Получение значений по маркерам в файле 
    static std::wstring extractParamValue(const std::wstring& content, const std::wstring& marker);

    // Чтение файла в бинарном формате 
    static std::string readFileContent(const std::wstring& filePath);

    // Получение значений по регулярным выражениям
    static std::wstring extractValueWithRegex(const std::wstring& content, const std::wregex& regex);

    // Чтение экспресс файла 
    static void readDataFromFile(const std::wstring& filePath, FilePair& pair);

    // Сбор путей к файлам 
    static void collectRootPaths(std::set<std::wstring>& parentFolders, const std::wstring rootFolder);

    // Получение айди из таблиц: data, units, struct 
    static std::tuple<int, int, int, int> getRecordIDs(SQLHDBC dbc, const FilePair& pair);

    // Вставка в таблицу units
    static int insertIntoUnitTable(SQLHDBC dbc, const FilePair& pair);

    // Вставка в таблицу struct
    static int insertIntoStructTable(SQLHDBC dbc, const FilePair& pair);

    // Вставка в таблицу data
    static int insertIntoDataTable(SQLHDBC dbc, const FilePair& pair, int struct_id);

    // Вставка в таблицу data_process
    static int insertIntoProcessTable(SQLHDBC dbc, const FilePair& pair, int data_id);

    // Общий метод интеграции
    static void fileIntegrationDB(SQLHDBC dbc, const FilePair& pair, std::atomic_bool& mailingIsActive);

    // Вспомогательная функция для объединения строк с разделителем
    static std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& delimiter);
    
    // Прасинг путя для в случае сортировки 
    static void processPath(const fs::path& fullPath, FilePair& pair, std::wstring rootFolder);

    // Общий метод сбора информации и паре файлов 
    static void collectInfo(FilePair& pair, const fs::directory_entry& entry, const std::wstring& path, std::wstring rootFolder, SQLHDBC dbc);

    // Метод для сортировки по папкам
    static void sortPair(FilePair& pair);

    // Метод для получения пути для файла по номеру рекона
    static std::wstring getPathByRNumber(int recon_id, SQLHDBC dbc);


private:
    // Хранение путей к файлам 
	std::set<std::wstring> parentFolders;
	
    // Путь корневой папке
    std::wstring rootFolder;

};

#endif // INTEGRATION_H
