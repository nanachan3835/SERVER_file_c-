#include "auth_manager.hpp"
#include "protocol.hpp" // Cho JsonKeys
#include <iostream>

AuthManager::AuthManager(HttpClient& httpClient, const std::string& username, const std::string& password)
    : http_client_(httpClient), username_(username), password_(password) {}

bool AuthManager::login() {
    std::cout << "Attempting login for user: " << username_ << std::endl;
    ApiResponse res = http_client_.login(username_, password_);
    std::cout << "username: " << username_ << " password: " << password_ << std::endl;
    if (res.statusCode == Poco::Net::HTTPResponse::HTTP_OK && res.body.is_object() &&
        res.body.contains("data") && res.body["data"].is_object() &&
        res.body["data"].contains(JsonKeys::TOKEN)) {
        
        current_token_ = res.body["data"][JsonKeys::TOKEN].get<std::string>();
        // TODO: Lấy user_id, home_dir nếu cần lưu ở AuthManager
        // TODO: Xử lý token expiry nếu server trả về
        std::cout << "Login successful. Token received." << std::endl;
        return true;
    }
    std::cerr << "Login failed: " << res.error_message << " (Status: " << res.statusCode << ")" << std::endl;
    current_token_.clear();
    return false;
}


void AuthManager::invalidateToken() { // Đảm bảo hàm này được định nghĩa
    if (!current_token_.empty()) {
        std::cout << "[AuthManager] Token invalidated by client." << std::endl;
        current_token_.clear();
    }
}


bool AuthManager::ensureAuthenticated() {
    if (!current_token_.empty()) {
        std::cout << "[AuthManager] Token exists. Verifying with server..." << std::endl;
        // Gọi một API nhẹ để kiểm tra token, ví dụ /users/me
        ApiResponse me_res = http_client_.getCurrentUser(current_token_);
        if (me_res.isSuccess()) { // isSuccess() kiểm tra cả status code và error_code
            std::cout << "[AuthManager] Token is still valid." << std::endl;
            return true;// Tạm thời coi token đã có là hợp lệ
    }
    // Nếu /users/me trả về lỗi (đặc biệt là 401), token không còn hợp lệ
        std::cerr << "[AuthManager] Token verification failed (Code: " << me_res.statusCode
                  << "). Error: " << me_res.error_message << ". Invalidating current token." << std::endl;
        invalidateToken(); //

}
    std::cout << "[AuthManager] No valid token, attempting new login." << std::endl;
    return login();
}

std::optional<std::string> AuthManager::getToken() {
    if (current_token_.empty()) {
        return std::nullopt;
    }
    return current_token_;
}