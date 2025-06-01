#include "sync_manager.hpp"
#include "config.hpp" // If needed for paths, but server_sync_root_path should be absolute
#include <sqlite3.h>
#include <iostream>     // For debugging
#include <Poco/File.h>  // For Poco::File if directly accessing FS (though prefer DB)
#include <Poco/DateTimeFormat.h> // For formatting timestamps if debugging

SyncManager::SyncManager(Database& db, FileManager& file_manager)
    : db_(db), file_manager_(file_manager) {}

std::map<std::string, ServerSyncFileInfo> SyncManager::get_server_file_states(
    int user_id,
    const Poco::Path& server_sync_root_path)
{
    std::map<std::string, ServerSyncFileInfo> server_states;
    // The file_path in file_metadata is the FULL absolute path on the server.
    // We need to fetch records that are within the server_sync_root_path
    // and then convert their full paths to relative paths for comparison.

    // Sanitize server_sync_root_path to ensure it ends with a slash for LIKE pattern
    std::string root_path_str = server_sync_root_path.toString();
    if (root_path_str.empty() || root_path_str.back() != Poco::Path::separator()) {
        root_path_str += Poco::Path::separator();
    }
    
    // SQL to fetch files under the specific root_path, owned by the user or in shared context
    // This query assumes 'file_path' in 'file_metadata' is the key.
    // We might need a more complex query if shared files are involved and permissions are not simply by owner_user_id.
    // For user's home directory, owner_user_id = user_id.
    // For shared directories, owner_user_id might be NULL or a special group ID, and access is via shared_access table.
    // This simple version focuses on user's own files for now.
    // A more robust solution would involve checking AccessControlManager here or having metadata table link to permissions.
    
    char* sql_query = sqlite3_mprintf(
        "SELECT file_path, checksum, last_modified, version, owner_user_id FROM file_metadata "
        "WHERE file_path LIKE %Q || '%%' AND (owner_user_id = %d OR owner_user_id IS NULL);", // owner_user_id IS NULL for shared files not explicitly owned
        root_path_str.c_str(), user_id
    );
    // Note: The LIKE condition `root_path_str || '%%'` will fetch all files *under* this root.
    // The owner_user_id = %d part is a simplification. For shared folders, you'd check shared_access.
    // For now, let's assume owner_user_id is sufficient for files directly in user's scope or shared items they have some link to.

    if (!sql_query) {
        std::cerr << "Failed to allocate memory for SQL query in get_server_file_states." << std::endl;
        return server_states;
    }

    db_.execute_query(sql_query, [&](sqlite3_stmt* stmt) {
        std::string full_path_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        Poco::Path full_server_path(full_path_str);
        
        // Make path relative to server_sync_root_path
        // Example: server_sync_root_path = /data/users/alice/
        //          full_server_path      = /data/users/alice/docs/report.pdf
        //          relative_path         = docs/report.pdf
        std::string relative_path_str;
        if (full_server_path.toString().rfind(server_sync_root_path.toString(), 0) == 0) { // starts_with
            relative_path_str = full_server_path.toString().substr(server_sync_root_path.toString().length());
        } else {
            // This shouldn't happen if LIKE query is correct and paths are canonical
            std::cerr << "Warning: Mismatch between LIKE query and path prefix for " << full_path_str
                      << " and root " << server_sync_root_path.toString() << std::endl;
            return; // Skip this entry
        }
        // Normalize separators for consistency if needed, though Poco::Path handles it.
        Poco::Path temp_relative_path(relative_path_str);
        relative_path_str = temp_relative_path.toString(Poco::Path::PATH_UNIX); // Ensure Unix separators


        ServerSyncFileInfo sfi;
        sfi.full_path_on_server = full_path_str;
        sfi.relative_path = relative_path_str;
        sfi.checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sfi.last_modified = Poco::Timestamp::fromEpochTime(static_cast<std::time_t>(sqlite3_column_int64(stmt, 2)));
        sfi.version = sqlite3_column_int(stmt, 3);
        sfi.owner_user_id = sqlite3_column_int(stmt, 4); // Could be NULL
        
        server_states[sfi.relative_path] = sfi;
    });

    sqlite3_free(sql_query);
    return server_states;
}


std::vector<SyncOperation> SyncManager::determine_sync_actions(
    int user_id,
    const Poco::Path& server_sync_root_path,
    const std::vector<ClientSyncFileInfo>& client_files)
{
    std::vector<SyncOperation> operations;
    std::map<std::string, ServerSyncFileInfo> server_file_states = get_server_file_states(user_id, server_sync_root_path);
    std::map<std::string, bool> client_files_processed; // To track which client files we've seen

    // Debug: Print server root path
    // std::cout << "SyncManager: Server sync root: " << server_sync_root_path.toString() << std::endl;

    // Debug: Print initial server states
    // for (const auto& pair : server_file_states) {
    //    std::cout << "Server has: " << pair.first << " (TS: " << Poco::DateTimeFormat::ISO8601_FORMAT.format(pair.second.last_modified) << ", CS: " << pair.second.checksum << ")" << std::endl;
    // }

    // Iterate through client files
    for (const auto& client_file : client_files) {
        std::string client_relative_path = Poco::Path(client_file.relative_path).toString(Poco::Path::PATH_UNIX);
        client_files_processed[client_relative_path] = true;

        // std::cout << "Client has: " << client_relative_path << " (TS: " << Poco::DateTimeFormat::ISO8601_FORMAT.format(client_file.last_modified) << ", CS: " << client_file.checksum << ")" << std::endl;


        auto it = server_file_states.find(client_relative_path);
        if (it != server_file_states.end()) {
            // File exists on both client and server
            const ServerSyncFileInfo& server_file = it->second;

            // Compare timestamps (Poco::Timestamp comparison works directly)
            if (client_file.last_modified == server_file.last_modified) {
                if (client_file.checksum == server_file.checksum) {
                    operations.emplace_back(SyncActionType::NO_ACTION, client_relative_path);
                } else {
                    // Timestamps same, checksums differ: CONFLICT
                    // Strategy: Server wins for now (client downloads server's version)
                    operations.emplace_back(SyncActionType::CONFLICT_SERVER_WINS, client_relative_path);
                    std::cout << "CONFLICT (TS same, CS diff): " << client_relative_path << ". Server wins." << std::endl;
                }
            } else if (client_file.last_modified > server_file.last_modified) {
                // Client's file is newer
                operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
                // std::cout << "UPLOAD: " << client_relative_path << " (Client newer)" << std::endl;

            } else { // server_file.last_modified > client_file.last_modified
                // Server's file is newer
                operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, client_relative_path);
                // std::cout << "DOWNLOAD: " << client_relative_path << " (Server newer)" << std::endl;
            }
        } else {
            // File exists on client, but not on server
            operations.emplace_back(SyncActionType::UPLOAD_TO_SERVER, client_relative_path);
            // std::cout << "UPLOAD (New on client): " << client_relative_path << std::endl;
        }
    }

    // Iterate through server files to find those not present on client
    for (const auto& pair : server_file_states) {
        const std::string& server_relative_path = pair.first;
        if (client_files_processed.find(server_relative_path) == client_files_processed.end()) {
            // File exists on server, but not in client's list
            // Strategy: Client needs to download it
            operations.emplace_back(SyncActionType::DOWNLOAD_TO_CLIENT, server_relative_path);
            // std::cout << "DOWNLOAD (New on server / Missing on client): " << server_relative_path << std::endl;
        }
    }

    return operations;
}