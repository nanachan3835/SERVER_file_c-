#include "server.hpp"
#include "config.hpp"    // For HTTP_SERVER_PORT, SERVER_BASE_URL, etc.
#include "protocol.hpp"  // For JsonKeys, Endpoints, HttpHeaders, ContentTypes
#include <filesystem>
#include <Poco/StreamCopier.h>
#include <Poco/Path.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>
#include <Poco/NumberParser.h> // For parsing numbers from strings if needed
#include <Poco/Net/PartSource.h> // <<< THÊM INCLUDE NÀY
#include <Poco/Net/MessageHeader.h> // Để lấy tên file từ part header (tùy chọn)
#include <fstream>   // For std::ifstream, std::ofstream
#include <sstream>   // For std::ostringstream
#include <iostream>  // For std::cout, std::cerr (debugging)
#include <chrono>



namespace { // Anonymous namespace để giới hạn scope
    class NotFoundHandler : public Poco::Net::HTTPRequestHandler {
    public:
        void handleRequest(Poco::Net::HTTPServerRequest& req, Poco::Net::HTTPServerResponse& resp) override {
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            resp.setContentType(ContentTypes::TEXT_PLAIN); // Giả sử ContentTypes::TEXT_PLAIN đã được định nghĩa
            const char* msg = "Resource not found.";
            resp.sendBuffer(msg, strlen(msg));
        }
    };
} 










// Initialize static members of APIRouterHandler
std::map<std::string, APIRouterHandler::ActiveSession> APIRouterHandler::active_sessions_;
Poco::Mutex APIRouterHandler::session_mutex_;

// --- FileServerRequestHandlerFactory Implementation ---
FileServerRequestHandlerFactory::FileServerRequestHandlerFactory(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm)
    : db_(db), user_manager_(um), file_manager_(fm), sync_manager_(sm), access_control_manager_(acm) {
    std::cout << "FileServerRequestHandlerFactory created." << std::endl;
    // Log the incoming request URI
}



HTTPRequestHandler* FileServerRequestHandlerFactory::createRequestHandler(const HTTPServerRequest& request) {
    // std::cout << "Factory: Received request for URI: " << request.getURI() << std::endl;

    // Route only API paths to APIRouterHandler
    if (request.getURI().rfind(API_BASE_PATH, 0) == 0) { // starts_with
        // std::cout << "Factory: Routing to APIRouterHandler for " << request.getURI() << std::endl;
        return new APIRouterHandler(db_, user_manager_, file_manager_, sync_manager_, access_control_manager_);
    }

    // For any other path, return a simple 404 handler
    // std::cout << "Factory: Path " << request.getURI() << " not API, returning NotFoundHandler." << std::endl;
    return new class NotFoundHandler : public HTTPRequestHandler {
        void handleRequest(HTTPServerRequest& req, HTTPServerResponse& resp) override {
            resp.setStatus(HTTPResponse::HTTP_NOT_FOUND);
            resp.setContentType(ContentTypes::TEXT_PLAIN);
            const char* msg = "Resource not found.";
            resp.sendBuffer(msg, strlen(msg));
        }
    };
}

// --- APIRouterHandler Implementation ---
APIRouterHandler::APIRouterHandler(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm)
    : db_(db), user_manager_(um), file_manager_(fm), sync_manager_(sm), access_control_manager_(acm) {}

// Utility: Send JSON response
void APIRouterHandler::sendJsonResponse(HTTPServerResponse& response, HTTPResponse::HTTPStatus status, const json& payload) {
    response.setStatus(status);
    response.setContentType(ContentTypes::APPLICATION_JSON);
    std::ostream& ostr = response.send();
    ostr << payload.dump(2); // Pretty print JSON with indent 2
    ostr.flush();
}

// Utility: Send error JSON response
void APIRouterHandler::sendErrorResponse(HTTPServerResponse& response, HTTPResponse::HTTPStatus status, const std::string& message) {
    json error_payload;
    error_payload[JsonKeys::STATUS] = "error"; // Using JsonKeys from protocol.hpp
    error_payload[JsonKeys::MESSAGE] = message;
    sendJsonResponse(response, status, error_payload);
}

