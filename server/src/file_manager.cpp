#include "file_manager.hpp"
#include "config.hpp"
#include <fstream>
#include <iostream>
#include <openssl/sha.h> // For checksums later
#include <iomanip>
#include <sstream>

FileManager::FileManager(Database& db) : db_(db) {}

// Helper to ensure user_path is within base_path and doesn't use ".." to escape.
// Returns the canonical absolute path if safe, otherwise an empty path.
fs::path FileManager::resolve_safe_path(const fs::path& base_path, const std::string& relative_user_path_str) {
    fs::path relative_user_path(relative_user_path_str); // Chuyển string sang path

    // Ngăn chặn ".." trong đường dẫn tương đối ngay từ đầu
    for (const auto& part : relative_user_path) {
        if (part == "..") {
            std::cerr << "Path traversal attempt detected in relative path: " << relative_user_path_str << std::endl;
            return {}; // Trả về đường dẫn rỗng nếu không hợp lệ
        }
    }

    fs::path full_path = base_path / relative_user_path;
    fs::path canonical_full_path;
    fs::path canonical_base_path;

    try {
        // weakly_canonical cố gắng chuẩn hóa đường dẫn ngay cả khi một số phần không tồn tại.
        // Tuy nhiên, để an toàn hơn, chúng ta nên đảm bảo base_path tồn tại.
        if (!fs::exists(base_path) || !fs::is_directory(base_path)) {
             std::cerr << "Base path does not exist or is not a directory: " << base_path << std::endl;
            return {};
        }
        canonical_base_path = fs::weakly_canonical(base_path); // Hoặc fs::canonical nếu base_path chắc chắn tồn tại
        
        // For full_path, weakly_canonical is appropriate as the final component might not exist yet (e.g., for upload/mkdir)
        canonical_full_path = fs::weakly_canonical(full_path);

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error during path canonicalization (" << full_path << "): " << e.what() << std::endl;
        return {};
    }

    // Kiểm tra xem đường dẫn đã chuẩn hóa có thực sự nằm trong base_path đã chuẩn hóa không
    std::string full_str = canonical_full_path.string();
    std::string base_str = canonical_base_path.string();

    if (full_str.rfind(base_str, 0) == 0) { // starts_with
        // Thêm một kiểm tra nữa để đảm bảo nó không chỉ là prefix mà còn là một sub-directory thực sự
        // Ví dụ: base = /data/user, path = /data/username (không được phép)
        //         base = /data/user, path = /data/user/docs (OK)
        //         base = /data/user, path = /data/user (OK - là chính nó)
        if (full_str.length() == base_str.length() || 
            (full_str.length() > base_str.length() && full_str[base_str.length()] == fs::path::preferred_separator) ) {
            return canonical_full_path;
        }
    }
    
    std::cerr << "Path " << full_path << " (canonical: " << canonical_full_path 
              << ") is outside of base " << base_path << " (canonical: " << canonical_base_path << ")" << std::endl;
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
            fs::remove_all(full_server_path); // Deletes directory and its contents
        } else {
            fs::remove(full_server_path);
        }
        std::cout << "Deleted: " << full_server_path << std::endl;
        remove_file_metadata(full_server_path);
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
    fs::path relative_path(relative_path_str);
    fs::path full_server_path = resolve_safe_path(server_base_path, relative_path);

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
                info.size = entry.file_size();
            } else {
            info.size = 0;
    }
            
            auto ftime = fs::last_write_time(entry.path());
            // The following conversion is C++20. For C++17, it's more complex.
            // Using chrono::file_clock::to_sys converts file_time to system_time.
            // Then convert system_time to time_t.
            // This line requires C++20 and <chrono>
            // info.last_modified = std::chrono::system_clock::to_time_t(std::chrono::utc_clock::to_sys(ftime)); // C++20 file_clock to system_clock to time_t
            
            // This is a common way but relies on file_time_type's epoch matching system_clock's epoch
            // Simpler C++17 compatible (might be less precise or portable with epoch)
            //info.last_modified = decltype(ftime)::clock::to_time_t(ftime);
            //auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            //        ftime - std::filesystem::file_clock::now() + std::chrono::system_clock::now()
            //    );
            //info.last_modified = std::chrono::system_clock::to_time_t(sctp);
            #if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && defined(__cpp_lib_filesystem) && __cpp_lib_filesystem >= 201703L && !defined(__APPLE__) // C++20 way (except on older Apple Clang)
    // This is the C++20 standard way if available and not problematic
                    info.last_modified = std::chrono::system_clock::to_time_t(std::chrono::utc_clock::to_sys(std::chrono::file_clock::to_utc(ftime))));
            #else // Fallback for C++17 or compilers with less complete C++20 chrono/fs
    // This relies on the epoch of file_clock being convertible or relatable to system_clock's epoch.
    // It's a common approach but less standardly guaranteed than the C++20 one.
                    auto system_time_point = std::chrono::system_clock::now() + (ftime - fs::file_clock::now());
                    info.last_modified = std::chrono::system_clock::to_time_t(system_time_point);
            #endif
            // TODO: Get checksum from metadata if available
            result.push_back(info);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error listing directory " << full_server_path << ": " << e.what() << std::endl;
    }
    return result;
}


