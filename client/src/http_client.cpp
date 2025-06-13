#include "http_client.hpp" // Từ thư mục includes/
// protocol.hpp và json.hpp đã được include trong http_client.hpp

#include <Poco/Net/HTMLForm.h>
#include <Poco/Net/FilePartSource.h> // Cho upload file
#include <Poco/Net/StringPartSource.h> // Cho các text field trong multipart
#include <Poco/StreamCopier.h>
#include <Poco/Path.h>
#include <Poco/Exception.h>
#include <fstream>   // Cho std::ofstream trong downloadFile
#include <sstream>   // Cho std::ostringstream
#include <iostream>  // Cho std::cerr (debug)
#include <Poco/File.h> 
//#include <Poco/Net/ConnectionRefusedException.h>
//#include <Poco/TimeoutException.h>
//#include <Poco/Exception.h>
#include <Poco/Net/NetException.h>
#include <Poco/UUIDGenerator.h> // Thêm include này để tạo boundary duy nhất
#include <Poco/String.h>




const char* clientSyncErrorCodeToString(ClientSyncErrorCode code) {
    switch (code) {
        case ClientSyncErrorCode::SUCCESS: return "Success";
        case ClientSyncErrorCode::ERROR_INVALID_URL: return "Invalid URL";
        case ClientSyncErrorCode::ERROR_CONNECTION_FAILED: return "Connection failed";
        case ClientSyncErrorCode::ERROR_AUTH_FAILED: return "Authentication failed (401)";
        case ClientSyncErrorCode::ERROR_FORBIDDEN: return "Forbidden (403)";
        case ClientSyncErrorCode::ERROR_NOT_FOUND: return "Resource not found (404)";
        case ClientSyncErrorCode::ERROR_TIMEOUT: return "Request timed out";
        case ClientSyncErrorCode::ERROR_BAD_REQUEST: return "Bad request (400)";
        case ClientSyncErrorCode::ERROR_SERVER_ERROR: return "Server error (5xx)";
        case ClientSyncErrorCode::ERROR_CONFLICT: return "Conflict (409)";
        case ClientSyncErrorCode::ERROR_JSON_PARSE: return "JSON parse error in response";
        case ClientSyncErrorCode::ERROR_LOCAL_FILE_IO: return "Local file I/O error";
        case ClientSyncErrorCode::ERROR_MULTIPART_FORM: return "Multipart form error";
        case ClientSyncErrorCode::ERROR_UNKNOWN: return "Unknown error";
        default: return "Undefined error code";
    }
}

HttpClient::HttpClient(const std::string& base_url, Poco::Timespan timeout)
    : server_uri_base_(base_url), default_timeout_(timeout) {
    if (server_uri_base_.getPath().empty()) {
        server_uri_base_.setPath("/"); // Đảm bảo URI có path, ít nhất là "/"
    }
}




