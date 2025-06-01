#pragma once

#include "db.hpp"
#include "file_manager.hpp" // For FileInfo struct, if useful, or define a local one
#include <string>
#include <vector>
#include <map>
#include <Poco/Timestamp.h>
#include <Poco/Path.h>

// Information about a file as reported by the client
struct ClientSyncFileInfo {
    std::string relative_path;   // Path relative to the sync root
    Poco::Timestamp last_modified; // Client's timestamp
    std::string checksum;          // Client's checksum
    // bool is_directory; // Optional: if client also syncs empty dirs
};

// Information about a file on the server, typically from file_metadata DB
struct ServerSyncFileInfo {
    std::string full_path_on_server; // For direct access by FileManager if needed
    std::string relative_path;      // Path relative to the sync root
    Poco::Timestamp last_modified;    // Server's timestamp
    std::string checksum;             // Server's checksum
    int owner_user_id;
    int version;
    // bool is_directory; // Optional
};

enum class SyncActionType {
    NO_ACTION,
    UPLOAD_TO_SERVER,    // Client sends file to server
    DOWNLOAD_TO_CLIENT,  // Client receives file from server
    CONFLICT_SERVER_WINS,// Client should download server's version to resolve
    CONFLICT_CLIENT_WINS,// Client should upload its version to resolve (server overwrites)
    CREATE_CONFLICT_COPY_ON_SERVER, // Server renames its, client uploads. Or server renames client's upload.
    DELETE_ON_CLIENT,    // Client should delete its local copy
    DELETE_ON_SERVER     // Server should delete its copy (client initiated)
};

struct SyncOperation {
    SyncActionType action;
    std::string relative_path;
    // Optional: bool is_directory;
    // Optional: std::string conflict_new_name_on_server; // If creating conflict copy

    SyncOperation(SyncActionType act, std::string path) : action(act), relative_path(std::move(path)) {}
};

class SyncManager {
public:
    SyncManager(Database& db, FileManager& file_manager);

    /**
     * @brief Determines the necessary synchronization operations.
     * @param user_id The ID of the user performing the sync.
     * @param server_sync_root_path The absolute path to the user's sync root on the server (e.g., /data/users/username/ or /data/shared/projectA/).
     * @param client_files A list of files and their states from the client.
     * @return A vector of SyncOperation to be performed.
     */
    std::vector<SyncOperation> determine_sync_actions(
        int user_id,
        const Poco::Path& server_sync_root_path,
        const std::vector<ClientSyncFileInfo>& client_files
    );

private:
    Database& db_;
    FileManager& file_manager_; // May not be strictly needed if all info comes from DB

    // Fetches file metadata for a given sync root from the database.
    std::map<std::string, ServerSyncFileInfo> get_server_file_states(
        int user_id,
        const Poco::Path& server_sync_root_path
    );
};