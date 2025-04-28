#ifndef FILEINFO_H
#define FILEINFO_H

#include <vector>
#include <memory>
#include <string>
#include "base_file.h"

struct BaseFile;

struct FileInfo {
    std::vector<std::shared_ptr<BaseFile>> files;
};

#endif