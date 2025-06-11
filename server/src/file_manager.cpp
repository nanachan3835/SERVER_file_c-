#include "file_manager.hpp"
#include "config.hpp"
#include <fstream>
#include <iostream>
#include <openssl/sha.h> // For checksums later
#include <iomanip>
#include <sstream>
#include <filesystem> // Đảm bảo include
#include <chrono>  
namespace fs = std::filesystem;



FileManager::FileManager(Database& db) : db_(db) {}

// Helper to ensure user_path is within base_path and doesn't use ".." to escape.
// Returns the canonical absolute path if safe, otherwise an empty path.
fs::path FileManager::resolve_safe_path(const fs::path& base_path, const std::string& relative_user_path_str) {
    fs::path relative_user_path(relative_user_path_str);
    for (const auto& part : relative_user_path) {
        if (part == "..") {
            std::cerr << "Path traversal attempt detected: " << relative_user_path_str << std::endl;
            return {};
        }
    }
    fs::path full_path = base_path / relative_user_path;
    fs::path canonical_full_path, canonical_base_path;
    try {
        if (!fs::exists(base_path) || !fs::is_directory(base_path)) { return {}; }
        canonical_base_path = fs::weakly_canonical(base_path);
        canonical_full_path = fs::weakly_canonical(full_path);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Path canonicalization error (" << full_path << "): " << e.what() << std::endl;
        return {};
    }
    std::string full_str = canonical_full_path.string();
    std::string base_str = canonical_base_path.string();
    if (full_str.rfind(base_str, 0) == 0) {
        if (full_str.length() == base_str.length() || (full_str.length() > base_str.length() && full_str[base_str.length()] == fs::path::preferred_separator)) {
            return canonical_full_path;
        }
    }
    std::cerr << "Path " << full_path << " is outside base " << base_path << std::endl;
    return {};
}




bool FileManager::upload_file(const fs::path& server_base_path, const std::string& relative_path_str, const std::vector<char>& data, int user_id) {
    fs::path relative_path(relative_path_str);
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path);

    if (full_server_path.empty()) {
        std::cerr << "Upload: unsafe or invalid path: " << relative_path_str << " relative to " << server_base_path << std::endl;
        return false;
    }
    
    try {
        // Create parent directories if they don't exist
        if (full_server_path.has_parent_path()) {
            fs::create_directories(full_server_path.parent_path());
        }

        std::ofstream outfile(full_server_path, std::ios::binary | std::ios::trunc);
        if (!outfile) {
            std::cerr << "Failed to open file for writing: " << full_server_path << std::endl;
            return false;
        }
        outfile.write(data.data(), data.size());
        outfile.close();
        std::cout << "Uploaded file: " << full_server_path << std::endl;
        update_file_metadata(full_server_path, user_id);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error uploading file " << full_server_path << ": " << e.what() << std::endl;
        return false;
    }
}

