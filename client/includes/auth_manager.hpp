#pragma once
#include <string>
#include <optional>
#include "http_client.hpp" // Phụ thuộc vào HttpClient

class AuthManager {
public:
    AuthManager(HttpClient& httpClient, const std::string& username, const std::string& password);
    // Trả về true nếu login thành công hoặc đã có token hợp lệ (cần kiểm tra thêm)
    bool ensureAuthenticated(); 
    std::optional<std::string> getToken(); // Lấy token hiện tại
    void invalidateToken(); // Vô hiệu hóa token hiện tại
private:
    HttpClient& http_client_;
    std::string username_;
    std::string password_;
    std::string current_token_;
    // Poco::Timestamp token_expiry_time_; // Để quản lý hết hạn token

    bool login(); // Hàm private để thực hiện login
};