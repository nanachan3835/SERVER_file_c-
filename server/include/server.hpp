#pragma once

#include "db.hpp"
#include "user_manager.hpp"
#include "file_manager.hpp"
#include "sync_manager.hpp"
#include "access_control.hpp"
#include "protocol.hpp" // Our HTTP protocol definitions

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTMLForm.h> // For parsing form data and multipart
#include <Poco/URI.h>
#include <Poco/Timestamp.h>
#include <Poco/Mutex.h>
#include <nlohmann/json.hpp> // For JSON manipulation

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory> // For std::unique_ptr if FileServerApp owns managers

// POCO using declarations
using Poco::Net::HTTPRequestHandler;
using Poco::Net::HTTPRequestHandlerFactory;
using Poco::Net::HTTPServerRequest;
using Poco::Net::HTTPServerResponse;
using Poco::Net::HTMLForm;
using Poco::Net::HTTPResponse; // For status codes
using json = nlohmann::json;   // Alias for nlohmann::json

// Forward declaration
class APIRouterHandler;

// Request Handler Factory: Creates instances of our APIRouterHandler
class FileServerRequestHandlerFactory : public HTTPRequestHandlerFactory {
public:
    FileServerRequestHandlerFactory(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm);
    HTTPRequestHandler* createRequestHandler(const HTTPServerRequest& request) override;

private:
    // References to shared manager instances
    Database& db_;
    UserManager& user_manager_;
    FileManager& file_manager_;
    SyncManager& sync_manager_;
    AccessControlManager& access_control_manager_;
};


// Main HTTP Request Handler: Routes and processes API requests
class APIRouterHandler : public HTTPRequestHandler {
public:
    APIRouterHandler(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm);
    void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) override;

private:
    // References to shared manager instances
    Database& db_;
    UserManager& user_manager_;
    FileManager& file_manager_;
    SyncManager& sync_manager_;
    AccessControlManager& access_control_manager_;

    // --- Active Session Structure (Simplified for local server) ---
    struct ActiveSession {
        int user_id;
        std::string username;
        std::string home_dir;       // Absolute path to user's home directory
        Poco::Timestamp last_activity;
        // Potentially other session data: Poco::UUID session_id_uuid;
    };

    // Static map for active sessions.
    // WARNING: For a local, single-process server. NOT suitable for production/multi-process.
    // Needs protection for concurrent access from multiple handler threads.
    static std::map<std::string, ActiveSession> active_sessions_;
    static Poco::Mutex session_mutex_; // Mutex to protect active_sessions_

    // --- Request Handling Helper Methods ---
    // User Management
    void handleUserRegister(HTTPServerRequest& request, HTTPServerResponse& response);
    void handleUserLogin(HTTPServerRequest& request, HTTPServerResponse& response);
    void handleUserLogout(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleUserMe(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);

    // File & Directory Management
    void handleFileUpload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleFileDownload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleFileList(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleFileMkdir(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleFileDelete(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleFileRename(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    // void handleFileMetadata(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session); // TODO

    // Synchronization
    void handleSyncManifest(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);

    // Sharing & Permissions
    void handleCreateSharedStorage(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    void handleGrantSharedAccess(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session);
    // void handleRevokeSharedAccess(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session); // TODO
    // void handleListSharedStorages(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session); // TODO

    // --- Utility Methods ---
    void sendJsonResponse(HTTPServerResponse& response, HTTPResponse::HTTPStatus status, const json& payload);
    void sendErrorResponse(HTTPServerResponse& response, HTTPResponse::HTTPStatus status, const std::string& message);
    void sendSuccessResponse(HTTPServerResponse& response, const std::string& message, HTTPResponse::HTTPStatus status = HTTPResponse::HTTP_OK);


    std::optional<ActiveSession> getAuthenticatedSession(HTTPServerRequest& request);
    std::string generateToken(int user_id, const std::string& username); // Generate a unique session token
    // void clearExpiredSessions(); // Optional: For periodic cleanup of old sessions
};

// The FileServerApp class (using Poco::Util::ServerApplication)
// would typically be in main.cpp or its own app.hpp/cpp if it gets complex.
// For this exercise, keeping server.hpp focused on request handling.