// #include "sync_manager.hpp"
// #include "config.hpp" // If needed for paths, but server_sync_root_path should be absolute
// #include <sqlite3.h>
// #include <iostream>     // For debugging
// #include <Poco/File.h>  // For Poco::File if directly accessing FS (though prefer DB)
// #include <Poco/DateTimeFormat.h> // For formatting timestamps if debugging
// SyncManager::SyncManager(Database& db, FileManager& file_manager)

//     : db_(db), file_manager_(file_manager) {}

// //     std::map<std::string, ServerSyncFileInfo> SyncManager::get_server_file_states(
// //     int user_id,
// //     const Poco::Path& server_sync_root_path
// // )
// {
//     std::map<std::string, ServerSyncFileInfo> server_states;
//     // The file_path in file_metadata is the FULL absolute path on the server.
//     // We need to fetch records that are within the server_sync_root_path
//     // and then convert their full paths to relative paths for comparison.

//     // Sanitize server_sync_root_path to ensure it ends with a slash for LIKE pattern
//     std::string root_path_str = server_sync_root_path.toString();
//     if (root_path_str.empty() || root_path_str.back() != Poco::Path::separator()) {
//         root_path_str += Poco::Path::separator();
//     }
    
//     // SQL to fetch files under the specific root_path, owned by the user or in shared context
//     // This query assumes 'file_path' in 'file_metadata' is the key.
//     // We might need a more complex query if shared files are involved and permissions are not simply by owner_user_id.
//     // For user's home directory, owner_user_id = user_id.
//     // For shared directories, owner_user_id might be NULL or a special group ID, and access is via shared_access table.
//     // This simple version focuses on user's own files for now.
//     // A more robust solution would involve checking AccessControlManager here or having metadata table link to permissions.
    
//     char* sql_query = sqlite3_mprintf(
//         "SELECT file_path, checksum, last_modified, version, owner_user_id, is_directory FROM file_metadata "
//         "WHERE file_path LIKE %Q || '%%' AND is_deleted = 0;", // THÊM ĐIỀU KIỆN is_deleted = 0
//         root_path_str.c_str()
//     );
//     // Note: The LIKE condition `root_path_str || '%%'` will fetch all files *under* this root.
//     // The owner_user_id = %d part is a simplification. For shared folders, you'd check shared_access.
//     // For now, let's assume owner_user_id is sufficient for files directly in user's scope or shared items they have some link to.

//     if (!sql_query) {
//         std::cerr << "Failed to allocate memory for SQL query in get_server_file_states." << std::endl;
//         return server_states;
//     }

//     db_.execute_query(sql_query, [&](sqlite3_stmt* stmt) {
//         std::string full_path_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

//         if (acm.get_permission(user_id, fs::path(full_path_str)) < PermissionLevel::READ) {
//             return; // Bỏ qua file này nếu không có quyền đọc
//         }



//         Poco::Path full_server_path(full_path_str);
//         // Make path relative to server_sync_root_path
//         // Example: server_sync_root_path = /data/users/alice/
//         //          full_server_path      = /data/users/alice/docs/report.pdf
//         //          relative_path         = docs/report.pdf

//         std::string relative_path_str;
//         if (full_server_path.toString().rfind(server_sync_root_path.toString(), 0) == 0) { // starts_with
//             relative_path_str = full_server_path.toString().substr(server_sync_root_path.toString().length());
//         } else {
//             // This shouldn't happen if LIKE query is correct and paths are canonical
//             std::cerr << "Warning: Mismatch between LIKE query and path prefix for " << full_path_str
//                       << " and root " << server_sync_root_path.toString() << std::endl;
//             return; // Skip this entry
//         }
//         // Normalize separators for consistency if needed, though Poco::Path handles it.
//         Poco::Path temp_relative_path(relative_path_str);
//         relative_path_str = temp_relative_path.toString(Poco::Path::PATH_UNIX); // Ensure Unix separators


