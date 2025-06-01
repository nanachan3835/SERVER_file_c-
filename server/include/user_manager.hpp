#pragma once

#include "db.hpp"
#include <string>
#include <optional>

class UserManager {
public:
    UserManager(Database& db);

    // Returns user_id on success
    std::optional<int> register_user(const std::string& username, const std::string& password);
    // Returns user_id on success
    std::optional<int> login_user(const std::string& username, const std::string& password);
    bool delete_user(int user_id); // Or by username
    std::optional<std::string> get_user_home_dir(int user_id);
    std::optional<int> get_user_id_by_username(const std::string& username);

private:
    Database& db_;
    
    std::string hash_password(const std::string& password);
    bool verify_password(const std::string& password, const std::string& hash);
    bool create_user_directory(const std::string& username);
};