// Utility: Send success JSON response (simple message)
void APIRouterHandler::sendSuccessResponse(HTTPServerResponse& response, const std::string& message, HTTPResponse::HTTPStatus status) {
    json success_payload;
    success_payload[JsonKeys::STATUS] = "success";
    success_payload[JsonKeys::MESSAGE] = message;
    sendJsonResponse(response, status, success_payload);
}


// Authentication: Generate a simple session token
std::string APIRouterHandler::generateToken(int user_id, const std::string& username) {
    Poco::UUIDGenerator& generator = Poco::UUIDGenerator::defaultGenerator();
    Poco::UUID uuid = generator.createRandom(); // Using random UUID for more uniqueness
    // Token format: "token_UID<user_id>_USER<username>_UUID<uuid_string>"
    // This is just for local demo, not cryptographically secure for production.
    return "token_UID" + std::to_string(user_id) + "_USER" + username + "_UUID" + uuid.toString();
}

// Authentication: Get active session based on token from request header
std::optional<APIRouterHandler::ActiveSession> APIRouterHandler::getAuthenticatedSession(HTTPServerRequest& request) {
    if (!request.has(HttpHeaders::AUTH_TOKEN)) {
        return std::nullopt;
    }
    std::string token = request.get(HttpHeaders::AUTH_TOKEN);

    Poco::Mutex::ScopedLock lock(session_mutex_); // Protect access to active_sessions_
    auto it = active_sessions_.find(token);
    if (it != active_sessions_.end()) {
        // Optional: Implement session expiry check
        // Poco::Timespan session_duration = Poco::Timestamp() - it->second.last_activity;
        // if (session_duration.totalMinutes() > 30) { // Example: 30 minute expiry
        //     active_sessions_.erase(it);
        //     std::cout << "Session expired for token: " << token << std::endl;
        //     return std::nullopt;
        // }
        it->second.last_activity = Poco::Timestamp(); // Update last activity time
        return it->second; // Return copy of the session data
    }
    return std::nullopt; // Token not found
}