ApiResponse HttpClient::performRequest(Poco::Net::HTTPRequest& request, const std::string& requestBody, Poco::Net::HTTPClientSession* existingSession) {
    ApiResponse api_res;
    Poco::Net::HTTPClientSession localSession; // Tạo session cục bộ nếu không có session được truyền vào
    Poco::Net::HTTPClientSession* session_ptr;

    if (existingSession) {
        session_ptr = existingSession;
    } else {
        localSession.setHost(server_uri_base_.getHost());
        localSession.setPort(server_uri_base_.getPort());
        localSession.setTimeout(default_timeout_);
        session_ptr = &localSession;
    }

    try {
        if (!requestBody.empty()) {
            request.setContentLength(requestBody.length());
            if (request.getContentType().empty()) { // Đặt default nếu client chưa set
                request.setContentType(ContentTypes::APPLICATION_JSON);
            }
        }
        // Gửi request
        std::ostream& ostr = session_ptr->sendRequest(request);
        if (!requestBody.empty()) {
            ostr << requestBody;
            ostr.flush();
        }

        // Nhận response
        Poco::Net::HTTPResponse http_res;
        std::istream& rs = session_ptr->receiveResponse(http_res);
        api_res.statusCode = http_res.getStatus();

        std::ostringstream data_oss;
        Poco::StreamCopier::copyStream(rs, data_oss);
        std::string response_body_str = data_oss.str();
        api_res.raw_body_if_not_json = response_body_str; // Lưu lại raw body

        // Parse JSON nếu content type là JSON
        std::string contentType = http_res.getContentType();
        std::string::size_type pos = contentType.find(';');
        if (pos != std::string::npos) {
            contentType = contentType.substr(0, pos); // Bỏ phần charset nếu có
        }

        if (contentType == "application/json" && !response_body_str.empty()) {
            try {
                api_res.body = json::parse(response_body_str);
            } catch (const json::parse_error& e) {
                std::cerr << "HttpClient: Failed to parse JSON response for " << request.getURI() << ": " << e.what()
                          << ". Body: " << response_body_str.substr(0, 200) << std::endl;
                api_res.error_message = "Server returned malformed JSON.";
                api_res.error_code = ClientSyncErrorCode::ERROR_JSON_PARSE;
                // Giữ lại status code gốc, nhưng đánh dấu là parse lỗi
            }
        }

        // Xử lý lỗi dựa trên status code
        if (api_res.statusCode < 200 || api_res.statusCode >= 300) {
            if (api_res.error_code == ClientSyncErrorCode::SUCCESS) { // Nếu chưa có lỗi cụ thể từ parse JSON
                if (api_res.body.is_object() && api_res.body.contains(JsonKeys::MESSAGE) && api_res.body[JsonKeys::MESSAGE].is_string()) {
                    api_res.error_message = api_res.body[JsonKeys::MESSAGE].get<std::string>();
                } else if (!response_body_str.empty() && response_body_str.length() < 512) {
                    api_res.error_message = "Server error: " + response_body_str;
                } else {
                    api_res.error_message = "Server error: " + http_res.getReason();
                }

                switch (api_res.statusCode) {
                    case Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED:    api_res.error_code = ClientSyncErrorCode::ERROR_AUTH_FAILED; break;
                    case Poco::Net::HTTPResponse::HTTP_FORBIDDEN:       api_res.error_code = ClientSyncErrorCode::ERROR_FORBIDDEN; break;
                    case Poco::Net::HTTPResponse::HTTP_NOT_FOUND:       api_res.error_code = ClientSyncErrorCode::ERROR_NOT_FOUND; break;
                    case Poco::Net::HTTPResponse::HTTP_CONFLICT:        api_res.error_code = ClientSyncErrorCode::ERROR_CONFLICT; break;
                    default:
                        if (api_res.statusCode >= 400 && api_res.statusCode < 500) api_res.error_code = ClientSyncErrorCode::ERROR_BAD_REQUEST;
                        else if (api_res.statusCode >= 500) api_res.error_code = ClientSyncErrorCode::ERROR_SERVER_ERROR;
                        else api_res.error_code = ClientSyncErrorCode::ERROR_UNKNOWN;
                        break;
                }
            }
        }

    } catch (const Poco::TimeoutException& e) { // Bắt TimeoutException cụ thể trước
        api_res.error_message = "Request to " + request.getURI() + " timed out: " + e.displayText();
        api_res.error_code = ClientSyncErrorCode::ERROR_TIMEOUT;
    } catch (const Poco::Net::ConnectionRefusedException& e) { // Bắt ConnectionRefusedException
        api_res.error_message = "Connection refused to " + server_uri_base_.getHost() + ":" + std::to_string(server_uri_base_.getPort()) + ": " + e.displayText();
        api_res.error_code = ClientSyncErrorCode::ERROR_CONNECTION_FAILED;
    } catch (const Poco::Net::NetException& e) { // Bắt các lỗi mạng khác
        api_res.error_message = "Network Exception for " + request.getURI() + ": " + e.displayText();
        api_res.error_code = ClientSyncErrorCode::ERROR_CONNECTION_FAILED;
    } catch (const Poco::Exception& e) { // Bắt các lỗi Poco chung chung cuối cùng
        api_res.error_message = "Poco HTTP Exception for " + request.getURI() + ": " + e.displayText();
        api_res.error_code = ClientSyncErrorCode::ERROR_UNKNOWN;
    }
    return api_res;
}