std::string FileManager::calculate_checksum(const fs::path& file_path_obj) {
    // This was a placeholder before, let's make it use SHA256
    fs::path file_path = fs::weakly_canonical(file_path_obj); // Ensure path is good

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Checksum: Cannot open file: " << file_path << std::endl;
        return ""; // Or throw
    }

    unsigned char hash_digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256_ctx;
    SHA256_Init(&sha256_ctx);

    char buffer[8192]; // Read in chunks
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
    if (!fs::exists(full_server_path_obj) || fs::is_directory(full_server_path_obj)) {
        // For directories, you might want different metadata or skip checksum
        return; 
    }

    std::string full_server_path = fs::weakly_canonical(full_server_path_obj).string();
    std::string checksum = calculate_checksum(full_server_path_obj);



    auto ftime = fs::last_write_time(full_server_path_obj);

    //time_t last_modified = decltype(ftime)::clock::to_time_t(ftime);
    //auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    //            ftime - std::filesystem::file_clock::now() + std::chrono::system_clock::now()
    //        );
    //time_t last_modified = std::chrono::system_clock::to_time_t(sctp);
    #if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && defined(__cpp_lib_filesystem) && __cpp_lib_filesystem >= 201703L && !defined(__APPLE__)
        time_t last_modified = std::chrono::system_clock::to_time_t(std::chrono::utc_clock::to_sys(std::chrono::file_clock::to_utc(ftime))));
    #else
        auto system_time_point = std::chrono::system_clock::now() + (ftime - fs::file_clock::now());
        time_t last_modified = std::chrono::system_clock::to_time_t(system_time_point);
    #endif

    sqlite3_stmt* stmt;
    // UPSERT: Update if path exists, otherwise insert.
    std::string sql = R"(
        INSERT INTO file_metadata (file_path, checksum, last_modified, owner_user_id) 
        VALUES (?, ?, ?, ?)
        ON CONFLICT(file_path) DO UPDATE SET
        checksum = excluded.checksum,
        last_modified = excluded.last_modified,
        owner_user_id = excluded.owner_user_id,
        version = version + 1;
    )";

    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, full_server_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, checksum.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(last_modified));
        if (user_id != -1) {
            sqlite3_bind_int(stmt, 4, user_id);
        } else {
            sqlite3_bind_null(stmt, 4);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to update/insert metadata for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare metadata statement for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
    }
}

void FileManager::remove_file_metadata(const fs::path& full_server_path_obj) {
    std::string full_server_path = fs::weakly_canonical(full_server_path_obj).string();
    std::string sql = "DELETE FROM file_metadata WHERE file_path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, full_server_path.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to delete metadata for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare metadata delete statement for " << full_server_path << ": " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
    }
}


bool FileManager::update_metadata_after_rename(const fs::path& old_abs_path_obj, const fs::path& new_abs_path_obj, int user_id) {
    std::string old_path_str = fs::weakly_canonical(old_abs_path_obj).string();
    std::string new_path_str = fs::weakly_canonical(new_abs_path_obj).string();

    // Read existing metadata if any
    char* sql_get = sqlite3_mprintf("SELECT checksum, last_modified, version, owner_user_id FROM file_metadata WHERE file_path = %Q;", old_path_str.c_str());
    // ... (execute scalar or row callback to get old data) ...
    sqlite3_free(sql_get);
    
    // Delete old metadata entry
    remove_file_metadata(old_abs_path_obj);
    
    // Re-insert/update metadata for new path
    // If it was a directory, checksum might not apply or might be different
    if (fs::is_regular_file(new_abs_path_obj)) {
        update_file_metadata(new_abs_path_obj, user_id); // user_id might be the owner
    } else if (fs::is_directory(new_abs_path_obj)) {
        // Handle directory metadata if you store it (e.g. empty directories)
        // For now, directories might not have checksums in file_metadata, or timestamp is enough.
        // A simple approach for directories might be to just record their existence and timestamp.
        // The current update_file_metadata might skip directories or handle them differently.
    }
    return true; // Add error handling
}