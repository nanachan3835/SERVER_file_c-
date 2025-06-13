#include "access_control.hpp"
#include "config.hpp"
#include <iostream>
#include <sqlite3.h>

AccessControlManager::AccessControlManager(Database& db, UserManager& user_manager)
    : db_(db), user_manager_(user_manager) {}

std::string AccessControlManager::permission_level_to_string(PermissionLevel perm) {
    switch (perm) {
        case PermissionLevel::READ: return "r";
        case PermissionLevel::READ_WRITE: return "rw";
        default: return ""; // Or throw
    }
}

PermissionLevel AccessControlManager::string_to_permission_level(const std::string& perm_str) {
    if (perm_str == "rw") return PermissionLevel::READ_WRITE;
    if (perm_str == "r") return PermissionLevel::READ;
    return PermissionLevel::NONE;
}

PermissionLevel AccessControlManager::get_permission(int user_id, const fs::path& absolute_server_resource_path_obj) {
    fs::path canonical_resource_path;
    try {
        canonical_resource_path = fs::weakly_canonical(absolute_server_resource_path_obj);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ACM: Filesystem error canonicalizing path " << absolute_server_resource_path_obj << ": " << e.what() << std::endl;
        return PermissionLevel::NONE;
    }
    std::string resource_path_str = canonical_resource_path.string();

    PermissionLevel highest_perm = PermissionLevel::NONE;

    // Khai báo user_home_path_for_check ở đây để nó có scope toàn hàm
    fs::path user_home_path_for_check; // Sẽ là rỗng nếu user không có home_dir hoặc có lỗi

    std::optional<std::string> user_home_dir_str_opt = user_manager_.get_user_home_dir(user_id);
    if (user_home_dir_str_opt) {
        try {
            user_home_path_for_check = fs::weakly_canonical(*user_home_dir_str_opt); // Gán giá trị
            if (resource_path_str.rfind(user_home_path_for_check.string(), 0) == 0) { // starts_with
                highest_perm = PermissionLevel::READ_WRITE;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "ACM: Filesystem error canonicalizing user home path " << *user_home_dir_str_opt << ": " << e.what() << std::endl;
            // user_home_path_for_check sẽ vẫn rỗng
        }
    }

    // 2. Check explicit user permissions on the exact path or its parents
    fs::path current_check_path_obj = canonical_resource_path;
    while (true) {
        char* sql_direct = sqlite3_mprintf(
            "SELECT access FROM permissions WHERE user_id = %d AND path = %Q;",
            user_id, current_check_path_obj.string().c_str()
        );
        if (!sql_direct) { /* Lỗi mprintf */ break; }

        std::optional<std::string> perm_str_direct_opt = db_.execute_scalar(sql_direct);
        sqlite3_free(sql_direct);

        if (perm_str_direct_opt) {
            PermissionLevel explicit_perm = string_to_permission_level(*perm_str_direct_opt);
            return explicit_perm; // Explicit permission overrides everything for this path and user
        }
        
        fs::path parent = current_check_path_obj.parent_path();
        if (parent == current_check_path_obj || // Reached filesystem root
            // Bây giờ user_home_path_for_check đã được khai báo ở scope này
            (!user_home_path_for_check.empty() && current_check_path_obj == user_home_path_for_check) || // Dừng nếu đã đến home của user
            current_check_path_obj.string() == Config::USER_DATA_ROOT || // Dừng nếu đến gốc của thư mục data/users
            current_check_path_obj.string() == Config::SHARED_DATA_ROOT ) { // Dừng nếu đến gốc của thư mục data/shared
            break;
        }
        current_check_path_obj = parent;
    }

    // 3. Check shared_access if the resource_path is within a shared storage
    fs::path shared_root_path_obj(Config::SHARED_DATA_ROOT); // Không cần fs::weakly_canonical ở đây vì nó là hằng số
    // Chỉ canonicalize khi so sánh với resource_path_str đã được canonicalized
    if (resource_path_str.rfind(shared_root_path_obj.string(), 0) == 0) { // starts_with, dùng path string để so sánh prefix
        fs::path current_shared_candidate = canonical_resource_path; // Bắt đầu từ resource path
        fs::path shared_root_resolved;
        try {
             shared_root_resolved = fs::weakly_canonical(shared_root_path_obj);
        } catch(const fs::filesystem_error& e){
             std::cerr << "ACM: Filesystem error canonicalizing SHARED_DATA_ROOT " << shared_root_path_obj << ": " << e.what() << std::endl;
             return highest_perm; // Không thể xác định quyền shared
        }


        while (current_shared_candidate != shared_root_resolved && current_shared_candidate.has_parent_path()) {
            char* sql_shared_check = sqlite3_mprintf(
                "SELECT sa.access FROM shared_access sa "
                "JOIN shared_storage ss ON sa.shared_storage_id = ss.id "
                "WHERE sa.user_id = %d AND ss.storage_path = %Q;", // storage_path trong DB nên là canonical
                user_id, current_shared_candidate.string().c_str()
            );
            if (!sql_shared_check) { break; }

            std::optional<std::string> perm_str_shared_opt = db_.execute_scalar(sql_shared_check);
            sqlite3_free(sql_shared_check);

            if (perm_str_shared_opt) {
                PermissionLevel shared_p_val = string_to_permission_level(*perm_str_shared_opt);
                if (shared_p_val > highest_perm) { // Shared permission có thể cao hơn home dir default (nếu chưa có explicit)
                    highest_perm = shared_p_val;
                }
                break; 
            }
            if (current_shared_candidate.parent_path() == current_shared_candidate) break; // Tránh lặp vô hạn
            current_shared_candidate = current_shared_candidate.parent_path();
        }
    }
    
    return highest_perm;
}


bool AccessControlManager::grant_explicit_permission(int user_id, const fs::path& absolute_server_resource_path_obj, PermissionLevel perm) {
    std::string perm_s = permission_level_to_string(perm);
    if (perm_s.empty() && perm != PermissionLevel::NONE) { // Allow granting "NONE" to explicitly revoke
         std::cerr << "ACM: Invalid permission level for grant_explicit_permission." << std::endl;
         return false;
    }
    if (perm == PermissionLevel::NONE) perm_s = "none"; // Store 'none' explicitly

    fs::path canonical_path;
    try {
        canonical_path = fs::weakly_canonical(absolute_server_resource_path_obj);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ACM: Invalid path for grant_explicit_permission: " << e.what() << std::endl;
        return false;
    }

    char* sql = sqlite3_mprintf(
        "INSERT INTO permissions (user_id, path, access) VALUES (%d, %Q, %Q) "
        "ON CONFLICT(user_id, path) DO UPDATE SET access = excluded.access;",
        user_id, canonical_path.string().c_str(), perm_s.c_str()
    );
    if (!sql) return false;

    bool success = db_.execute(sql);
    sqlite3_free(sql);
    return success;
}

bool AccessControlManager::revoke_explicit_permission(int user_id, const fs::path& absolute_server_resource_path_obj) {
    fs::path canonical_path;
    try {
        canonical_path = fs::weakly_canonical(absolute_server_resource_path_obj);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "ACM: Invalid path for revoke_explicit_permission: " << e.what() << std::endl;
        return false;
    }
    char* sql = sqlite3_mprintf(
        "DELETE FROM permissions WHERE user_id = %d AND path = %Q;",
        user_id, canonical_path.string().c_str()
    );
    if (!sql) return false;
    bool success = db_.execute(sql);
    sqlite3_free(sql);
    return success;
}

bool AccessControlManager::create_shared_storage(const std::string& storage_name, int creating_user_id) {
    fs::path storage_dir = fs::path(Config::SHARED_DATA_ROOT) / storage_name;
    fs::path canonical_storage_path;

    try {
        if (!fs::exists(Config::SHARED_DATA_ROOT)) {
            fs::create_directories(Config::SHARED_DATA_ROOT);
        }
        if (!fs::exists(storage_dir)) {
            if (!fs::create_directories(storage_dir)) {
                std::cerr << "Failed to create physical directory for shared storage: " << storage_dir << std::endl;
                return false;
            }
        }
        canonical_storage_path = fs::weakly_canonical(storage_dir);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error creating shared storage directory " << storage_dir << ": " << e.what() << std::endl;
        return false;
    }

    char* sql = sqlite3_mprintf(
        "INSERT OR IGNORE INTO shared_storage (storage_name, storage_path) VALUES (%Q, %Q);",
        storage_name.c_str(), canonical_storage_path.string().c_str()
    );
     if (!sql) { std::cerr << "ACM: Mprintf failed for create_shared_storage." << std::endl; return false;}

    bool success = db_.execute(sql);
    sqlite3_free(sql);

    if (!success) {
        std::cerr << "Failed to create shared storage DB entry for: " << storage_name << std::endl;
        return false;
    }
    // Grant creator RW access
    return grant_shared_storage_access(creating_user_id, storage_name, PermissionLevel::READ_WRITE);
}

std::optional<fs::path> AccessControlManager::get_shared_storage_path(const std::string& storage_name) {
    char* sql = sqlite3_mprintf(
        "SELECT storage_path FROM shared_storage WHERE storage_name = %Q;",
        storage_name.c_str()
    );
    if (!sql) return std::nullopt;

    auto path_str_opt = db_.execute_scalar(sql);
    sqlite3_free(sql);

    if (path_str_opt) {
        try {
            return fs::weakly_canonical(*path_str_opt);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "ACM: Invalid shared storage path in DB for " << storage_name << ": " << *path_str_opt << std::endl;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool AccessControlManager::grant_shared_storage_access(int user_id, const std::string& storage_name, PermissionLevel perm) {
    std::string perm_s = permission_level_to_string(perm);
    if (perm_s.empty() && perm != PermissionLevel::NONE) {
        std::cerr << "ACM: Invalid permission level for grant_shared_storage_access." << std::endl;
        return false;
    }
     if (perm == PermissionLevel::NONE) perm_s = "none";


    char* sql_get_id = sqlite3_mprintf("SELECT id FROM shared_storage WHERE storage_name = %Q;", storage_name.c_str());
    if(!sql_get_id) { std::cerr << "ACM: Mprintf failed for get_id." << std::endl; return false;}
    auto storage_id_str_opt = db_.execute_scalar(sql_get_id);
    sqlite3_free(sql_get_id);

    if (!storage_id_str_opt) {
        std::cerr << "Shared storage '" << storage_name << "' not found." << std::endl;
        return false;
    }
    int storage_id = std::stoi(*storage_id_str_opt);

    char* sql_grant = sqlite3_mprintf(
        "INSERT INTO shared_access (shared_storage_id, user_id, access) VALUES (%d, %d, %Q) "
        "ON CONFLICT(shared_storage_id, user_id) DO UPDATE SET access = excluded.access;",
        storage_id, user_id, perm_s.c_str()
    );
    if (!sql_grant) { std::cerr << "ACM: Mprintf failed for grant." << std::endl; return false;}

    bool success = db_.execute(sql_grant);
    sqlite3_free(sql_grant);
    return success;
}

bool AccessControlManager::revoke_shared_storage_access(int user_id, const std::string& storage_name) {
    char* sql_get_id = sqlite3_mprintf("SELECT id FROM shared_storage WHERE storage_name = %Q;", storage_name.c_str());
    if(!sql_get_id) { std::cerr << "ACM: Mprintf failed for get_id on revoke." << std::endl; return false;}
    auto storage_id_str_opt = db_.execute_scalar(sql_get_id);
    sqlite3_free(sql_get_id);
    
    if (!storage_id_str_opt) {
        return true; 
    }
    int storage_id = std::stoi(*storage_id_str_opt);

    char* sql_revoke = sqlite3_mprintf(
        "DELETE FROM shared_access WHERE shared_storage_id = %d AND user_id = %d;",
        storage_id, user_id
    );
    if (!sql_revoke) { std::cerr << "ACM: Mprintf failed for revoke." << std::endl; return false;}

    bool success = db_.execute(sql_revoke);
    sqlite3_free(sql_revoke);
    return success;
}