ApiResponse HttpClient::performMultipartUpload(Poco::Net::HTTPRequest& request, Poco::Net::HTMLForm& form, Poco::Net::HTTPClientSession* existingSession) {
    ApiResponse api_res;
    Poco::Net::HTTPClientSession localSession;
    Poco::Net::HTTPClientSession* session_ptr;

    if (existingSession) {
        session_ptr = existingSession;
    } else {
        localSession.setHost(server_uri_base_.getHost());
        localSession.setPort(server_uri_base_.getPort());
        localSession.setTimeout(default_timeout_);
        session_ptr = &localSession;
    }
    
try {
    // 1. Để Poco::Net::HTMLForm tự động chuẩn bị request.
    //    Hàm này sẽ tạo boundary, set header "Content-Type" với boundary đó,
    //    và tính toán chính xác "Content-Length" cho toàn bộ request body.
    form.prepareSubmit(request);

    // 2. (Tùy chọn nhưng rất hữu ích) In ra các header đã được Poco chuẩn bị để debug.
    //    Bạn sẽ thấy header Content-Type chứa một chuỗi boundary.
    std::cout << "[Client Upload Debug] ---- Request Headers Prepared by Poco ----" << std::endl;
    std::cout << "[Client Upload Debug] Method: " << request.getMethod() << ", URI: " << request.getURI() << std::endl;
    std::cout << "[Client Upload Debug] Content-Type: " << request.getContentType() << std::endl;
    std::cout << "[Client Upload Debug] Content-Length: " << request.getContentLength64() << std::endl;
    if (request.has(HttpHeaders::AUTH_TOKEN)) {
        std::cout << "[Client Upload Debug] X-Auth-Token: ... (present)" << std::endl;
    }
    if (request.has(HttpHeaders::FILE_RELATIVE_PATH)) {
        std::cout << "[Client Upload Debug] X-File-Relative-Path: " << request.get(HttpHeaders::FILE_RELATIVE_PATH) << std::endl;
    }
    std::cout << "[Client Upload Debug] -----------------------------------------" << std::endl;

    // 3. Gửi request (chỉ chứa headers) đến server.
    std::ostream& ostr = session_ptr->sendRequest(request);

    // 4. Ghi nội dung của form (đã được định dạng với các boundary) vào stream.
    //    Đây chính là bước gửi HTTP body.
    form.write(ostr);
    ostr.flush();

    // 5. Nhận và xử lý phản hồi từ server (phần này giữ nguyên như cũ).
    Poco::Net::HTTPResponse http_res;
    std::istream& rs = session_ptr->receiveResponse(http_res);
    api_res.statusCode = http_res.getStatus();

    std::ostringstream data_oss;
    Poco::StreamCopier::copyStream(rs, data_oss);
    std::string response_body_str = data_oss.str();
    api_res.raw_body_if_not_json = response_body_str;

    // Xử lý response body và các mã lỗi...
    if (http_res.getContentType().find("application/json") != std::string::npos && !response_body_str.empty()) {
        try {
            api_res.body = json::parse(response_body_str);
        } catch (const json::parse_error& e) {
            api_res.error_message = "Upload response: Malformed JSON: " + std::string(e.what());
            api_res.error_code = ClientSyncErrorCode::ERROR_JSON_PARSE;
        }
    }
    
    // Nếu request không thành công, điền thông tin lỗi
    if (!api_res.isSuccess() && api_res.error_code == ClientSyncErrorCode::SUCCESS) {
        if (api_res.body.is_object() && api_res.body.contains(JsonKeys::MESSAGE)) {
            api_res.error_message = api_res.body[JsonKeys::MESSAGE].get<std::string>();
        } else if (!response_body_str.empty()) {
            api_res.error_message = "Server error during upload: " + response_body_str.substr(0, 256);
        } else {
            api_res.error_message = "Server error during upload: " + http_res.getReason();
        }

        // Map status code sang error code của client
        switch (api_res.statusCode) {
            case Poco::Net::HTTPResponse::HTTP_BAD_REQUEST:    api_res.error_code = ClientSyncErrorCode::ERROR_BAD_REQUEST; break;
            case Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED:   api_res.error_code = ClientSyncErrorCode::ERROR_AUTH_FAILED; break;
            case Poco::Net::HTTPResponse::HTTP_FORBIDDEN:      api_res.error_code = ClientSyncErrorCode::ERROR_FORBIDDEN; break;
            case Poco::Net::HTTPResponse::HTTP_NOT_FOUND:      api_res.error_code = ClientSyncErrorCode::ERROR_NOT_FOUND; break;
            default:                                           api_res.error_code = ClientSyncErrorCode::ERROR_SERVER_ERROR; break;
        }
    }

} catch (const Poco::TimeoutException& e) {
    api_res.error_message = "Multipart Upload Timed Out for " + request.getURI() + ": " + e.displayText();
    api_res.error_code = ClientSyncErrorCode::ERROR_TIMEOUT;
} catch (const Poco::Net::ConnectionRefusedException& e) {
    api_res.error_message = "Connection refused during multipart upload for " + request.getURI() + ": " + e.displayText();
    api_res.error_code = ClientSyncErrorCode::ERROR_CONNECTION_FAILED;
} catch (const Poco::Net::NetException& e) {
    api_res.error_message = "Network Exception during multipart upload for " + request.getURI() + ": " + e.displayText();
    api_res.error_code = ClientSyncErrorCode::ERROR_CONNECTION_FAILED; // Lỗi kết nối chung
} catch (const Poco::Exception& e) {
    api_res.error_message = "Poco Exception during multipart upload for " + request.getURI() + ": " + e.displayText();
    api_res.error_code = ClientSyncErrorCode::ERROR_MULTIPART_FORM; // Lỗi chung cho quá trình upload
}
return api_res;
}