// Main request router
void APIRouterHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    // Set common headers (CORS for local development)
    response.set("Server", "FileServer/1.0 (Poco)");
    response.set("Access-Control-Allow-Origin", "*");
    response.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    response.set("Access-Control-Allow-Headers", "Content-Type, " + HttpHeaders::AUTH_TOKEN + ", " + HttpHeaders::FILE_CHECKSUM + ", " + HttpHeaders::FILE_RELATIVE_PATH + ", " + HttpHeaders::FILE_LAST_MODIFIED);
    response.set("Access-Control-Max-Age", "86400"); // Cache preflight for 1 day

    // Handle OPTIONS (preflight) requests for CORS
    if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_OPTIONS) {
        response.setStatus(HTTPResponse::HTTP_OK);
        response.setContentLength(0);
        response.send();
        return;
    }

    Poco::URI uri(request.getURI());
    std::string endpointPath = uri.getPath(); // The path part of the URI
    std::string method = request.getMethod();

    std::cout << Poco::DateTimeFormatter::format(Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FORMAT)
              << " Request: " << method << " " << endpointPath << " from " << request.clientAddress().toString() << std::endl;

    std::optional<ActiveSession> current_session_opt;

    // Determine if authentication is needed for this endpoint
    bool needs_authentication = !(
        (endpointPath == Endpoints::REGISTER && method == Poco::Net::HTTPRequest::HTTP_POST) ||
        (endpointPath == Endpoints::LOGIN && method == Poco::Net::HTTPRequest::HTTP_POST)
    );

    if (needs_authentication) {
        current_session_opt = getAuthenticatedSession(request);
        if (!current_session_opt) {
            sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Authentication required or token invalid/expired.");
            return;
        }
    }

    try {
        // --- Public Endpoints ---
        if (endpointPath == Endpoints::REGISTER && method == Poco::Net::HTTPRequest::HTTP_POST) {
            handleUserRegister(request, response);
        } else if (endpointPath == Endpoints::LOGIN && method == Poco::Net::HTTPRequest::HTTP_POST) {
            handleUserLogin(request, response);
        }
        // --- Authenticated Endpoints ---
        else if (current_session_opt) { // This implies needs_authentication was true and successful
            const ActiveSession& session = *current_session_opt; // Now safe to use

            if (endpointPath == Endpoints::LOGOUT && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleUserLogout(request, response, session);
            } else if (endpointPath == Endpoints::USER_ME && method == Poco::Net::HTTPRequest::HTTP_GET) {
                handleUserMe(request, response, session);
            } else if (endpointPath == Endpoints::FILES_UPLOAD && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleFileUpload(request, response, session);
            } else if (endpointPath == Endpoints::FILES_DOWNLOAD && method == Poco::Net::HTTPRequest::HTTP_GET) {
                handleFileDownload(request, response, session);
            } else if (endpointPath == Endpoints::FILES_LIST && method == Poco::Net::HTTPRequest::HTTP_GET) {
                handleFileList(request, response, session);
            } else if (endpointPath == Endpoints::FILES_MKDIR && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleFileMkdir(request, response, session);
            } else if (endpointPath == Endpoints::FILES_DELETE && method == Poco::Net::HTTPRequest::HTTP_DELETE) {
                handleFileDelete(request, response, session);
            } else if (endpointPath == Endpoints::FILES_RENAME && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleFileRename(request, response, session);
            } else if (endpointPath == Endpoints::SYNC_MANIFEST && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleSyncManifest(request, response, session);
            } else if (endpointPath == Endpoints::SHARED_CREATE_STORAGE && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleCreateSharedStorage(request, response, session);
            } else if (endpointPath == Endpoints::SHARED_GRANT_ACCESS && method == Poco::Net::HTTPRequest::HTTP_POST) {
                handleGrantSharedAccess(request, response, session);
            }
            // ... more authenticated routes
            else {
                sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Authenticated API endpoint not found.");
            }
        } else if (needs_authentication) {
            // This case should have been caught by the earlier check, but as a safeguard
            sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Endpoint requires authentication, but no valid session found.");
        } else {
            // Public endpoint not matched
            sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Public API endpoint not found.");
        }
    } catch (const Poco::Exception& e) {
        std::cerr << "Poco Exception in handler: " << e.displayText() << std::endl;
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Server error (Poco): " + e.displayText());
    } catch (const json::exception& e) {
        std::cerr << "JSON Exception in handler: " << e.what() << std::endl;
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON processing error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        std::cerr << "Standard Exception in handler: " << e.what() << std::endl;
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Server error: " + std::string(e.what()));
    }
}


// --- Handler Implementations ---

// User Management Handlers
void APIRouterHandler::handleUserRegister(HTTPServerRequest& request, HTTPServerResponse& response) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }

    std::string username = req_payload.value(JsonKeys::USERNAME, "");
    std::string password = req_payload.value(JsonKeys::PASSWORD, "");

    if (username.empty() || password.empty()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Username and password are required.");
        return;
    }
    // Add more validation for username/password complexity if needed

    auto user_id_opt = user_manager_.register_user(username, password);
    if (user_id_opt) {
        json res_data;
        res_data[JsonKeys::USER_ID] = *user_id_opt;
        res_data[JsonKeys::USERNAME] = username;
        
        json res_payload;
        res_payload[JsonKeys::STATUS] = "success";
        res_payload[JsonKeys::MESSAGE] = "User registered successfully.";
        res_payload[JsonKeys::DATA] = res_data;
        sendJsonResponse(response, HTTPResponse::HTTP_CREATED, res_payload);
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_CONFLICT, "Registration failed (username may exist or other server error).");
    }
}

