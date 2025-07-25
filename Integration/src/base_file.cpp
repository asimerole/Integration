#include "base_file.h"


std::string BaseFile::readFileContent() {
    if (fullPath.empty()) {
        return "";
    }
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {  
        return "";
    }
    if (file.fail()) {
        return "";
    }
    try {
        std::uintmax_t size = fs::file_size(fullPath);
        if (size <= 0 || size == std::streamsize(-1)) {
            //logError(L"Incorrect size in readFileContent" + fullPath, LOG_PATH);
            return "";
        }

        binaryDataSize = size;
        file.seekg(0, std::ios::beg);       

        std::string buffer(size, '\0');
        if (!file.read(&buffer[0], size)) {
            return "";
        }

        return buffer;
    }
    catch (...) {
        return "";
    }
}


void BaseFile::processPath(std::wstring rootFolder)
{
    // We get the path to the parent folder
    fs::path parentPath = parentFolderPath;
    if (parentPath.filename() == ".")
        parentPath = parentPath.parent_path();

    std::wstring parentFolderName = parentPath.filename().wstring();

    // Check if the file is in a sorted folder
    if (Integration::isSortedFolder(parentFolderName)) {
        inSortedFolder = true;
        parentPath = parentPath.parent_path(); // Cut up 1 level
    }
    parentFolderPath = parentPath.wstring(); // Save the path without the YYYY_MM folder

    // Relative path from rootFolder to file
    fs::path relativePath = fs::relative(parentPath, rootFolder);
    std::vector<std::wstring> pathParts;

	std::wstring rootFolderName = fs::path(rootFolder).filename().wstring();
    for (const auto& part : relativePath) {
        if (part.wstring() == rootFolderName) {
			break; // Stop if we reach the root folder
        }
        pathParts.push_back(part.wstring());
    }

    if (pathParts.size() >= 3) {
        object = pathParts.back();        // The last folder — object
        pathParts.pop_back();             // Delete last element (object)

        substation = pathParts.back();    // Penultimate — substation
        pathParts.pop_back();             // Delete penultimate element (substation)

        // The rest combine into unit
        unit = Integration::join(pathParts, L" - ");
    }
    int reconNum;
    if (fileName.size() >= 8) {
        reconNum = std::stoi(fileName.substr(5, 3));
    }
    else {
        logError(L"[BaseFile::processPath] fileName.size() <= 8. File name: " + fileName, INTEGRATION_LOG_PATH);
    }

    reconNumber = reconNum;
}

bool BaseFile::getFileDateAndTime()
{
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;

    if (!GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        logError(L"[getFileDateAndTime]: Error opening file", INTEGRATION_LOG_PATH);
        return false;
    }

    // Time conversion
    FILETIME localFileTime;
    SYSTEMTIME sysTime;
    FileTimeToLocalFileTime(&fileInfo.ftLastWriteTime, &localFileTime);
    FileTimeToSystemTime(&localFileTime, &sysTime);

    // Formatting date and time wYear wMonth wDay
    std::wostringstream dateStream, timeStream;
    dateStream << std::setw(2) << std::setfill(L'0') << sysTime.wDay << L"/"
               << std::setw(2) << std::setfill(L'0') << sysTime.wMonth << L"/"
               << std::setw(4) << std::setfill(L'0') << sysTime.wYear;

    timeStream << std::setw(2) << std::setfill(L'0') << sysTime.wHour << L":"
               << std::setw(2) << std::setfill(L'0') << sysTime.wMinute << L":"
               << std::setw(2) << std::setfill(L'0') << sysTime.wSecond;

    // We write into the structure
    date = dateStream.str();
    time = timeStream.str();

    return true;
}

void ExpressFile::readDataFromFile() {
    try {
        std::string fileContent = readFileContent();

        std::string utf8Content = cp866_to_utf8(fileContent);
        std::wstring wideFileContent = stringToWString(utf8Content);
        binaryData = fileContent;


        std::map<std::wstring, std::wregex> regexMap = {
            {L"date", std::wregex(L"Дата\\s*:?\\s*(\\d{2}/\\d{2}/\\d{4})")},
            {L"time", std::wregex(L"Время(?:\\s+пуска)?\\s*:?\\s*(\\d{2}:\\d{2}:\\d{2}\\.\\d{3})")},
            {L"reconObject", std::wregex(L"Объект:\\s*(.*)")},
            {L"factor", std::wregex(L"Фактор пуска:\\s*(.*)")},
            {L"typeKz", std::wregex(L"Повреждение.*:\\s*(.*)")},
            {L"damagedLine", std::wregex(L"Поврежденная линия, предположительно:\\s*(.*)")}
        };

        date = Integration::extractValueWithRegex(wideFileContent, regexMap[L"date"]);
        time = Integration::extractValueWithRegex(wideFileContent, regexMap[L"time"]);
        factor = Integration::extractValueWithRegex(wideFileContent, regexMap[L"factor"]);
        typeKz = Integration::extractValueWithRegex(wideFileContent, regexMap[L"typeKz"]);
        damagedLine = Integration::extractValueWithRegex(wideFileContent, regexMap[L"damagedLine"]);

        if (date.empty() || time.empty() || factor.empty() || typeKz.empty()) {
            if (date.empty()) {
                date = Integration::extractParamValue(wideFileContent, L"$DP=");
            }
            if (time.empty()) {
                time = Integration::extractParamValue(wideFileContent, L"$TP=");
            }
            if (factor.empty()) {
                factor = Integration::extractParamValue(wideFileContent, L"$SF=");
            }
            if (typeKz.empty()) {
                typeKz = Integration::extractParamValue(wideFileContent, L"$LF=");
                if (typeKz == L"1" || typeKz == L"2" || typeKz == L"3" || typeKz == L"4") {
                    std::string templateStr = " фазное КЗ";
                    typeKz += stringToWString(templateStr);
                }
                else {
                    typeKz = L" ";
                }
            }
        }

    }
    catch (const std::exception& e) {
        logError(stringToWString("Exception caught in readDataFromFile: ") + fileName + L" " + stringToWString(e.what()), INTEGRATION_LOG_PATH);
    }
}