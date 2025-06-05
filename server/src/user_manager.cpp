#include "user_manager.hpp"
#include "config.hpp"
#include <openssl/sha.h> // For SHA256
#include <openssl/rand.h> // For salt (if using salt with simple SHA256, bcrypt is better)
#include <iomanip>      // For std::hex, std::setw, std::setfill
#include <sstream>      // For std::ostringstream
#include <iostream>     // For std::cerr
#include <filesystem>   // For std::filesystem

namespace fs = std::filesystem;

UserManager::UserManager(Database& db) : db_(db) {}

// Basic SHA256 hashing (without salt, for simplicity here. Production should use bcrypt/scrypt/Argon2)
// Or at least SHA256 with a unique salt per user stored alongside the hash.
std::string UserManager::hash_password(const std::string& password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, password.c_str(), password.length());
    SHA256_Final(hash, &sha256);

    std::ostringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

bool UserManager::verify_password(const std::string& password, const std::string& stored_hash) {
    return hash_password(password) == stored_hash;
}

bool UserManager::create_user_directory(const std::string& username) {
    fs::path user_dir = fs::path(USER_DATA_ROOT) / username;
    try {
        if (!fs::exists(user_dir)) {
            if (fs::create_directories(user_dir)) {
                std::cout << "Created directory for user: " << username << " at " << user_dir << std::endl;
                return true;
            } else {
                std::cerr << "Failed to create directory for user: " << username << std::endl;
                return false;
            }
        }
        return true; // Directory already exists
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error creating directory for " << username << ": " << e.what() << std::endl;
        return false;
    }
}


std::optional<int> UserManager::register_user(const std::string& username, const std::string& password) {
    // Check if username exists
    std::string check_sql = "SELECT id FROM users WHERE username = ?;";
    sqlite3_stmt* stmt_check;
    if (sqlite3_prepare_v2(db_.get_db_handle(), check_sql.c_str(), -1, &stmt_check, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare check statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        return std::nullopt;
    }
    sqlite3_bind_text(stmt_check, 1, username.c_str(), -1, SQLITE_STATIC);
    
    bool exists = false;
    if (sqlite3_step(stmt_check) == SQLITE_ROW) {
        exists = true;
    }
    sqlite3_finalize(stmt_check);

    if (exists) {
        std::cerr << "Username '" << username << "' already exists." << std::endl;
        return std::nullopt;
    }

    // Create user directory
    if (!create_user_directory(username)) {
        return std::nullopt; // Directory creation failed
    }

    std::string hashed_password = hash_password(password);
    fs::path home_dir_path = fs::path(USER_DATA_ROOT) / username;
    std::string home_dir_str = home_dir_path.string(); // Store as string

    std::string insert_sql = "INSERT INTO users (username, password_hash, home_dir) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt_insert;
    if (sqlite3_prepare_v2(db_.get_db_handle(), insert_sql.c_str(), -1, &stmt_insert, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        return std::nullopt;
    }

    sqlite3_bind_text(stmt_insert, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_insert, 2, hashed_password.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_insert, 3, home_dir_str.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt_insert) != SQLITE_DONE) {
        std::cerr << "Failed to execute insert statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        sqlite3_finalize(stmt_insert);
        // Potentially roll back directory creation or clean up
        return std::nullopt;
    }
    sqlite3_finalize(stmt_insert);
    
    int user_id = sqlite3_last_insert_rowid(db_.get_db_handle());
    std::cout << "User " << username << " registered successfully with ID: " << user_id << std::endl;
    return user_id;
}




std::optional<int> UserManager::login_user(const std::string& username, const std::string& password) {
    std::string sql = "SELECT id, password_hash FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare login statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int user_id = sqlite3_column_int(stmt, 0);
        std::string stored_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sqlite3_finalize(stmt);
        if (verify_password(password, stored_hash)) {
            std::cout << "User " << username << " logged in successfully." << std::endl;
            return user_id;
        } else {
            std::cerr << "Incorrect password for user " << username << std::endl;
            return std::nullopt;
        }
    } else {
        sqlite3_finalize(stmt);
        std::cerr << "User " << username << " not found." << std::endl;
        return std::nullopt;
    }
}

bool UserManager::delete_user(int user_id) {
    // Optional: Get username to delete directory
    std::string get_user_sql = "SELECT username, home_dir FROM users WHERE id = ?;";
    sqlite3_stmt* stmt_get;
    std::string username_to_delete;
    std::string home_dir_to_delete;

    if (sqlite3_prepare_v2(db_.get_db_handle(), get_user_sql.c_str(), -1, &stmt_get, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_get, 1, user_id);
        if (sqlite3_step(stmt_get) == SQLITE_ROW) {
            username_to_delete = reinterpret_cast<const char*>(sqlite3_column_text(stmt_get, 0));
            home_dir_to_delete = reinterpret_cast<const char*>(sqlite3_column_text(stmt_get, 1));
        }
        sqlite3_finalize(stmt_get);
    }


    std::string sql = "DELETE FROM users WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
         std::cerr << "Failed to prepare delete statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        return false;
    }
    sqlite3_bind_int(stmt, 1, user_id);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::cerr << "Failed to delete user: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
    }
    sqlite3_finalize(stmt);

    if (success && !home_dir_to_delete.empty()) {
        try {
            fs::path user_dir(home_dir_to_delete);
            if (fs::exists(user_dir)) {
                uintmax_t n = fs::remove_all(user_dir); // remove_all deletes directory and its contents
                std::cout << "Removed user directory " << user_dir << " and " << n << " files/subdirectories." << std::endl;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Filesystem error deleting directory " << home_dir_to_delete << ": " << e.what() << std::endl;
            // DB entry deleted, but directory remains. This is an inconsistency.
            // Log this error prominently.
        }
    }
    return success;
}

std::optional<std::string> UserManager::get_user_home_dir(int user_id) {
    std::string sql = "SELECT home_dir FROM users WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get_db_handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_.get_db_handle()) << std::endl;
        return std::nullopt;
    }
    sqlite3_bind_int(stmt, 1, user_id);

    std::optional<std::string> home_dir;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        home_dir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return home_dir;
}

std::optional<int> UserManager::get_user_id_by_username(const std::string& username) {
    char* sql = sqlite3_mprintf("SELECT id FROM users WHERE username = %Q;", username.c_str());
    if (!sql) return std::nullopt;

    std::optional<std::string> id_str_opt = db_.execute_scalar(sql);
    sqlite3_free(sql);

    if (id_str_opt) {
        try {
            return std::stoi(*id_str_opt);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}