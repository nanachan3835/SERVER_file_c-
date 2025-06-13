#pragma once

#include <string>
#include <filesystem>

// Một struct để chứa các biến cấu hình toàn cục
// Các giá trị này sẽ được load từ file config.properties lúc khởi động server
struct Config {
    // Server
    static unsigned short HTTP_SERVER_PORT;
    static std::string SERVER_BASE_URL;

    // Paths
    static std::string DATABASE_PATH;
    static std::string USER_DATA_ROOT;
    static std::string SHARED_DATA_ROOT;

    // Security
    static int PASSWORD_SALT_LENGTH;
    static int HASH_ITERATIONS;
};

// Thêm dòng sau vào cuối struct hoặc ngoài struct:
void loadConfigFromFile(const std::string& filePath);
