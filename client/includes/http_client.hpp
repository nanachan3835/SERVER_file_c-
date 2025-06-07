#pragma once

#include <string>
#include <vector>
#include <optional>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/URI.h>
#include <Poco/Timespan.h> // Cho timeout
#include "json.hpp"         // Thư viện nlohmann/json của bạn
#include "protocol.hpp"     // File protocol.hpp của bạn
#include <Poco/Net/HTMLForm.h>
#include <Poco/File.h> // Cho Poco::File

// Forward declaration cho các struct/enum nếu chúng được định nghĩa ở nơi khác
// và bạn chỉ muốn dùng con trỏ/tham chiếu ở đây.
// Tuy nhiên, với struct ApiResponse và enum ClientSyncErrorCode, tốt hơn là định nghĩa chúng ở đây
// hoặc trong một file dùng chung (ví dụ: common_types.hpp) mà cả http_client và các module khác cùng include.
// Hiện tại, tôi sẽ định nghĩa chúng ở đây để file này tự chứa.

using json = nlohmann::json; // Alias cho nlohmann::json

enum class ClientSyncErrorCode {
    SUCCESS = 0,
    ERROR_INVALID_URL = 1001,
    ERROR_CONNECTION_FAILED,
    ERROR_AUTH_FAILED,          // 401
    ERROR_FORBIDDEN,            // 403
    ERROR_NOT_FOUND,            // 404 (có thể là endpoint hoặc file/resource)
    ERROR_TIMEOUT,
    ERROR_BAD_REQUEST,          // 400
    ERROR_SERVER_ERROR,         // 5xx
    ERROR_CONFLICT,             // 409
    ERROR_JSON_PARSE,           // Lỗi parse JSON response
    ERROR_LOCAL_FILE_IO,        // Lỗi đọc/ghi file cục bộ khi upload/download
    ERROR_MULTIPART_FORM,       // Lỗi tạo hoặc gửi multipart form
    ERROR_UNKNOWN
};

const char* clientSyncErrorCodeToString(ClientSyncErrorCode code);

struct ApiResponse {
    long statusCode = 0; // Mặc định là 0 nếu request không đến được server
    json body;           // Sẽ là object rỗng nếu không có body hoặc parse lỗi
    std::string raw_body_if_not_json; // Lưu body nếu không phải JSON và có lỗi
    std::string error_message;
    ClientSyncErrorCode error_code = ClientSyncErrorCode::SUCCESS;

    bool isSuccess() const {
        return statusCode >= 200 && statusCode < 300 && error_code == ClientSyncErrorCode::SUCCESS;
    }
};

class HttpClient {
public:
    HttpClient(const std::string& base_url, Poco::Timespan timeout = Poco::Timespan(30, 0)); // Timeout 30 giây

    // Auth
    ApiResponse login(const std::string& username, const std::string& password);
    ApiResponse logout(const std::string& token);
    ApiResponse getCurrentUser(const std::string& token);

    // File Operations
    ApiResponse uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverRelativePath);
    // downloadFile sẽ stream trực tiếp vào file, trả về error code để đơn giản hơn
    ClientSyncErrorCode downloadFile(const std::string& token, const std::string& serverRelativePath, const std::string& localSavePath);
    ApiResponse listDirectory(const std::string& token, const std::string& serverRelativePath = "."); // Mặc định là thư mục gốc
    ApiResponse createDirectory(const std::string& token, const std::string& serverRelativePath);
    ApiResponse deletePath(const std::string& token, const std::string& serverRelativePath);
    ApiResponse renamePath(const std::string& token, const std::string& oldServerRelativePath, const std::string& newServerRelativePath);

    // Sync
    ApiResponse postSyncManifest(const std::string& token, const json& clientManifest);

private:
    Poco::URI server_uri_base_; // Lưu URI gốc của server
    Poco::Timespan default_timeout_;

    // Hàm helper chung để gửi request và nhận response
    ApiResponse performRequest(Poco::Net::HTTPRequest& request, const std::string& requestBody = "", Poco::Net::HTTPClientSession* existingSession = nullptr);
    // Hàm helper cho multipart (upload)
    ApiResponse performMultipartUpload(Poco::Net::HTTPRequest& request, Poco::Net::HTMLForm& form, Poco::Net::HTTPClientSession* existingSession = nullptr);
};