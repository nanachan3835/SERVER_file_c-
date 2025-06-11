#include "server.hpp"
#include "config.hpp"
#include "protocol.hpp"
#include "sync_manager.hpp" // Để có SyncActionType enum

#include <Poco/StreamCopier.h>
#include <Poco/Path.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>
#include <Poco/Net/PartSource.h>
#include <Poco/Net/PartHandler.h>
#include <Poco/Net/MessageHeader.h>
#include <Poco/Net/NameValueCollection.h>
#include <Poco/Exception.h>


//#include <Poco/Net/MessageHeader.h>
//#include <Poco/Net/NameValueCollection.h>
//#include <Poco/StreamCopier.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
// Thêm vào đầu file server.cpp
#include <Poco/TemporaryFile.h>
#include <Poco/File.h>



std::map<std::string, APIRouterHandler::ActiveSession> APIRouterHandler::active_sessions_;
Poco::Mutex APIRouterHandler::session_mutex_;

namespace {
    class NotFoundHandler : public Poco::Net::HTTPRequestHandler {
    public:
        void handleRequest(Poco::Net::HTTPServerRequest& req, Poco::Net::HTTPServerResponse& resp) override {
            resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
            resp.setContentType(ContentTypes::TEXT_PLAIN);
            const char* msg = "Resource not found.";
            resp.sendBuffer(msg, strlen(msg));
        }
    };
}












FileServerRequestHandlerFactory::FileServerRequestHandlerFactory(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm)
    : db_(db), user_manager_(um), file_manager_(fm), sync_manager_(sm), access_control_manager_(acm) {}

HTTPRequestHandler* FileServerRequestHandlerFactory::createRequestHandler(const HTTPServerRequest& request) {
    if (request.getURI().rfind(API_BASE_PATH, 0) == 0) {
        return new APIRouterHandler(db_, user_manager_, file_manager_, sync_manager_, access_control_manager_);
    }
    return new NotFoundHandler();
}






// --- APIRouterHandler Implementation ---
APIRouterHandler::APIRouterHandler(Database& db, UserManager& um, FileManager& fm, SyncManager& sm, AccessControlManager& acm)
    : db_(db), user_manager_(um), file_manager_(fm), sync_manager_(sm), access_control_manager_(acm) {
    setupRoutes(); // Gọi hàm đăng ký route
}



