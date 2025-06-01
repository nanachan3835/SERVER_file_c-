#pragma once

#include "db.hpp"
#include "user_manager.hpp" // To get user's home directory
#include <string>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

enum class PermissionLevel { // Renamed from Permission for clarity
    NONE,
    READ,
    READ_WRITE
};

class AccessControlManager {
public:
    AccessControlManager(Database& db, UserManager& user_manager); // Added UserManager

    // Check permission for a user on a specific absolute server path
    PermissionLevel get_permission(int user_id, const fs::path& absolute_server_resource_path);

    // Grant permission (typically for explicit sharing, not home dir)
    bool grant_explicit_permission(int user_id, const fs::path& absolute_server_resource_path, PermissionLevel perm);
    bool grant_shared_storage_access(int user_id, const std::string& storage_name, PermissionLevel perm);

    // Revoke permission
    bool revoke_explicit_permission(int user_id, const fs::path& absolute_server_resource_path);
    bool revoke_shared_storage_access(int user_id, const std::string& storage_name);

    // Helper for shared storage
    bool create_shared_storage(const std::string& storage_name, int creating_user_id); // User who creates it gets RW initially
    std::optional<fs::path> get_shared_storage_path(const std::string& storage_name);
    PermissionLevel string_to_permission_level(const std::string& perm_str);

private:
    Database& db_;
    UserManager& user_manager_; // To resolve user home directories

    std::string permission_level_to_string(PermissionLevel perm);
    
};