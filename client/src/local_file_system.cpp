#include "local_file_system.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h> // Giữ lại SHA256 cũ
#include <chrono>      // Cho chuyển đổi thời gian

LocalFileSystem::LocalFileSystem() {}

std::vector<char> LocalFileSystem::readFile(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("LFS: Cannot open file for reading: " + filePath.string());
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("LFS: Error reading file: " + filePath.string());
    }
    return buffer;
}

bool LocalFileSystem::writeFile(const fs::path& filePath, const std::vector<char>& data) {
    try {
        fs::path parent = filePath.parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "LFS: Cannot open file for writing: " << filePath.string() << std::endl;
            return false;
        }
        file.write(data.data(), data.size());
        return file.good();
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Filesystem error writing file " << filePath << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalFileSystem::createDirectoryRecursive(const fs::path& dirPath) {
    try {
        return fs::create_directories(dirPath);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Error creating directory " << dirPath << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalFileSystem::deletePathRecursive(const fs::path& path) {
    try {
        if (!fs::exists(path)) return true; // Không tồn tại thì coi như đã xóa
        return fs::remove_all(path) > 0 || !fs::exists(path); // remove_all trả về số lượng đã xóa
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Error deleting path " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalFileSystem::renamePath(const fs::path& oldPath, const fs::path& newPath) {
    try {
        fs::rename(oldPath, newPath);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Error renaming path from " << oldPath << " to " << newPath << ": " << e.what() << std::endl;
        return false;
    }
}

Poco::Timestamp file_time_to_poco_timestamp(const fs::file_time_type& ftime) {
    // Chuyển đổi fs::file_time_type sang std::time_t rồi sang Poco::Timestamp
    std::chrono::time_point<std::chrono::system_clock> sctp(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            ftime.time_since_epoch()
        )
    );
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    return Poco::Timestamp::fromEpochTime(tt);
}


std::optional<LocalFileInfo> LocalFileSystem::getFileInfo(const fs::path& path, const fs::path& syncRoot) {
    try {
        if (!fs::exists(path)) return std::nullopt;
        LocalFileInfo info;
        info.absolutePath = fs::weakly_canonical(path); // Hoặc canonical nếu chắc chắn tồn tại
        info.relativePath = fs::relative(info.absolutePath, syncRoot).lexically_normal().string();
        info.isDirectory = fs::is_directory(path);
        info.size = info.isDirectory ? 0 : fs::file_size(path);
        info.lastModifiedPoco = file_time_to_poco_timestamp(fs::last_write_time(path));
        if (!info.isDirectory) {
            info.checksum = calculateChecksum(path);
        }
        return info;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Error getting file info for " << path << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::string LocalFileSystem::calculateChecksum(const fs::path& filePath) {
    // Giữ nguyên code SHA256 cũ (sẽ có warning) hoặc thay bằng EVP
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) { return ""; }
    unsigned char hash_digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256_ctx;
    SHA256_Init(&sha256_ctx);
    char buffer[8192];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            SHA256_Update(&sha256_ctx, buffer, static_cast<size_t>(bytes_read));
        }
    }
    file.close();
    SHA256_Final(hash_digest, &sha256_ctx);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<unsigned int>(hash_digest[i]);
    }
    return ss.str();
}

std::vector<LocalFileInfo> LocalFileSystem::scanDirectoryRecursive(const fs::path& dirPath, const fs::path& syncRoot) {
    std::vector<LocalFileInfo> results;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return results;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            auto info_opt = getFileInfo(entry.path(), syncRoot);
            if (info_opt) {
                results.push_back(*info_opt);
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "LFS: Error scanning directory " << dirPath << ": " << e.what() << std::endl;
    }
    return results;
}