//         ServerSyncFileInfo sfi;
//         sfi.full_path_on_server = full_path_str;
//         sfi.relative_path = relative_path_str;
//         sfi.checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
//         sfi.last_modified = Poco::Timestamp::fromEpochTime(static_cast<std::time_t>(sqlite3_column_int64(stmt, 2)));
//         sfi.version = sqlite3_column_int(stmt, 3);
//         sfi.owner_user_id = sqlite3_column_int(stmt, 4); // Could be NULL
        
//         server_states[sfi.relative_path] = sfi;
//     });

//     sqlite3_free(sql_query);
//     return server_states;
// }


// std::vector<SyncOperation> SyncManager::determine_sync_actions(
//     int user_id,
//     const Poco::Path& server_sync_root_path,
//     const std::vector<ClientSyncFileInfo>& client_files,
//     AccessControlManager& acm)

// {
//     std::vector<SyncOperation> operations;
//     //std::map<std::string, ServerSyncFileInfo> all_server_files = get_server_file_states(user_id, server_sync_root_path);
    
//     // Bước 2: Lọc lại danh sách file mà user hiện tại có quyền truy cập
//     std::map<std::string, ServerSyncFileInfo> server_file_states =  get_server_file_states(user_id, server_sync_root_path, acm);
//     //AccessControlManager acm(db_, user_manager_);
    

//     // for (const auto& pair : all_server_files) {
//     //     // Giả sử bạn có thể truyền AccessControlManager vào hàm này
//     //     // Hoặc bạn cần inject các dependency cần thiết để tạo nó
//     //     if (acm.get_permission(user_id, fs::path(pair.second.full_path_on_server)) >= PermissionLevel::READ) {
//     //         server_file_states[pair.first] = pair.second;
//     //     }
//     // }
    
    
//     std::map<std::string, bool> client_files_processed;

//     // Iterate through client files
//     for (const auto& client_file : client_files) {



//         std::string client_relative_path = Poco::Path(client_file.relative_path).toString(Poco::Path::PATH_UNIX);
//         client_files_processed[client_relative_path] = true;
//         auto it = server_file_states.find(client_relative_path);
// // đã xóa ở client
//         if (client_file.is_deleted) {
//         if (it != server_file_states.end()) {
//             operations.emplace_back(SyncActionType::DELETE_ON_SERVER, client_relative_path);
//         } else {
//             operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
//         }
//         continue; // Đã xử lý xong, chuyển sang mục tiếp theo
//     }

//     // chưa xóa
//         if (client_file.is_directory) {
//         if (it == server_file_states.end()) {
//             // Thư mục chỉ có ở client -> Yêu cầu server tạo
//             operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
//         } else {
//             // Cả hai đều có thư mục -> Không làm gì
//             operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
//         }
//         continue; // Đã xử lý xong, chuyển sang mục tiếp theo
//     }



//         if (client_file.is_deleted) {
//             if (it != server_file_states.end()) {
//                 // Client đã xóa, server vẫn còn -> Yêu cầu server xóa
//                 operations.emplace_back(SyncActionType::DELETE_ON_SERVER, client_relative_path);
//             } else {
//                 // Cả hai đều đã xóa (hoặc server chưa từng có) -> Không làm gì
//                 operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
//             }
//             continue; // Chuyển sang file tiếp theo
//         }
//         if (it != server_file_states.end()) {
//             // File exists on both client and server
//             const ServerSyncFileInfo& server_file = it->second;

//             Poco::Timestamp client_ts_sec(Poco::Timestamp::fromEpochTime(client_file.last_modified.epochTime()));
//             Poco::Timestamp server_ts_sec(Poco::Timestamp::fromEpochTime(server_file.last_modified.epochTime()));

//             bool checksums_match = (client_file.checksum == server_file.checksum);
//             bool timestamps_match = (client_ts_sec == server_ts_sec);
            
//             std::cout << "--- Comparing: " << client_relative_path << " ---\n";
//             std::cout << "Client TS: " << client_file.last_modified.epochTime() << " | CS: " << client_file.checksum << "\n";
//             std::cout << "Server TS: " << server_file.last_modified.epochTime() << " | CS: " << server_file.checksum << "\n";
//             std::cout << "Checksums match: " << std::boolalpha << checksums_match << "\n";
//             std::cout << "Timestamps (sec) match: " << std::boolalpha << timestamps_match << "\n";