// --- Auth ---
ApiResponse HttpClient::login(const std::string& username, const std::string& password) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::LOGIN); // Sử dụng hằng số từ protocol.hpp
    std::cout<< "Login endpoint URI: " << endpoint_uri.toString() << std::endl;

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    // ContentType sẽ được set trong performRequest

    json payload;
    payload[JsonKeys::USERNAME] = username;
    payload[JsonKeys::PASSWORD] = password;

    return performRequest(request, payload.dump());
}

ApiResponse HttpClient::logout(const std::string& token) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath(Endpoints::LOGOUT);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);

    return performRequest(request);
}

ApiResponse HttpClient::getCurrentUser(const std::string& token) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::USER_ME);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);

    return performRequest(request);
}

// --- File Operations ---
// http_client.cpp

ApiResponse HttpClient::uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverRelativePath) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath(Endpoints::FILES_UPLOAD);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);
    request.set(HttpHeaders::FILE_RELATIVE_PATH, serverRelativePath);

    // Biến để chứa nội dung file
    std::string fileContent;

    // Bước 1: Kiểm tra và đọc file vào biến fileContent
    try {
        Poco::File localFile(localFilePath);
        if (!localFile.exists() || !localFile.isFile() || !localFile.canRead()) {
            ApiResponse res;
            res.error_message = "Local file is invalid or unreadable: " + localFilePath;
            res.error_code = ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
            return res;
        }
        
        if (localFile.getSize() == 0) {
            std::cout << "[HttpClient] Warning: Uploading a 0-byte file: " << localFilePath << std::endl;
        }

        std::ifstream fileStream(localFilePath, std::ios::binary);
        if (!fileStream) {
            ApiResponse res;
            res.error_message = "Could not create ifstream for: " + localFilePath;
            res.error_code = ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
            return res;
        }
        
        std::stringstream buffer;
        buffer << fileStream.rdbuf();
        fileContent = buffer.str(); // Gán nội dung vào biến fileContent
        fileStream.close();

    } catch (const Poco::Exception& e) {
        std::cerr << "[HttpClient] Pre-flight check exception for " << localFilePath << ": " << e.displayText() << std::endl;
        ApiResponse res;
        res.error_message = "Poco Exception during pre-flight check: " + e.displayText();
        res.error_code = ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
        return res;
    }
    
    // Bước 2: Tạo form và sử dụng StringPartSource với dữ liệu từ fileContent
    Poco::Net::HTMLForm form;
    form.setEncoding(Poco::Net::HTMLForm::ENCODING_MULTIPART);

    Poco::Path p(localFilePath);

    // --- ĐÂY LÀ DÒNG THAY ĐỔI QUAN TRỌNG NHẤT ---
    // Sử dụng StringPartSource và biến fileContent đã đọc ở trên
    form.addPart("file", new Poco::Net::StringPartSource(fileContent, p.getFileName(), "application/octet-stream"));
    // --- KHÔNG DÙNG FilePartSource NỮA ---
    // form.addPart("file", new Poco::Net::FilePartSource(localFilePath, p.getFileName())); 

    return performMultipartUpload(request, form);
}
//////downloadfile
ClientSyncErrorCode HttpClient::downloadFile(const std::string& token, const std::string& serverRelativePath, const std::string& localSavePath) {
    Poco::Net::HTTPClientSession session(server_uri_base_.getHost(), server_uri_base_.getPort());
    session.setTimeout(default_timeout_); // Có thể cần timeout dài hơn cho download file lớn

    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::FILES_DOWNLOAD);
    endpoint_uri.addQueryParameter(JsonKeys::PATH, serverRelativePath);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);

    try {
        session.sendRequest(request);
        Poco::Net::HTTPResponse http_res;
        std::istream& rs = session.receiveResponse(http_res);

        if (http_res.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
            Poco::Path p_local(localSavePath);
            Poco::File f_parent(p_local.parent());
            if (!f_parent.exists()) {
                try { f_parent.createDirectories(); }
                catch (const Poco::Exception e) {
                    std::cerr << "HttpClient: Cannot create parent dir " << f_parent.path() << ": " << e.displayText() << std::endl;
                    return ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
                }
            }

            std::ofstream outfile(localSavePath, std::ios::binary | std::ios::trunc);
            if (!outfile) {
                std::cerr << "HttpClient: Cannot open local file for writing: " << localSavePath << std::endl;
                return ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
            }
            Poco::StreamCopier::copyStream(rs, outfile);
            outfile.close();
            if (!outfile.good()) {
                 std::cerr << "HttpClient: Error writing to local file: " << localSavePath << std::endl;
                return ClientSyncErrorCode::ERROR_LOCAL_FILE_IO;
            }
            return ClientSyncErrorCode::SUCCESS;
        } else {
            std::cerr << "HttpClient: Download failed for " << serverRelativePath << ". Status: " << http_res.getStatus() << " " << http_res.getReason() << std::endl;
            std::ostringstream err_oss; Poco::StreamCopier::copyStream(rs, err_oss); // Đọc body lỗi
            std::cerr << "Server error body: " << err_oss.str().substr(0, 200) << std::endl;

            if (http_res.getStatus() == Poco::Net::HTTPResponse::HTTP_NOT_FOUND) return ClientSyncErrorCode::ERROR_NOT_FOUND;
            if (http_res.getStatus() == Poco::Net::HTTPResponse::HTTP_FORBIDDEN) return ClientSyncErrorCode::ERROR_FORBIDDEN;
            if (http_res.getStatus() == Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED) return ClientSyncErrorCode::ERROR_AUTH_FAILED;
            return ClientSyncErrorCode::ERROR_SERVER_ERROR;
        }
    } catch (const Poco::TimeoutException& e) {
        std::cerr << "HttpClient Download Timeout for " << serverRelativePath << ": " << e.displayText() << std::endl;
        return ClientSyncErrorCode::ERROR_TIMEOUT;
    } catch (const Poco::Net::ConnectionRefusedException& e) {
        std::cerr << "HttpClient Download Connection Refused for " << serverRelativePath << ": " << e.displayText() << std::endl;
        return ClientSyncErrorCode::ERROR_CONNECTION_FAILED;
    } catch (const Poco::Net::NetException& e) {
        std::cerr << "HttpClient Download Network Exception for " << serverRelativePath << ": " << e.displayText() << std::endl;
        return ClientSyncErrorCode::ERROR_CONNECTION_FAILED;
    } catch (const Poco::Exception& e) {
        std::cerr << "HttpClient Download Poco Exception for " << serverRelativePath << ": " << e.displayText() << std::endl;
        return ClientSyncErrorCode::ERROR_UNKNOWN;
    }
}