std::optional<std::vector<char>> FileManager::download_file(const fs::path& server_base_path, const std::string& relative_path_str, int user_id) {
    fs::path relative_path(relative_path_str);
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path);

    if (full_server_path.empty() || !fs::exists(full_server_path) || fs::is_directory(full_server_path)) {
        std::cerr << "Download: File not found or is a directory: " << full_server_path << std::endl;
        return std::nullopt;
    }

    try {
        std::ifstream infile(full_server_path, std::ios::binary | std::ios::ate);
        if (!infile) {
            std::cerr << "Failed to open file for reading: " << full_server_path << std::endl;
            return std::nullopt;
        }
        std::streamsize size = infile.tellg();
        infile.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (infile.read(buffer.data(), size)) {
            std::cout << "Downloaded file: " << full_server_path << std::endl;
            return buffer;
        } else {
            std::cerr << "Failed to read file: " << full_server_path << std::endl;
            return std::nullopt;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error downloading file " << full_server_path << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

bool FileManager::delete_file_or_directory(const fs::path& server_base_path, const std::string& relative_path_str, int user_id) {
    fs::path relative_path(relative_path_str);
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path);

    if (full_server_path.empty() || !fs::exists(full_server_path)) {
        std::cerr << "Delete: Path not found or unsafe: " << full_server_path << std::endl;
        return false;
    }
    // Prevent deleting the base path itself
    if (full_server_path == fs::weakly_canonical(server_base_path)) {
        std::cerr << "Delete: Attempt to delete base path denied: " << full_server_path << std::endl;
        return false;
    }

    try {
        if (fs::is_directory(full_server_path)) {
           
            for (const auto& entry : fs::recursive_directory_iterator(full_server_path)) {
                remove_file_metadata(entry.path());
            }
            
            remove_file_metadata(full_server_path);

            
            fs::remove_all(full_server_path);
        } else {
            
            remove_file_metadata(full_server_path);
            fs::remove(full_server_path);
        }
        // --- KẾT THÚC SỬA ĐỔI ---

        std::cout << "Deleted: " << full_server_path << std::endl;
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error deleting " << full_server_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FileManager::create_directory(const fs::path& server_base_path, const std::string& relative_path_str, int user_id) {
    fs::path relative_path(relative_path_str);
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path);

    if (full_server_path.empty()) {
        std::cerr << "Create Directory: Unsafe or invalid path: " << relative_path_str << std::endl;
        return false;
    }

    try {
        if (fs::create_directories(full_server_path)) {
            std::cout << "Created directory: " << full_server_path << std::endl;
            // Optionally, add metadata for directories if needed, e.g., for empty dir sync
            // update_file_metadata(full_server_path, user_id); // Or a specific dir metadata function
            update_file_metadata(full_server_path, user_id);
            return true;
        } else {
             // It might already exist, which is not an error for create_directories
            if (fs::exists(full_server_path) && fs::is_directory(full_server_path)) {
                 std::cout << "Directory already exists: " << full_server_path << std::endl;
                 return true;
            }
            std::cerr << "Failed to create directory: " << full_server_path << std::endl;
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error creating directory " << full_server_path << ": " << e.what() << std::endl;
        return false;
    }
}

std::vector<FileInfo> FileManager::list_directory(const fs::path& server_base_path, const std::string& relative_path_str, int user_id) {
    std::vector<FileInfo> result;
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path_str);

    if (full_server_path.empty() || !fs::exists(full_server_path) || !fs::is_directory(full_server_path)) {
        std::cerr << "List Directory: Path not found, not a directory, or unsafe: " << full_server_path << std::endl;
        return result;
    }

    try {
        for (const auto& entry : fs::directory_iterator(full_server_path)) {
            FileInfo info;
            info.name = entry.path().filename().string();
            fs::path entry_relative_path = fs::relative(entry.path(), server_base_path);
            info.path = entry_relative_path.lexically_normal().string();
            info.is_directory = entry.is_directory();
            if (!info.is_directory) {
                 try { info.size = entry.file_size(); } catch(const fs::filesystem_error&){ info.size = 0;}
            } else {
                info.size = 0;
            }

            auto ftime = fs::last_write_time(entry.path());
            // SỬA LỖI CHUYỂN ĐỔI THỜI GIAN
            std::chrono::time_point<std::chrono::system_clock> sctp(
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    ftime.time_since_epoch()
                )
            );
            info.last_modified = std::chrono::system_clock::to_time_t(sctp);

            result.push_back(info);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error listing directory " << full_server_path << ": " << e.what() << std::endl;
    }
    return result;
}



std::string FileManager::calculate_checksum(const fs::path& file_path_obj) {
    // Giữ nguyên code SHA256 cũ (sẽ có warning) hoặc thay bằng EVP
    fs::path file_path = fs::weakly_canonical(file_path_obj);
    std::ifstream file(file_path, std::ios::binary);
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


void FileManager::update_file_metadata(const fs::path& full_server_path_obj, int user_id) {
    // if (!fs::exists(full_server_path_obj) || fs::is_directory(full_server_path_obj)) {
    //     if (fs::is_directory(full_server_path_obj)) return;
    // }
    if (!fs::exists(full_server_path_obj)) return;
    bool is_dir = fs::is_directory(full_server_path_obj);
    std::string full_server_path_str = fs::weakly_canonical(full_server_path_obj).string();
    std::string checksum = is_dir ? "" : calculate_checksum(full_server_path_obj);

    //////////////////////////////
    auto ftime = fs::last_write_time(full_server_path_obj);
    // SỬA LỖI CHUYỂN ĐỔI THỜI GIAN
    std::chrono::time_point<std::chrono::system_clock> sctp(
    std::chrono::duration_cast<std::chrono::system_clock::duration>(
            ftime.time_since_epoch()
        )
    );


    time_t last_modified = std::chrono::system_clock::to_time_t(sctp);
    // #if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && defined(__cpp_lib_filesystem) && __cpp_lib_filesystem >= 201703L && !defined(__APPLE__)
    //     time_t last_modified = std::chrono::system_clock::to_time_t(std::chrono::utc_clock::to_sys(std::chrono::file_clock::to_utc(ftime))));
    // #else
    //     auto system_time_point_list = std::chrono::system_clock::now() + (ftime - decltype(ftime)::clock::now());
    //     time_t last_modified = std::chrono::system_clock::to_time_t(system_time_point_list);
    // #endif
    sqlite3_stmt* stmt;
    std::string sql = R"(
        INSERT INTO file_metadata (file_path, checksum, last_modified, owner_user_id, version, is_directory, is_deleted)
        VALUES (?, ?, ?, ?, 1, ?, 0)
        ON CONFLICT(file_path) DO UPDATE SET
        checksum = excluded.checksum,
        last_modified = excluded.last_modified,
        owner_user_id = COALESCE(excluded.owner_user_id, owner_user_id),
        version = version + 1,
        is_directory = excluded.is_directory,
        is_deleted = 0; -- Quan trọng: đảm bảo file được "hồi sinh" nếu được upload lại
    )";

    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        //////////////
        sqlite3_bind_text(stmt, 1, full_server_path_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, checksum.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(last_modified));
        if (user_id != -1) { sqlite3_bind_int(stmt, 4, user_id); } else { sqlite3_bind_null(stmt, 4); }
        /////////////////
        sqlite3_bind_int(stmt, 5, is_dir ? 1 : 0);
        /////////////////
        // Execute the statement
         // Note: This will insert a new row or update an existing one
         // If the file already exists, it will update the metadata
         // If it doesn't exist, it will insert a new row

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to update metadata for " << full_server_path_str << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        } else {
            std::cout << "Updated metadata for " << full_server_path_str << std::endl;
        }
         // Finalize the statement to release resources
         // Note: This is important to avoid memory leaks
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare metadata statement for " << full_server_path_str << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
    }
}