void APIRouterHandler::setupRoutes() {
    // --- Public Routes ---
    // Key của map là "METHOD /path"
    public_routes_["POST " + Endpoints::REGISTER] = [this](auto& req, auto& resp){ this->handleUserRegister(req, resp); };
    public_routes_["POST " + Endpoints::LOGIN]    = [this](auto& req, auto& resp){ this->handleUserLogin(req, resp); };

    // --- Authenticated Routes ---
    authenticated_routes_["POST " + Endpoints::LOGOUT]          = [this](auto& req, auto& resp, const auto& sess){ this->handleUserLogout(req, resp, sess); };
    authenticated_routes_["GET " + Endpoints::USER_ME]           = [this](auto& req, auto& resp, const auto& sess){ this->handleUserMe(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::FILES_UPLOAD]     = [this](auto& req, auto& resp, const auto& sess){ this->handleFileUpload(req, resp, sess); };
    authenticated_routes_["GET " + Endpoints::FILES_DOWNLOAD]    = [this](auto& req, auto& resp, const auto& sess){ this->handleFileDownload(req, resp, sess); };
    authenticated_routes_["GET " + Endpoints::FILES_LIST]        = [this](auto& req, auto& resp, const auto& sess){ this->handleFileList(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::FILES_MKDIR]      = [this](auto& req, auto& resp, const auto& sess){ this->handleFileMkdir(req, resp, sess); };
    authenticated_routes_["DELETE " + Endpoints::FILES_DELETE]   = [this](auto& req, auto& resp, const auto& sess){ this->handleFileDelete(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::FILES_RENAME]     = [this](auto& req, auto& resp, const auto& sess){ this->handleFileRename(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::SYNC_MANIFEST]    = [this](auto& req, auto& resp, const auto& sess){ this->handleSyncManifest(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::SHARED_CREATE_STORAGE] = [this](auto& req, auto& resp, const auto& sess){ this->handleCreateSharedStorage(req, resp, sess); };
    authenticated_routes_["POST " + Endpoints::SHARED_GRANT_ACCESS]   = [this](auto& req, auto& resp, const auto& sess){ this->handleGrantSharedAccess(req, resp, sess); };
}








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
    //////////////////////////////////////////////////////////////////////////

    Poco::URI uri(request.getURI());
    std::string endpointPath = uri.getPath();
    std::string method = request.getMethod();
    std::string route_key = method + " " + endpointPath;
    std::cout << Poco::DateTimeFormatter::format(Poco::Timestamp(), Poco::DateTimeFormat::ISO8601_FORMAT)
              << " Request: " << method << " " << endpointPath << " from " << request.clientAddress().toString() << std::endl;
    

    
    
//     std::optional<ActiveSession> current_session_opt;
//     bool needs_authentication = !(
//     (endpointPath == Endpoints::REGISTER && method == Poco::Net::HTTPRequest::HTTP_POST) || // SỬA Ở ĐÂY
//     (endpointPath == Endpoints::LOGIN && method == Poco::Net::HTTPRequest::HTTP_POST)    // SỬA Ở ĐÂY
// );

    // if (needs_authentication) {
    //     current_session_opt = getAuthenticatedSession(request);
    //     if (!current_session_opt) {
    //         sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Auth required.");
    //         return;
    //     }
    // }

    



    
    // try {
    //     // --- Public Endpoints ---
    //     if (endpointPath == Endpoints::REGISTER && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //         handleUserRegister(request, response);
    //     } else if (endpointPath == Endpoints::LOGIN && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //         handleUserLogin(request, response);
    //     }
    //     // --- Authenticated Endpoints ---
    //     else if (current_session_opt) { // This implies needs_authentication was true and successful
    //         const ActiveSession& session = *current_session_opt; // Now safe to use

    //         if (endpointPath == Endpoints::LOGOUT && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleUserLogout(request, response, session);
    //         } else if (endpointPath == Endpoints::USER_ME && method == Poco::Net::HTTPRequest::HTTP_GET) {
    //             handleUserMe(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_UPLOAD && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleFileUpload(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_DOWNLOAD && method == Poco::Net::HTTPRequest::HTTP_GET) {
    //             handleFileDownload(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_LIST && method == Poco::Net::HTTPRequest::HTTP_GET) {
    //             handleFileList(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_MKDIR && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleFileMkdir(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_DELETE && method == Poco::Net::HTTPRequest::HTTP_DELETE) {
    //             handleFileDelete(request, response, session);
    //         } else if (endpointPath == Endpoints::FILES_RENAME && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleFileRename(request, response, session);
    //         } else if (endpointPath == Endpoints::SYNC_MANIFEST && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleSyncManifest(request, response, session);
    //         } else if (endpointPath == Endpoints::SHARED_CREATE_STORAGE && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleCreateSharedStorage(request, response, session);
    //         } else if (endpointPath == Endpoints::SHARED_GRANT_ACCESS && method == Poco::Net::HTTPRequest::HTTP_POST) {
    //             handleGrantSharedAccess(request, response, session);
    //         }
    //         // ... more authenticated routes
    //         else {
    //             sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Authenticated API endpoint not found.");
    //         }
    //     } else if (needs_authentication) {
    //         // This case should have been caught by the earlier check, but as a safeguard
    //         sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Endpoint requires authentication, but no valid session found.");
    //     } else {
    //         // Public endpoint not matched
    //         sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Public API endpoint not found.");
    //     }

    try {
        // 1. Thử tìm trong các route public trước
        auto public_it = public_routes_.find(route_key);
        if (public_it != public_routes_.end()) {
            public_it->second(request, response); // Gọi handler public
            return;
        }

        // 2. Nếu không phải public, yêu cầu xác thực
        auto current_session_opt = getAuthenticatedSession(request);
        if (!current_session_opt) {
            sendErrorResponse(response, HTTPResponse::HTTP_UNAUTHORIZED, "Authentication required.");
            return;
        }
        const ActiveSession& session = *current_session_opt;

        // 3. Tìm trong các route đã xác thực
        auto auth_it = authenticated_routes_.find(route_key);
        if (auth_it != authenticated_routes_.end()) {
            auth_it->second(request, response, session); // Gọi handler đã xác thực
            return;
        }

        // 4. Nếu không tìm thấy ở đâu cả
        sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "API endpoint not found.");

    


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
        req_payload = json::parse(request.stream()); // Parse stream trực tiếp
    } catch (const json::parse_error& e) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid JSON for register: " + std::string(e.what()) + " at byte " + std::to_string(e.byte));
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
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid JSON for login: " + std::string(e.what()) + " at byte " + std::to_string(e.byte));
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
// void APIRouterHandler::handleFileUpload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
//     // if (request.getContentType().rfind("multipart/form-data", 0) != 0) {
//     //     sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Content-Type must be multipart/form-data.");
//     //     return;
//     // }



//     std::string relative_path_from_header = request.get(HttpHeaders::FILE_RELATIVE_PATH, "");
//     if (relative_path_from_header.empty() || Poco::Path(relative_path_from_header).isAbsolute() || relative_path_from_header.find("..") != std::string::npos) {
//         sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing '" + HttpHeaders::FILE_RELATIVE_PATH + "' header.");
//         return;
//     }
//     std::cout << "[Server Upload] Received multipart request. Content-Type: " << request.getContentType() << std::endl;

//     std::cout << "[SERVER DEBUG UPLOAD] User: " << session.username << ", HomeDir: " << session.home_dir << std::endl;
//     std::cout << "[SERVER DEBUG UPLOAD] Request Content-Type: " << request.getContentType() << std::endl;
//     std::cout << "[SERVER DEBUG UPLOAD] Request Content-Length: " << request.getContentLength() << std::endl;
//     //std::string relative_path_from_header = request.get(HttpHeaders::FILE_RELATIVE_PATH, "");
    
//     std::cout << "[Server Upload] User: " << session.username << ", HomeDir: " << session.home_dir << std::endl;
//     std::cout << "[Server Upload] Target relative path: '" << relative_path_from_header << "'" << std::endl;
     
     
 
    
 



//     class FileUploadPartHandler : public Poco::Net::PartHandler {
//     public:
//         std::string tempFilePath;
//         std::string originalFileName;
//         bool fileReceived = false;

//         void handlePart(const Poco::Net::MessageHeader& header, std::istream& stream) override {
//             // Chỉ xử lý part đầu tiên là "file"
//             if (fileReceived) return;

//             Poco::Net::NameValueCollection params;
//             std::string disposition;
//             if (header.has("Content-Disposition")) {
//                 disposition = header.get("Content-Disposition");
//                 Poco::Net::MessageHeader::splitParameters(disposition, disposition, params);
//             }

//             // Chúng ta chỉ quan tâm đến part có name="file"
//             if (params.get("name", "") == "file") {
//                 originalFileName = params.get("filename", "(unspecified)");
//                 if (originalFileName.empty() || originalFileName == "(unspecified)") {
//                     // Nếu không có tên file, chúng ta không thể xử lý
//                     return;
//                 }

//                 // Tạo một file tạm để lưu nội dung upload
//                 Poco::TemporaryFile tempFile;
//                 tempFilePath = tempFile.path();
//                 std::cout << "[Server Upload] Streaming to temporary file: " << tempFilePath << std::endl;

//                 // Mở file tạm để ghi
//                 std::ofstream outfile(tempFilePath, std::ios::binary | std::ios::trunc);
//                 if (!outfile) {
//                     std::cerr << "Failed to open temporary file for writing: " << tempFilePath << std::endl;
//                     return;
//                 }

//                 // Stream dữ liệu từ request vào file tạm
//                 Poco::StreamCopier::copyStream(stream, outfile);
//                 outfile.close();

//                 fileReceived = true;
//             }
//         }
//     };

    
    //HTMLForm form_processor(partHandler); // Chỉ định PartHandler
    //Poco::Net::HTMLForm form_processor(request, request.stream(), partHandler);
    // Poco::Net::HTMLForm form_processor(request.getContentType(), partHandler); // Cần contentType
//     

void APIRouterHandler::handleFileUpload(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    if (request.getContentType().rfind("multipart/form-data", 0) != 0) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Content-Type must be multipart/form-data.");
        return;
    }

    std::string relative_path_from_header = request.get(HttpHeaders::FILE_RELATIVE_PATH, "");
    if (relative_path_from_header.empty() || Poco::Path(relative_path_from_header).isAbsolute() || relative_path_from_header.find("..") != std::string::npos) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid or missing '" + HttpHeaders::FILE_RELATIVE_PATH + "' header.");
        return;
    }

    std::cout << "[Server Upload] User: " << session.username << ", Target relative path: '" << relative_path_from_header << "'" << std::endl;

    class FileUploadPartHandler : public Poco::Net::PartHandler {
    public:
        std::string tempFilePath;
        std::string originalFileName;
        bool fileReceived = false;

        void handlePart(const Poco::Net::MessageHeader& header, std::istream& stream) override {
            if (fileReceived) return;

            Poco::Net::NameValueCollection params;
            std::string disposition;
            if (header.has("Content-Disposition")) {
                disposition = header.get("Content-Disposition");
                Poco::Net::MessageHeader::splitParameters(disposition, disposition, params);
            }

            if (params.get("name", "") == "file") {
                originalFileName = params.get("filename", "(unspecified)");
                if (originalFileName.empty() || originalFileName == "(unspecified)") return;

                Poco::TemporaryFile tempFile; // Sẽ hoạt động sau khi include
                tempFilePath = tempFile.path();
                std::cout << "[Server Upload] Streaming to temporary file: " << tempFilePath << std::endl;

                std::ofstream outfile(tempFilePath, std::ios::binary | std::ios::trunc);
                if (!outfile) return;

                Poco::StreamCopier::copyStream(stream, outfile);
                outfile.close();
                fileReceived = true;
            }
        }
    };

    try {
        FileUploadPartHandler partHandler;
        Poco::Net::HTMLForm form(request, request.stream(), partHandler);

        if (!partHandler.fileReceived || partHandler.tempFilePath.empty()) {
            sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing or invalid 'file' part in multipart form.");
            if (!partHandler.tempFilePath.empty()) Poco::File(partHandler.tempFilePath).remove();
            return;
        }

        fs::path target_abs_fs_path = file_manager_.resolve_safe_path(session.home_dir, relative_path_from_header);
        if (target_abs_fs_path.empty()) {
            sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Path resolution failed for upload.");
            Poco::File(partHandler.tempFilePath).remove();
            return;
        }

        PermissionLevel perm = access_control_manager_.get_permission(session.user_id, target_abs_fs_path.parent_path());
        if (perm < PermissionLevel::READ_WRITE) {
            sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied to write to target location.");
            Poco::File(partHandler.tempFilePath).remove();
            return;
        }

        fs::create_directories(target_abs_fs_path.parent_path());
        ////bắt đầu logic ghi file   
        Poco::File tempPocoFile(partHandler.tempFilePath);
        tempPocoFile.moveTo(target_abs_fs_path.string());

        std::cout << "[Server Upload] File moved from temp to: " << target_abs_fs_path << std::endl;

        // std::ifstream final_file(target_abs_fs_path, std::ios::binary | std::ios::ate);
        // std::streamsize size = final_file.tellg();
        // final_file.seekg(0, std::ios::beg);
        // std::vector<char> file_data(size);
        // if (final_file.read(file_data.data(), size)) {
        //     if (file_manager_.upload_file(session.home_dir, relative_path_from_header, file_data, session.user_id)) {
        //         sendSuccessResponse(response, "File '" + relative_path_from_header + "' uploaded successfully.", HTTPResponse::HTTP_CREATED);
        //     } else {
        //         sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "File upload failed during final metadata update.");
        //     }
        // } else {
        //     sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Could not read final file for metadata update.");
        // }
        file_manager_.update_file_metadata(target_abs_fs_path, session.user_id);
        sendSuccessResponse(response, "File '" + relative_path_from_header + "' uploaded successfully.", HTTPResponse::HTTP_CREATED);

    } catch (const Poco::Exception& e) {
        std::cerr << "[Server Upload] Poco::Exception: " << e.displayText() << std::endl;
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Server error during upload: " + e.displayText());
    } catch (const std::exception& e) {
        std::cerr << "[Server Upload] std::exception: " << e.what() << std::endl;
        sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Generic server error during upload: " + std::string(e.what()));
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
    //fs::path safe_old_abs, safe_new_abs;
    try {
        // BƯỚC 1: Resolve và xác thực tất cả các đường dẫn
        fs::path safe_old_abs = file_manager_.resolve_safe_path(session.home_dir, old_relative_path);
        fs::path safe_new_parent_abs = file_manager_.resolve_safe_path(session.home_dir, Poco::Path(new_relative_path).parent().toString());

        if (safe_old_abs.empty() || safe_new_parent_abs.empty()) {
            sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Path resolution failed for rename (likely out of bounds).");
            return;
        }
        fs::path safe_new_abs = safe_new_parent_abs / Poco::Path(new_relative_path).getFileName();

        // BƯỚC 2: Sau khi có đường dẫn an toàn, mới kiểm tra quyền
        PermissionLevel perm_old_parent = access_control_manager_.get_permission(session.user_id, safe_old_abs.parent_path());
        PermissionLevel perm_new_parent = access_control_manager_.get_permission(session.user_id, safe_new_abs.parent_path());
        if (perm_old_parent < PermissionLevel::READ_WRITE || perm_new_parent < PermissionLevel::READ_WRITE) {
            sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied for rename operation (source or destination parent).");
            return;
        }

        // Đảm bảo thư mục cha của đích tồn tại
        if (!fs::exists(safe_new_abs.parent_path())) {
             if(!fs::create_directories(safe_new_abs.parent_path())) {
                sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Could not create destination parent directory.");
                return;
             }
        }

        // --- BẮT ĐẦU PHẦN THAY ĐỔI CHÍNH ---
        fs::rename(safe_old_abs, safe_new_abs);
        // --- KẾT THÚC PHẦN THAY ĐỔI CHÍNH ---

        file_manager_.update_metadata_after_rename(safe_old_abs, safe_new_abs, session.user_id);
        sendSuccessResponse(response, "Renamed successfully from '" + old_relative_path + "' to '" + new_relative_path + "'.");

    } catch (const fs::filesystem_error& e) {
        // Phân tích mã lỗi để trả về HTTP status chính xác
        std::error_code ec = e.code();
        if (ec == std::errc::no_such_file_or_directory) {
            // So sánh path trong exception với path nguồn và đích để biết cái nào bị thiếu
            // e.path1() thường là path nguồn, e.path2() là path đích
            sendErrorResponse(response, HTTPResponse::HTTP_NOT_FOUND, "Source path for rename does not exist: " + e.path1().string());
        } else if (ec == std::errc::file_exists) {
            sendErrorResponse(response, HTTPResponse::HTTP_CONFLICT, "Destination path for rename already exists: " + e.path2().string());
        } else if (ec == std::errc::permission_denied) {
            sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied by the filesystem for rename operation.");
        } else {
            // Các lỗi khác (VD: disk full, I/O error)
            sendErrorResponse(response, HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Rename failed due to a filesystem error: " + std::string(e.what()));
        }
    }
}

// Sync Handler
void APIRouterHandler::handleSyncManifest(HTTPServerRequest& request, HTTPServerResponse& response, const ActiveSession& session) {
    json req_payload;
    try {
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid JSON for sync: " + std::string(e.what()) + " at byte " + std::to_string(e.byte));
        return;
    }

    if (!req_payload.contains(JsonKeys::CLIENT_FILES) || !req_payload[JsonKeys::CLIENT_FILES].is_array()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing or invalid 'client_files' array.");
        return;
    }

    std::vector<ClientSyncFileInfo> client_files_info_list; // KHAI BÁO ĐÚNG
    for (const auto& cf_json : req_payload[JsonKeys::CLIENT_FILES]) {
        ClientSyncFileInfo cfi;
        cfi.relative_path = cf_json.value(JsonKeys::RELATIVE_PATH, "");
        cfi.last_modified = Poco::Timestamp::fromEpochTime(cf_json.value(JsonKeys::LAST_MODIFIED, (long long)0));
        cfi.checksum = cf_json.value(JsonKeys::CHECKSUM, "");

        cfi.is_directory = cf_json.value(JsonKeys::IS_DIRECTORY, false);
        cfi.is_deleted = cf_json.value("is_deleted", false); 
        if (!cfi.relative_path.empty()) {
            client_files_info_list.push_back(cfi);
        }
    }
    
    Poco::Path server_sync_root_path(session.home_dir); // KHAI BÁO ĐÚNG

    PermissionLevel perm = access_control_manager_.get_permission(session.user_id, fs::path(session.home_dir));
    if (perm < PermissionLevel::READ_WRITE) {
         sendErrorResponse(response, HTTPResponse::HTTP_FORBIDDEN, "Permission denied for sync on home dir.");
        return;
    }



    std::vector<SyncOperation> sync_ops_result = sync_manager_.determine_sync_actions(session.user_id, server_sync_root_path, client_files_info_list, access_control_manager_);

    json ops_json_array_resp = json::array();
    for (const auto& op : sync_ops_result) {
        json op_json_obj;
        std::string action_str;
        switch (op.action) { // Dùng SyncActionType từ sync_manager.hpp
            case SyncActionType::NO_ACTION: action_str = "NO_ACTION"; break;
            case SyncActionType::UPLOAD_TO_SERVER: action_str = "UPLOAD_TO_SERVER"; break;
            case SyncActionType::DOWNLOAD_TO_CLIENT: action_str = "DOWNLOAD_TO_CLIENT"; break;
            case SyncActionType::CONFLICT_SERVER_WINS: action_str = "CONFLICT_SERVER_WINS"; break;
            // ... các case khác ...
            default: action_str = "UNKNOWN_SYNC_ACTION";
        }

        // Tạo JSON object cho mỗi operation
        op_json_obj[JsonKeys::SYNC_ACTION_TYPE] = action_str;
        op_json_obj[JsonKeys::RELATIVE_PATH] = op.relative_path;
        ops_json_array_resp.push_back(op_json_obj);
    }
    json res_payload;
    res_payload[JsonKeys::STATUS] = "success";
    res_payload[JsonKeys::SYNC_OPERATIONS] = ops_json_array_resp;
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
        req_payload = json::parse(request.stream());
    } catch (const json::parse_error& e) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid JSON for grant access: " + std::string(e.what()) + " at byte " + std::to_string(e.byte));
        return;
    }
    std::string storage_name = req_payload.value(JsonKeys::STORAGE_NAME, "");
    std::string target_username = req_payload.value(JsonKeys::TARGET_USER, "");
    std::string perm_str = req_payload.value(JsonKeys::PERMISSION, "");

    if (storage_name.empty() || target_username.empty() || perm_str.empty()) {
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Missing fields for grant access.");
        return;
    }
    // SỬA LỖI: Gọi hàm public từ AccessControlManager
    PermissionLevel perm_to_grant = access_control_manager_.string_to_permission_level(perm_str);
    if (perm_to_grant == PermissionLevel::NONE && perm_str != "none") { // "none" có thể là một cách để revoke
        sendErrorResponse(response, HTTPResponse::HTTP_BAD_REQUEST, "Invalid permission string.");
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