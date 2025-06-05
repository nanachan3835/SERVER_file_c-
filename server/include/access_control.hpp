#pragma once

#include "db.hpp"
#include "user_manager.hpp"
#include <string>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

enum class PermissionLevel {
    NONE,
    READ,
    READ_WRITE
};

class AccessControlManager {
public:
    AccessControlManager(Database& db, UserManager& user_manager);
    PermissionLevel get_permission(int user_id, const fs::path& absolute_server_resource_path);
    bool grant_explicit_permission(int user_id, const fs::path& absolute_server_resource_path, PermissionLevel perm);
    bool grant_shared_storage_access(int user_id, const std::string& storage_name, PermissionLevel perm);
    bool revoke_explicit_permission(int user_id, const fs::path& absolute_server_resource_path);
    bool revoke_shared_storage_access(int user_id, const std::string& storage_name);
    bool create_shared_storage(const std::string& storage_name, int creating_user_id);
    std::optional<fs::path> get_shared_storage_path(const std::string& storage_name);

    PermissionLevel string_to_permission_level(const std::string& perm_str); // ĐÃ LÀ PUBLIC

private:
    Database& db_;
    UserManager& user_manager_;
    std::string permission_level_to_string(PermissionLevel perm);
};