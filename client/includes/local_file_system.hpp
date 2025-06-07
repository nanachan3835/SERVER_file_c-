#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <Poco/Timestamp.h> // Sử dụng Poco::Timestamp

namespace fs = std::filesystem;

struct LocalFileInfo {
    fs::path absolutePath;
    std::string relativePath;
    bool isDirectory;
    uintmax_t size;
    Poco::Timestamp lastModifiedPoco; // Đổi sang Poco::Timestamp
    std::string checksum;
};

class LocalFileSystem {
public:
    LocalFileSystem();
    std::vector<char> readFile(const fs::path& filePath);
    bool writeFile(const fs::path& filePath, const std::vector<char>& data);
    bool createDirectoryRecursive(const fs::path& dirPath); // Đổi tên
    bool deletePathRecursive(const fs::path& path);      // Đổi tên
    bool renamePath(const fs::path& oldPath, const fs::path& newPath);
    std::optional<LocalFileInfo> getFileInfo(const fs::path& path, const fs::path& syncRoot);
    std::string calculateChecksum(const fs::path& filePath); // SHA256
    std::vector<LocalFileInfo> scanDirectoryRecursive(const fs::path& dirPath, const fs::path& syncRoot);
};