#ifndef BASEFILE_H
#define BASEFILE_H

#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <boost/filesystem.hpp>
#include "integration_handler.h"
#include "utils.h"

namespace fs = boost::filesystem;

struct BaseFile {
    std::wstring fileName;              // File name
    std::wstring parentFolderPath;		// The folder where the file is located
    std::wstring fullPath;              // Full path to file include file name 
    std::string binaryData;			 	// Binary data
    size_t binaryDataSize;              // Size of file
    std::wstring fileNum;               // Num of file (xxxxx.xxx.321)

    bool inSortedFolder = false;        // Marker sort folder

    std::wstring unit;                            
    std::wstring substation;
    std::wstring object;
    int reconNumber = 0;

    std::wstring date;                 
    std::wstring time;

    virtual ~BaseFile() = default;

    // Reading a file in binary format (need fullPath)
    std::string readFileContent();

    void processPath(std::wstring rootFolder);
    bool getFileDateAndTime();

    // Virtual function for file processing
    virtual void processFile() {
        getFileDateAndTime();
        binaryData = readFileContent();

    };

};

// Derived class for ExpressFile
struct ExpressFile : public BaseFile {
    std::wstring typeKz;
    std::wstring damagedLine;
    std::wstring factor;

    bool hasExpressFile = false;

    void readDataFromFile();

    // Specific handling for ExpressFile
    void processFile() override {
        hasExpressFile = true;
        readDataFromFile();
    }
};

// Derived class for DataFile
struct DataFile : public BaseFile {
    bool hasDataFile = false;

    // Specific handling for DataFile
    void processFile() override {
        hasDataFile = true;
        binaryData = readFileContent();
        getFileDateAndTime();
    }
};

#endif