ApiResponse HttpClient::listDirectory(const std::string& token, const std::string& serverRelativePath) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::FILES_LIST);
    if (!serverRelativePath.empty() && serverRelativePath != ".") {
        endpoint_uri.addQueryParameter(JsonKeys::PATH, serverRelativePath);
    }

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);
    return performRequest(request);
}

ApiResponse HttpClient::createDirectory(const std::string& token, const std::string& serverRelativePath) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::FILES_MKDIR);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);

    json payload;
    payload[JsonKeys::PATH] = serverRelativePath;
    return performRequest(request, payload.dump());
}

ApiResponse HttpClient::deletePath(const std::string& token, const std::string& serverRelativePath) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::FILES_DELETE);
    endpoint_uri.addQueryParameter(JsonKeys::PATH, serverRelativePath);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_DELETE, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);
    return performRequest(request);
}

ApiResponse HttpClient::renamePath(const std::string& token, const std::string& oldServerRelativePath, const std::string& newServerRelativePath) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::FILES_RENAME);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);

    json payload;
    payload[JsonKeys::OLD_PATH] = oldServerRelativePath;
    payload[JsonKeys::NEW_PATH] = newServerRelativePath;
    return performRequest(request, payload.dump());
}

// --- Sync ---
ApiResponse HttpClient::postSyncManifest(const std::string& token, const json& clientManifest) {
    Poco::URI endpoint_uri(server_uri_base_);
    endpoint_uri.setPath( Endpoints::SYNC_MANIFEST);

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, endpoint_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    request.set(HttpHeaders::AUTH_TOKEN, token);
    // ContentType sẽ được set trong performRequest

    return performRequest(request, clientManifest.dump());
}