#pragma once

#include "db.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <optional> // Thêm nếu chưa có, vì download_file trả về optional

namespace fs = std::filesystem;

struct FileInfo {
    std::string name;
    std::string path;
    bool is_directory;
    uintmax_t size;
    std::time_t last_modified;
    // std::string checksum;
};


class FileManager {
public:
    FileManager(Database& db);

    bool upload_file(const fs::path& server_base_path, const std::string& relative_path, const std::vector<char>& data, int user_id = -1);
    std::optional<std::vector<char>> download_file(const fs::path& server_base_path, const std::string& relative_path, int user_id = -1);
    bool delete_file_or_directory(const fs::path& server_base_path, const std::string& relative_path, int user_id = -1);
    bool create_directory(const fs::path& server_base_path, const std::string& relative_path, int user_id = -1);
    std::vector<FileInfo> list_directory(const fs::path& server_base_path, const std::string& relative_path, int user_id = -1);
    
    // Path validation and resolution
    fs::path resolve_safe_path(const fs::path& base_path, const std::string& relative_user_path); // Đảm bảo khai báo này có và đúng

    std::string calculate_checksum(const fs::path& file_path);

    // --- THÊM KHAI BÁO NÀY VÀO ---
    bool update_metadata_after_rename(const fs::path& old_abs_path_obj, const fs::path& new_abs_path_obj, int user_id);
    // -------------------------------

private:
    Database& db_;
    void update_file_metadata(const fs::path& full_server_path, int user_id = -1); // Giữ nguyên user_id tùy chọn
    void remove_file_metadata(const fs::path& full_server_path);
    // calculate_checksum đã được public rồi, không cần private nữa nếu muốn gọi từ ngoài
};