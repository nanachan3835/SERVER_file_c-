#include "db.hpp"
#include "config.hpp" // For DATABASE_PATH
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <filesystem>
namespace fs = std::filesystem;



Database::Database(const std::string& db_path) : db_path_(db_path) {
    if (!open(db_path_)) {
        // Consider throwing an exception or setting an error state
        std::cerr << "Failed to open database: " << db_path_ << std::endl;
    }
}

Database::~Database() {
    close();
}

bool Database::open(const std::string& db_path) {
    // Create directory if it doesn't exist
    fs::path path_obj(db_path);
    if (path_obj.has_parent_path()) {
        fs::create_directories(path_obj.parent_path());
    }

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return false;
    }
    // Enable foreign key constraints
    execute("PRAGMA foreign_keys = ON;");
    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execute(const std::string& sql) {
    if (!db_) return false;
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool Database::execute_query(const std::string& sql, 
                           std::function<void(sqlite3_stmt*)> row_callback) {
    if (!db_) return false;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        row_callback(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

std::optional<std::string> Database::execute_scalar(const std::string& sql) {
    if (!db_) return std::nullopt;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for scalar: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }

    std::optional<std::string> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}


sqlite3* Database::get_db_handle() {
    return db_;
}

bool Database::initialize_schema() {
    if (!db_) return false;

    std::string users_table_sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            home_dir TEXT NOT NULL
        );
    )";

    std::string permissions_table_sql = R"(
        CREATE TABLE IF NOT EXISTS permissions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            path TEXT NOT NULL,
            access TEXT NOT NULL, -- 'r', 'rw'
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        );
    )";
     // Note: path could be relative to user's home or shared dir, or absolute within data root.
     // Need to define this convention. For now, assume relative to a context (user home or shared dir).

    std::string shared_storage_table_sql = R"(
        CREATE TABLE IF NOT EXISTS shared_storage (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            storage_name TEXT UNIQUE NOT NULL, -- e.g., 'project_alpha'
            storage_path TEXT UNIQUE NOT NULL  -- e.g., '/shared/project_alpha'
        );
    )";

    std::string shared_access_table_sql = R"(
        CREATE TABLE IF NOT EXISTS shared_access (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            shared_storage_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            access TEXT NOT NULL, -- 'r', 'rw'
            FOREIGN KEY (shared_storage_id) REFERENCES shared_storage(id) ON DELETE CASCADE,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
            UNIQUE (shared_storage_id, user_id) -- A user has one type of access per shared storage
        );
    )";
    
    // Optional: Table for file metadata (checksums, versions, timestamps for sync)
    std::string file_metadata_table_sql = R"(
        CREATE TABLE IF NOT EXISTS file_metadata (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT UNIQUE NOT NULL, 
            checksum TEXT,
            last_modified INTEGER, 
            version INTEGER DEFAULT 1,
            owner_user_id INTEGER, 
            is_directory INTEGER NOT NULL DEFAULT 0, 
            is_deleted INTEGER NOT NULL DEFAULT 0,   
            deleted_timestamp INTEGER,   
            FOREIGN KEY (owner_user_id) REFERENCES users(id) ON DELETE SET NULL
        );
    )";
    std::string metadata_index_sql = R"(
    CREATE INDEX IF NOT EXISTS idx_file_metadata_path_deleted 
    ON file_metadata (file_path, is_deleted);
)";

    bool success = true;
    success &= execute(users_table_sql);
    success &= execute(permissions_table_sql);
    success &= execute(shared_storage_table_sql);
    success &= execute(shared_access_table_sql);
    success &= execute(file_metadata_table_sql);

    if (!success) {
        std::cerr << "Failed to initialize database schema." << std::endl;
    }
    return success;
}