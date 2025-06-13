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
        ApiResponse me_res = http_client_.getCurrentUser(current_token_);
        if (me_res.isSuccess()) {
            return true;
        }

        // --- SỬA LOGIC Ở ĐÂY ---
        if (me_res.error_code == ClientSyncErrorCode::ERROR_AUTH_FAILED) { // Chỉ invalidate khi lỗi 401
            std::cerr << "[AuthManager] Token is invalid (401). Invalidating." << std::endl;
            invalidateToken();
        } else {
            // Với các lỗi khác (mạng, server 500), không invalidate token, chỉ báo lỗi và thử lại sau
            std::cerr << "[AuthManager] Token verification failed with non-auth error: " << me_res.error_message << std::endl;
            return false; // Trả về false để không tiếp tục login ngay, tránh bão request khi mạng lỗi
        }
        // --- KẾT THÚC SỬA LOGIC ---
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