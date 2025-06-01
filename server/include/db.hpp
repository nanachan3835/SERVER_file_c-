#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>

class Database {
public:
    Database(const std::string& db_path);
    ~Database();

    bool open(const std::string& db_path);
    void close();
    bool execute(const std::string& sql);
    // For SELECT statements that return multiple rows
    bool execute_query(const std::string& sql, 
                       std::function<void(sqlite3_stmt*)> row_callback);
    // For SELECT statements that expect a single value or row
    std::optional<std::string> execute_scalar(const std::string& sql);
    
    sqlite3* get_db_handle(); // Be careful with direct access
    bool initialize_schema();

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;
};