void APIRouterHandler::handleUserLogin(HTTPServerRequest& request, HTTPServerResponse& response) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }
    std::string username = req_payload.value(JsonKeys::USERNAME, "");
    std::string password = req_payload.value(JsonKeys::PASSWORD, "");

    if (username.empty() || password.empty()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Username and password are required.");
        return;
    }

    auto user_id_opt = user_manager_.login_user(username, password);
    if (user_id_opt) {
        auto home_dir_opt = user_manager_.get_user_home_dir(*user_id_opt);
        if (!home_dir_opt) {
            sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "User authenticated but home directory not found.");
            return;
        }

        std::string token = generateToken(*user_id_opt, username);
        ActiveSession new_session = {*user_id_opt, username, *home_dir_opt, Poco::Timestamp()};

        { // Scoped lock for session map
            Poco::Mutex::ScopedLock lock(session_mutex_);
            active_sessions_[token] = new_session;
        }
        
        json data;
        data[JsonKeys::USER_ID] = *user_id_opt;
        data[JsonKeys::USERNAME] = username;
        data[JsonKeys::TOKEN] = token;
        data[JsonKeys::HOME_DIR] = *home_dir_opt;

        json res_payload;
        res_payload[JsonKeys::STATUS] = "success";
        res_payload[JsonKeys::MESSAGE] = "Login successful.";
        res_payload[JsonKeys::DATA] = data;
        sendJsonResponse(response, HTTPResponse::HTTP_OK, res_payload);
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Login failed: Invalid username or password.");
    }
}

void APIRouterHandler::handleUserLogout(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    if (request.has(HttpHeaders::AUTH_TOKEN)) {
        std::string token = request.get(HttpHeaders::AUTH_TOKEN);
        Poco::Mutex::ScopedLock lock(session_mutex_);
        if (active_sessions_.erase(token)) {
            std::cout << "User logged out, token erased: " << token.substr(0, 20) << "..." << std::endl;
        }
    }
    sendSuccessResponse(response, "Logged out successfully.");
}

void APIRouterHandler::handleUserMe(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    json user_data;
    user_data[JsonKeys::USER_ID] = session.user_id;
    user_data[JsonKeys::USERNAME] = session.username;
    user_data[JsonKeys::HOME_DIR] = session.home_dir;
    // Add more user details if needed

    json res_payload;
    res_payload[JsonKeys::STATUS] = "success";
    res_payload[JsonKeys::DATA] = user_data;
    sendJsonResponse(response, HTTPResponse::HTTP_OK, res_payload);
}


// File & Directory Handlers
void APIRouterHandler::handleFileUpload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    if (request.getContentType().rfind("multipart/form-data", 0) != 0) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Content-Type must be multipart/form-data for file uploads.");
        return;
    }

    HTMLForm form(request, request.stream()); // Parses multipart/form-data
    std::string relative_path;

    if (request.has(HttpHeaders::FILE_RELATIVE_PATH)) {
        relative_path = request.get(HttpHeaders::FILE_RELATIVE_PATH);
    } else if (form.has("relativePath")) {
        relative_path = form.get("relativePath");
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing relative path (X-File-Relative-Path header or 'relativePath' form field).");
        return;
    }
    Poco::Path p_relative(relative_path);
    if (relative_path.empty() || p_relative.isAbsolute() || relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid relative path specified.");
        return;
    }

    Poco::Net::PartSource* part_source = nullptr;
    std::string original_filename;

    
    for (HTMLForm::ConstIterator it = form.begin(); it != form.end(); ++it) {
    if (it->second.isFile()) { // Kiểm tra xem part có phải là file không
        // Giả sử chỉ có một file được upload hoặc lấy file đầu tiên
        // Hoặc bạn có thể kiểm tra it->first (tên của form field) nếu client gửi tên cụ thể
        if (it->first == "file") { // Kiểm tra tên field là "file"
             try {
                part_source = dynamic_cast<Poco::Net::PartSource*>(form.getPartSource(it->first)); // Dùng getPartSource
                if (part_source) {
                    original_filename = it->second.getFileName(); // Lấy tên file gốc từ Part
                    break; // Tìm thấy file rồi, thoát vòng lặp
                }
            } catch (const Poco::NotFoundException&) {
                // Bỏ qua nếu part không phải là PartSource, hoặc getPartSource thất bại
            } catch (const std::bad_cast&) {
                // Bỏ qua nếu dynamic_cast thất bại
                    }
                }
            }
        }


    if (!part_source) {
    sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing 'file' part in the multipart form or it's not a file part.");
    return;
    }


    std::cout << "Uploading file: " << original_filename << " to " << relative_path << std::endl;

    std::istream& file_stream = part_source->stream(); // Bây giờ nên OK vì đã include PartSource.h
    std::vector<char> file_data((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());




    
    // Construct absolute path and check permissions
    fs::path target_abs_fs_path = fs::path(session.home_dir) / relative_path;
    // For upload, check write permission on the parent directory
    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path.parent_path());
    if (perm < PermissionLevel::READ_WRITE) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to write to the target location: " + target_abs_fs_path.parent_path().string());
        return;
    }

    std::istream& file_stream = part_source->stream();
    std::vector<char> file_data((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());

    if (file_manager_.upload_file(session.home_dir, relative_path, file_data, session.user_id)) {
        sendSuccessResponse(response, "File '" + relative_path + "' uploaded successfully.", HTTPResponse::HTTP_CREATED);
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "File upload failed on the server.");
    }
}

