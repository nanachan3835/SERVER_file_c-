#pragma once

#include "config_reader.h"
#include "sync.h"
#include "utils.hpp"

#include "json.hpp"
#include <fstream>
#include <vector>

namespace app {
using json = nlohmann::json;
struct AppData {
    std::vector<std::string> paths_on_server;
};
//* note inline here, if error in future, check this again
inline void to_json(json &j, const AppData &p) { j = json{{"paths_on_server", p.paths_on_server}}; }

inline void from_json(const json &j, AppData &p) { j.at("paths_on_server").get_to(p.paths_on_server); }
} // namespace app

class SyncHelper {
    using json = nlohmann::json;

  public:
    SyncHelper() : app_data({}) {
        Config *config = config_read("config.conf");
        if (!config) {
            throw std::runtime_error("Không thể đọc file cấu hình.");
        }
        const char *serverUrl = config_get(config, "server_url");
        if (!serverUrl) {
            throw std::runtime_error("Không tìm thấy server_url trong file cấu hình.");
        }
        const char *username = config_get(config, "username");
        if (!username) {
            throw std::runtime_error("Không tìm thấy username trong file cấu hình.");
        }
        const char *password = config_get(config, "password");
        if (!password) {
            throw std::runtime_error("Không tìm thấy password trong file cấu hình.");
        }
        server = init_sync_server(serverUrl, username, password);
        if (!server) {
            throw std::runtime_error("Không thể khởi tạo kết nối đến máy chủ.");
        }
        get_app_data();
        config_free(config);
    }

    ~SyncHelper() {
        if (server) {
            free_sync_server(server);
            server = nullptr;
        }
    }

    void upload(const char *pathFromWatcherRoot) {
        SyncErrorCode sync_errno = sync_upload(server, pathFromWatcherRoot);

        throw_if_error(sync_errno, pathFromWatcherRoot);
        // Cập nhật app_data
        if (std::find(app_data.paths_on_server.begin(), app_data.paths_on_server.end(),
                      pathFromWatcherRoot) == app_data.paths_on_server.end()) {
            app_data.paths_on_server.push_back(pathFromWatcherRoot);
            save_app_data();
        }
    }

    void download(const char *path) {
        SyncErrorCode sync_errno = sync_download(server, path);
        throw_if_error(sync_errno, path);

        // Cập nhật app_data
        // if (std::find(app_data.paths_on_server.begin(), app_data.paths_on_server.end(), path) ==
        //     app_data.paths_on_server.end()) {
        //     app_data.paths_on_server.push_back(path);
        //     save_app_data();
        // }
    }

    void delete_file(const char *path) {
        SyncErrorCode sync_errno = sync_delete(server, path);
        throw_if_error(sync_errno, path);

        // Cập nhật app_data
        auto it = std::find(app_data.paths_on_server.begin(), app_data.paths_on_server.end(), path);
        if (it != app_data.paths_on_server.end()) {
            app_data.paths_on_server.erase(it);
            save_app_data();
        }
    }

    void rename(const char *oldPath, const char *newPath) {
        SyncErrorCode sync_errno = sync_rename(server, oldPath, newPath);
        throw_if_error(sync_errno, oldPath);
        // Cập nhật app_data
        auto it = std::find(app_data.paths_on_server.begin(), app_data.paths_on_server.end(), oldPath);
        if (it != app_data.paths_on_server.end()) {
            *it = newPath;
            save_app_data();
        }
    }

    std::vector<std::string> get_files_list(const char *path) {
        constexpr size_t bufferSize = 64 * 1024; // 64KB buffer
        char responseBuffer[bufferSize];
        SyncErrorCode sync_errno = sync_get_files_list_json(server, path, responseBuffer, bufferSize);
        throw_if_error(sync_errno, path);
        std::string response(responseBuffer);
        json response_json = json::parse(response);

        std::vector<std::string> paths;
        if (response_json.contains("listing") && response_json["listing"].is_array()) {
            for (const auto &item : response_json["listing"]) {
                if (item.contains("path") && item["path"].is_string()) {
                    paths.push_back(item["path"].get<std::string>());
                } else
                    throw std::runtime_error("Lỗi: Phản hồi json có định dạng không hợp lệ từ máy chủ.");
            }
        } else
            throw std::runtime_error("Lỗi: Phản hồi json có định dạng không hợp lệ từ máy chủ.");

        return paths;
    }

  private:
    Server *server = nullptr;
    app::AppData app_data;
    const char *app_data_file_path = "app_data.json";

    void throw_if_error(SyncErrorCode code, const std::string &context) {
        switch (code) {
        case SYNC_SUCCESS:
            return;
        case SYNC_ERROR_INVALID_URL:
            throw std::runtime_error("Lỗi: Địa chỉ máy chủ không hợp lệ");
            break;
        case SYNC_ERROR_CONNECTION_FAILED:
            throw std::runtime_error("Lỗi: Kết nối đến máy chủ không thành công.");
            break;
        case SYNC_ERROR_AUTH_FAILED:
            throw std::runtime_error("Lỗi: Xác thực không thành công.");
            break;
        case SYNC_ERROR_TIMEOUT:
            throw std::runtime_error("Lỗi: Thời gian chờ hết hạn khi kết nối đến máy chủ.");
            break;
        case SYNC_ERROR_FILE_NOT_FOUND:
            throw std::runtime_error(std::string("Lỗi: Tệp không tìm thấy: ") + context);
            break;
        case SYNC_ERROR_UNKNOWN:
            throw std::runtime_error(std::string("Lỗi: Lỗi không xác định khi tải lên tệp: ") + context);
            break;
        default:
            throw std::runtime_error(std::string("Lỗi không xác định: ") + sync_strerror(code));
            break;
        }
    }

    void get_app_data() {
        std::ifstream app_data_file(app_data_file_path);
        if (!app_data_file) {
            throw std::runtime_error("Không thể mở file app data");
        }

        std::stringstream buffer;
        buffer << app_data_file.rdbuf();

        std::string content = buffer.str();

        trim(content);

        if (content.empty()) {
            app_data.paths_on_server = {};
        } else {
            json data = json::parse(content);
            app_data = data.get<app::AppData>();
        }
    }

    void save_app_data() {
        std::ofstream app_data_file(app_data_file_path, std::ios::out | std::ios::trunc);
        if (!app_data_file) {
            throw std::runtime_error("Không thể mở file app data.");
        }

        json j = app_data;
        app_data_file << j.dump(4);
        if (!app_data_file) {
            throw std::runtime_error("Lỗi khi ghi vào file app_data.");
        }
        app_data_file.close();
    }
};