void FileManager::remove_file_metadata(const fs::path& full_server_path_obj) {
    std::string full_server_path = fs::weakly_canonical(full_server_path_obj).string();
    std::string sql = R"(
        UPDATE file_metadata 
        SET is_deleted = 1, deleted_timestamp = ? 
        WHERE file_path = ? AND is_deleted = 0;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        //sqlite3_bind_text(stmt, 1, full_server_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr))); // Set current time as deleted timestamp
        sqlite3_bind_text(stmt, 2, full_server_path.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to mark metadata as deleted for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
         std::cerr << "Failed to prepare metadata delete (tombstone) statement for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
    }
}


bool FileManager::update_metadata_after_rename(const fs::path& old_abs_path_obj, const fs::path& new_abs_path_obj, int user_id) {
    if (!fs::exists(new_abs_path_obj)) {
        return false;
    }

    if (fs::is_regular_file(new_abs_path_obj)) {
        remove_file_metadata(old_abs_path_obj);
        update_file_metadata(new_abs_path_obj, user_id);
        return true;
    }

    
    if (fs::is_directory(new_abs_path_obj)) {
        std::string old_path_prefix = fs::weakly_canonical(old_abs_path_obj).string();
        std::string new_path_prefix = fs::weakly_canonical(new_abs_path_obj).string();

        char* sql_update_children = sqlite3_mprintf(
            "UPDATE file_metadata SET file_path = REPLACE(file_path, %Q, %Q) WHERE file_path LIKE %Q || '/%%';",
            old_path_prefix.c_str(),
            new_path_prefix.c_str(),
            old_path_prefix.c_str()
        );

        if (sql_update_children) {
            db_.execute(sql_update_children);
            sqlite3_free(sql_update_children);
        }

        remove_file_metadata(old_abs_path_obj);
        return true;
    }

    return false; 
}