void APIRouterHandler::handleFileDownload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    Poco::URI uri(request.getURI());
    auto params = uri.getQueryParameters();
    std::string relative_path;
    for (const auto& p : params) {
        if (p.first == JsonKeys::PATH) { // From protocol.hpp
            relative_path = p.second;
            break;
        }
    }
    if (relative_path.empty() || Poco::Path(relative_path).isAbsolute() || relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing 'path' query parameter.");
        return;
    }

    fs::path target_abs_fs_path = fs::path(session.home_dir) / relative_path;
    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path);
    if (perm < PermissionLevel::READ) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to read this file.");
        return;
    }

    auto file_data_opt = file_manager_.download_file(session.home_dir, relative_path, session.user_id);
    if (file_data_opt) {
        Poco::Path p_filename(relative_path); // To get just the filename part
        response.set(HttpHeaders::FILE_CHECKSUM, file_manager_.calculate_checksum(target_abs_fs_path));
        // Poco::File f(target_abs_fs_path.string());
        // response.set(HttpHeaders::FILE_LAST_MODIFIED, std::to_string(f.getLastModified().epochTime()));
        response.setContentLength(file_data_opt->size());
        response.setContentType(ContentTypes::APPLICATION_OCTET_STREAM); // Or try to guess MIME from extension
        response.set("Content-Disposition", "attachment; filename=\"" + p_filename.getFileName() + "\"");
        response.sendBuffer(file_data_opt->data(), file_data_opt->size());
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "File not found or download failed.");
    }
}

void APIRouterHandler::handleFileList(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    Poco::URI uri(request.getURI());
    auto params = uri.getQueryParameters();
    std::string relative_path = "."; // Default to current directory (root of user's home)
     for (const auto& p : params) {
        if (p.first == JsonKeys::PATH) { relative_path = p.second; break; }
    }
    if (Poco::Path(relative_path).isAbsolute() || relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid 'path' query parameter.");
        return;
    }

    fs::path target_abs_fs_path = fs::path(session.home_dir) / relative_path;
    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path);
    if (perm < PermissionLevel::READ) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to list directory contents.");
        return;
    }

    std::vector<FileInfo> items = file_manager_.list_directory(session.home_dir, relative_path, session.user_id);
    json j_items = json::array();
    for (const auto& item : items) {
        j_items.push_back({
            {JsonKeys::NAME, item.name},
            {JsonKeys::PATH, item.path}, // Path relative to the sync root/home dir
            {JsonKeys::IS_DIRECTORY, item.is_directory},
            {JsonKeys::SIZE, item.size},
            {JsonKeys::LAST_MODIFIED, static_cast<long long>(item.last_modified)} // Unix timestamp
        });
    }
    json res_payload;
    res_payload[JsonKeys::STATUS] = "success";
    res_payload[JsonKeys::LISTING] = j_items;
    sendJsonResponse(response, HTTPResponse::HTTP_OK, res_payload);
}