//             if (checksums_match) {
//                 // Nếu checksum khớp, file chắc chắn giống nhau. Không cần làm gì cả.
//                 // Điều này cũng giúp tự sửa lỗi nếu timestamp bị lệch.
//                 operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
//             } else {
//                 // Checksum khác nhau, file chắc chắn đã bị thay đổi ở một hoặc cả hai nơi.
//                 if (timestamps_match) {
//                     // Timestamps giống hệt nhau nhưng checksum khác -> CONFLICT
//                     // Chiến lược: Server thắng. Client cần download phiên bản của server.
//                     operations.emplace_back(SyncActionType::CONFLICT_SERVER_WINS, client_relative_path);
//                     std::cout << "CONFLICT (TS same, CS diff): " << client_relative_path << ". Server wins." << std::endl;
//                 } else if (client_ts_sec > server_ts_sec) {
//                     // Client mới hơn -> UPLOAD
//                     operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
//                 } else { // server_ts_sec > client_ts_sec
//                     // Server mới hơn -> DOWNLOAD
//                     operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, client_relative_path);
//                 }

//             }
            
//         } else {
//             // File exists on client, but not on server -> UPLOAD
//             operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
//         }

    
//     }

//     // Iterate through server files to find those not present on client -> DOWNLOAD
//     for (const auto& pair : server_file_states) {
//         const std::string& server_relative_path = pair.first;
//         if (client_files_processed.find(server_relative_path) == client_files_processed.end()) {
//             operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, server_relative_path);
//         }
//     }

//     return operations;
// }



#include "sync_manager.hpp"
#include "config.hpp"
#include <sqlite3.h>
#include <iostream>
#include <Poco/File.h>
#include <Poco/DateTimeFormat.h>

SyncManager::SyncManager(Database& db, FileManager& file_manager)
    : db_(db), file_manager_(file_manager) {}

// CHỈ GIỮ LẠI PHIÊN BẢN HÀM NÀY
// Nó nhận AccessControlManager để thực hiện lọc quyền bên trong.
std::map<std::string, ServerSyncFileInfo> SyncManager::get_server_file_states(
    int user_id,
    const Poco::Path& server_sync_root_path,
    AccessControlManager& acm)
{
    std::map<std::string, ServerSyncFileInfo> server_states;
    std::string root_path_str = server_sync_root_path.toString();
    if (root_path_str.empty() || root_path_str.back() != Poco::Path::separator()) {
        root_path_str += Poco::Path::separator();
    }

    // Truy vấn tất cả các mục chưa bị xóa trong đường dẫn gốc
    char* sql_query = sqlite3_mprintf(
        "SELECT file_path, checksum, last_modified, version, owner_user_id, is_directory FROM file_metadata "
        "WHERE file_path LIKE %Q || '%%' AND is_deleted = 0;",
        root_path_str.c_str()
    );

    if (!sql_query) {
        std::cerr << "Failed to allocate memory for SQL query in get_server_file_states." << std::endl;
        return server_states;
    }

    db_.execute_query(sql_query, [&](sqlite3_stmt* stmt) {
        std::string full_path_str(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));

        // LỌC QUYỀN NGAY TẠI ĐÂY
        if (acm.get_permission(user_id, fs::path(full_path_str)) < PermissionLevel::READ) {
            return; // Bỏ qua file này nếu không có quyền đọc
        }

        Poco::Path full_server_path(full_path_str);
        std::string relative_path_str;
        if (full_server_path.toString().rfind(server_sync_root_path.toString(), 0) == 0) {
            relative_path_str = full_server_path.toString().substr(server_sync_root_path.toString().length());
        } else {
            std::cerr << "Warning: Mismatch between LIKE query and path prefix for " << full_path_str
                      << " and root " << server_sync_root_path.toString() << std::endl;
            return;
        }
        
        Poco::Path temp_relative_path(relative_path_str);
        relative_path_str = temp_relative_path.toString(Poco::Path::PATH_UNIX);

        ServerSyncFileInfo sfi;
        sfi.full_path_on_server = full_path_str;
        sfi.relative_path = relative_path_str;
        sfi.checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sfi.last_modified = Poco::Timestamp::fromEpochTime(static_cast<std::time_t>(sqlite3_column_int64(stmt, 2)));
        sfi.version = sqlite3_column_int(stmt, 3);
        sfi.owner_user_id = sqlite3_column_int(stmt, 4);
        sfi.is_directory = (sqlite3_column_int(stmt, 5) == 1); // Lấy thông tin thư mục

        server_states[sfi.relative_path] = sfi;
    });

    sqlite3_free(sql_query);
    return server_states;
}