void APIRouterHandler::handleFileMkdir(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }
    std::string relative_path = req_payload.value(JsonKeys::PATH, "");
    if (relative_path.empty() || Poco::Path(relative_path).isAbsolute() || relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing 'path' in JSON body.");
        return;
    }

    fs::path target_abs_fs_path = fs::path(session.home_dir) / relative_path;
    // Check permission on parent directory
    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path.parent_path());
    if (perm < PermissionLevel::READ_WRITE) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to create directory in the target location.");
        return;
    }

    if (file_manager_.create_directory(session.home_dir, relative_path, session.user_id)) {
        sendSuccessResponse(response, "Directory '" + relative_path + "' created successfully.", HTTPResponse::HTTP_CREATED);
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Failed to create directory (it might already exist or path is invalid).");
    }
}

void APIRouterHandler::handleFileDelete(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    // For DELETE, path is typically a query parameter
    Poco::URI uri(request.getURI());
    auto params = uri.getQueryParameters();
    std::string relative_path;
    for (const auto& p : params) {
        if (p.first == JsonKeys::PATH) { relative_path = p.second; break; }
    }
    if (relative_path.empty() || Poco::Path(relative_path).isAbsolute() || relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing 'path' query parameter for delete.");
        return;
    }

    fs::path target_abs_fs_path = fs::path(session.home_dir) / relative_path;
    // Need RW permission on the item itself to delete it
    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path);
    if (perm < PermissionLevel::READ_WRITE) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to delete this path.");
        return;
    }

    if (file_manager_.delete_file_or_directory(session.home_dir, relative_path, session.user_id)) {
        sendSuccessResponse(response, "Path '" + relative_path + "' deleted successfully."); // Or 204 No Content
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Failed to delete path (it might not exist or is a non-empty directory and remove_all failed).");
    }
}

void APIRouterHandler::handleFileRename(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }

    std::string old_relative_path = req_payload.value(JsonKeys::OLD_PATH, "");
    std::string new_relative_path = req_payload.value(JsonKeys::NEW_PATH, "");

    if (old_relative_path.empty() || new_relative_path.empty() ||
        Poco::Path(old_relative_path).isAbsolute() || old_relative_path.find("..") != std::string::npos ||
        Poco::Path(new_relative_path).isAbsolute() || new_relative_path.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid 'old_path' or 'new_path'. Paths must be relative and valid.");
        return;
    }
    if (old_relative_path == new_relative_path) {
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Old path and new path cannot be the same.");
        return;
    }

    fs::path old_abs_fs_path = fs::path(session.home_dir) / old_relative_path;
    fs::path new_abs_fs_path = fs::path(session.home_dir) / new_relative_path;

    // ACL: Need RW on parent of old_path (to remove from there) and parent of new_path (to add there)
    PermissionLevel perm_old_parent = access_control_manager_.get_permission(session.user_id, old_abs_fs_path.parent_path());
    PermissionLevel perm_new_parent = access_control_manager_.get_permission(session.user_id, new_abs_fs_path.parent_path());

    if (perm_old_parent < PermissionLevel::READ_WRITE || perm_new_parent < PermissionLevel::READ_WRITE) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied for rename operation (source or destination parent).");
        return;
    }

    // Use FileManager's resolve_safe_path to ensure paths stay within user's home.
    // FileManager ideally should have a `rename_item` method that handles metadata and safety.
    // This is a direct fs call for now.
    try {
        fs::path safe_old_abs = file_manager_.resolve_safe_path(session.home_dir, old_relative_path);
        fs::path safe_new_parent_abs = file_manager_.resolve_safe_path(session.home_dir, Poco::Path(new_relative_path).parent().toString());

        if (safe_old_abs.empty() || safe_new_parent_abs.empty()) {
            sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Path resolution failed for rename (likely out of bounds).");
            return;
        }
        // Construct the full safe new path using the validated new parent and the new filename.
        fs::path safe_new_abs = safe_new_parent_abs / Poco::Path(new_relative_path).getFileName();


        if (!fs::exists(safe_old_abs)) {
            sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Source path for rename does not exist.");
            return;
        }
        if (fs::exists(safe_new_abs)) {
            sendErrorResponse(response, HTTPResponse::HTTP_CONFLICT, "Destination path for rename already exists.");
            return;
        }
        // Ensure parent directory of new_abs_path exists (create_directories is idempotent)
        if (!fs::exists(safe_new_abs.parent_path())) {
             if(!fs::create_directories(safe_new_abs.parent_path())) {
                sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Could not create destination parent directory.");
                return;
             }
        }

        fs::rename(safe_old_abs, safe_new_abs);
        file_manager_.update_metadata_after_rename(safe_old_abs, safe_new_abs, session.user_id); // Update DB
        sendSuccessResponse(response, "Renamed successfully from '" + old_relative_path + "' to '" + new_relative_path + "'.");
    } catch (const fs::filesystem_error& e) {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Rename failed: " + std::string(e.what()));
    }
}

// Sync Handler
void APIRouterHandler::handleSyncManifest(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    
    

    std::vector<SyncOperation> sync_ops = sync_manager_.determine_sync_actions(session.user_id, server_sync_root, client_files_info);

    json ops_json_array = json::array();
    for (const auto& op : sync_ops) {
        // Tự chuyển đổi ở đây thay vì dựa vào ProtocolSyncActionTypes::ToString
        std::string action_str;
        switch (op.action) {
            case SyncActionType::NO_ACTION: action_str = "NO_ACTION"; break;
            case SyncActionType::UPLOAD_TO_SERVER: action_str = "UPLOAD_TO_SERVER"; break;
            case SyncActionType::DOWNLOAD_TO_CLIENT: action_str = "DOWNLOAD_TO_CLIENT"; break;
            case SyncActionType::CONFLICT_SERVER_WINS: action_str = "CONFLICT_SERVER_WINS"; break;
            // ... các case khác ...
            default: action_str = "UNKNOWN_SYNC_ACTION";
        }
        // Tạo JSON object cho mỗi operation
        json op_json_obj;
        op_json_obj[JsonKeys::SYNC_ACTION_TYPE] = action_str;
        op_json_obj[JsonKeys::RELATIVE_PATH] = op.relative_path;
        ops_json_array.push_back(op_json_obj); // Push back object, không phải initializer list
    }


    json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }

    if (!req_payload.contains(JsonKeys::CLIENT_FILES) || !req_payload[JsonKeys::CLIENT_FILES].is_array()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing or invalid 'client_files' array.");
        return;
    }

    std::vector<ClientSyncFileInfo> client_files_info;
    for (const auto& cf_json : req_payload[JsonKeys::CLIENT_FILES]) {
        ClientSyncFileInfo cfi;
        cfi.relative_path = cf_json.value(JsonKeys::RELATIVE_PATH, ""); // Make sure this key matches protocol.hpp
        cfi.last_modified = Poco::Timestamp::fromEpochTime(cf_json.value(JsonKeys::LAST_MODIFIED, (long long)0));
        cfi.checksum = cf_json.value(JsonKeys::CHECKSUM, "");
        if (!cfi.relative_path.empty()) { // Basic validation
            client_files_info.push_back(cfi);
        }
    }

    Poco::Path server_sync_root(session.home_dir); // Syncing user's home directory
    // TODO: If syncing a shared folder, server_sync_root would be different and require ACL check.

    std::vector<SyncOperation> sync_ops = sync_manager_.determine_sync_actions(session.user_id, server_sync_root, client_files_info);

    json ops_json_array = json::array();
    for (const auto& op : sync_ops) {
        ops_json_array.push_back({
            {JsonKeys::SYNC_ACTION_TYPE, ProtocolSyncActionTypes::ToString(op.action)},
            {JsonKeys::RELATIVE_PATH, op.relative_path}
        });
    }
    json res_payload;
    res_payload[JsonKeys::STATUS] = "success";
    res_payload[JsonKeys::SYNC_OPERATIONS] = ops_json_array;
    sendJsonResponse(response, HTTPResponse::HTTP_OK, res_payload);
}