// VIẾT LẠI HOÀN TOÀN HÀM NÀY
std::vector<SyncOperation> SyncManager::determine_sync_actions(
    int user_id,
    const Poco::Path& server_sync_root_path,
    const std::vector<ClientSyncFileInfo>& client_files,
    AccessControlManager& acm)
{
    std::vector<SyncOperation> operations;
    
    // Bước 1: Lấy danh sách các file/thư mục trên server mà user có quyền truy cập
    std::map<std::string, ServerSyncFileInfo> server_file_states = get_server_file_states(user_id, server_sync_root_path, acm);
    
    std::map<std::string, bool> client_files_processed;

    // Bước 2: Duyệt qua manifest của client để so sánh
    for (const auto& client_file : client_files) {
        std::string client_relative_path = Poco::Path(client_file.relative_path).toString(Poco::Path::PATH_UNIX);
        client_files_processed[client_relative_path] = true;
        auto it = server_file_states.find(client_relative_path);

        // Ưu tiên 1: Xử lý các mục đã bị xóa ở client (Tombstone)
        if (client_file.is_deleted) {
            if (it != server_file_states.end()) {
                // Client đã xóa, server vẫn còn -> Yêu cầu server xóa
                operations.emplace_back(SyncActionType::DELETE_ON_SERVER, client_relative_path);
            } else {
                // Cả hai đều đã xóa (hoặc server chưa từng có) -> Không làm gì
                operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
            }
            continue; // Đã xử lý xong, chuyển sang mục tiếp theo
        }

        // Ưu tiên 2: Xử lý các thư mục (chưa bị xóa)
        if (client_file.is_directory) {
            if (it == server_file_states.end()) {
                // Thư mục chỉ có ở client -> Yêu cầu server tạo
                operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
            } else {
                // Cả hai đều có thư mục -> Không làm gì
                operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
            }
            continue; // Đã xử lý xong, chuyển sang mục tiếp theo
        }

        // Ưu tiên 3: Xử lý các file (chưa bị xóa)
        if (it != server_file_states.end()) {
            // File tồn tại ở cả hai nơi
            const ServerSyncFileInfo& server_file = it->second;

            Poco::Timestamp client_ts_sec(Poco::Timestamp::fromEpochTime(client_file.last_modified.epochTime()));
            Poco::Timestamp server_ts_sec(Poco::Timestamp::fromEpochTime(server_file.last_modified.epochTime()));

            bool checksums_match = (client_file.checksum == server_file.checksum);
            
            if (checksums_match) {
                operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
            } else {
                if (client_ts_sec == server_ts_sec) {
                    operations.emplace_back(SyncActionType::CONFLICT_SERVER_WINS, client_relative_path);
                } else if (client_ts_sec > server_ts_sec) {
                    operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
                } else {
                    operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, client_relative_path);
                }
            }
        } else {
            // File chỉ có ở client (và chưa bị xóa) -> UPLOAD
            operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
        }
    }

    // Bước 3: Duyệt qua các file trên server để tìm những file client không có
    for (const auto& pair : server_file_states) {
        const std::string& server_relative_path = pair.first;
        if (client_files_processed.find(server_relative_path) == client_files_processed.end()) {
            // File/thư mục chỉ có trên server -> Yêu cầu client tải về/tạo
            operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, server_relative_path);
        }
    }

    return operations;
}