// Sharing Handlers
void APIRouterHandler::handleCreateSharedStorage(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }
    std::string storage_name = req_payload.value(JsonKeys::STORAGE_NAME, "");
    if (storage_name.empty() || storage_name.find('/') != std::string::npos || storage_name.find("..") != std::string::npos) { // Basic validation
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing 'storage_name'.");
        return;
    }
    // Any authenticated user can create a shared storage in this simple model
    if (access_control_manager_.create_shared_storage(storage_name, session.user_id)) {
        json data;
        data[JsonKeys::STORAGE_NAME] = storage_name;
        auto path_opt = access_control_manager_.get_shared_storage_path(storage_name);
        if (path_opt) data[JsonKeys::PATH] = path_opt->string();

        json res_payload;
        res_payload[JsonKeys::STATUS] = "success";
        res_payload[JsonKeys::MESSAGE] = "Shared storage '" + storage_name + "' created.";
        res_payload[JsonKeys::DATA] = data;
        sendJsonResponse(response, HTTPResponse::HTTP_CREATED, res_payload);
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Failed to create shared storage (name might exist or other error).");
    }
}

void APIRouterHandler::handleGrantSharedAccess(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
        json req_payload;
    try {
        // request.stream() sẽ được đọc bởi json::parse.
        // Nếu stream rỗng, json::parse sẽ throw lỗi.
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) { // Bắt cụ thể parse_error
        std::string error_msg = "Invalid JSON for register: " + std::string(e.what()) +
                                " at byte " + std::to_string(e.byte);
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, error_msg);
        return;
    } catch (const json::exception& e) { // Bắt các json exception khác (ít xảy ra hơn khi parse)
         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "JSON error: " + std::string(e.what()));
        return;
    }

    std::string storage_name = req_payload.value(JsonKeys::STORAGE_NAME, "");
    std::string target_username = req_payload.value(JsonKeys::TARGET_USER, "");
    std::string perm_str = req_payload.value(JsonKeys::PERMISSION, ""); // "r" or "rw"

    if (storage_name.empty() || target_username.empty() || perm_str.empty()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing 'storage_name', 'target_user', or 'permission'.");
        return;
    }
    PermissionLevel perm_to_grant = access_control_manager_.string_to_permission_level(perm_str); // Ensure this method is public in ACM or use a helper
    if (perm_to_grant == PermissionLevel::NONE && perm_str != "none") {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid permission string. Use 'r', 'rw', or 'none'.");
        return;
    }
    if (target_username == session.username && perm_to_grant == PermissionLevel::NONE) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Cannot revoke your own access to a shared storage this way. Owner access is special.");
        return;
    }

    auto storage_path_opt = access_control_manager_.get_shared_storage_path(storage_name);
    if (!storage_path_opt) {
        sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Shared storage '" + storage_name + "' not found.");
        return;
    }
    // Check if the current user (session.user_id) has RW rights to the shared storage itself to manage its permissions
    if (access_control_manager_.get_permission(session.user_id, *storage_path_opt) < PermissionLevel::READ_WRITE) {
        sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "You do not have permission to manage access for this shared storage.");
        return;
    }

    auto target_user_id_opt = user_manager_.get_user_id_by_username(target_username);
    if (!target_user_id_opt) {
        sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Target user '" + target_username + "' not found.");
        return;
    }

    if (access_control_manager_.grant_shared_storage_access(*target_user_id_opt, storage_name, perm_to_grant)) {
        sendSuccessResponse(response, "Access to '" + storage_name + "' for user '" + target_username + "' set to '" + perm_str + "'.");
    } else {
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Failed to grant/update shared access.");
    }
}