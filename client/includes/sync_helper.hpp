#pragma once

#include "config_reader.h"      // Để đọc cấu hình ban đầu
#include "http_client.hpp"      // Để tương tác với server API
#include "auth_manager.hpp"     // Để quản lý đăng nhập và token
#include "local_file_system.hpp"// Để thao tác file cục bộ
#include "file_watcher_helper.hpp"// Để có thể bỏ qua sự kiện inotify
#include "utils.hpp"            // Cho các hàm tiện ích như trim
#include "json.hpp"             // nlohmann/json

#include <string>
#include <vector>
#include <optional>
#include <memory>    // Cho std::unique_ptr
#include <fstream>   // Cho std::ifstream, std::ofstream
#include <sstream>   // Cho std::stringstream
#include <algorithm> // Cho std::find, std::remove
#include <stdexcept> // Cho std::runtime_error

// Forward declaration nếu FileWatcherHelper chỉ cần con trỏ/tham chiếu ở đây
// class FileWatcherHelper; // Nếu không include đầy đủ file_watcher_helper.hpp

namespace app {
    using json = nlohmann::json;
    // Cấu trúc để lưu trữ trạng thái file đã biết trên server (hoặc trạng thái đã đồng bộ)
    struct AppData {
        // Key: đường dẫn tương đối (chuẩn hóa Unix-style)
        // Value: có thể là struct chứa thêm metadata như server_timestamp, server_checksum nếu cần
        // Hiện tại, chỉ lưu danh sách đường dẫn để giữ đơn giản như code gốc của bạn
        std::vector<std::string> paths_on_server;
    };

    // Hàm serialize/deserialize cho AppData với nlohmann::json
    inline void to_json(json &j, const AppData &p) {
        j = json{{"paths_on_server", p.paths_on_server}};
    }

    inline void from_json(const json &j, AppData &p) {
        if (j.contains("paths_on_server") && j.at("paths_on_server").is_array()) {
            j.at("paths_on_server").get_to(p.paths_on_server);
        } else {
            p.paths_on_server.clear(); // Hoặc throw lỗi nếu trường này là bắt buộc
            // std::cerr << "Warning: 'paths_on_server' not found or not an array in app_data.json" << std::endl;
        }
    }
} // namespace app

class SyncHelper {
    using json = nlohmann::json;

public:
    // Constructor nhận tham chiếu đến FileWatcherHelper để có thể bỏ qua sự kiện
    SyncHelper(FileWatcherHelper& watcher, const std::string& config_file_path = "config.conf");
    ~SyncHelper();

    // Các hàm public để thực hiện thao tác đồng bộ cụ thể (có thể được gọi từ CLI hoặc UI)
    // pathFromWatcherRoot là đường dẫn TƯƠNG ĐỐI so với watcher_root_path_
    void performUpload(const std::string& pathFromWatcherRoot);
    // serverRelativePath là đường dẫn TƯƠNG ĐỐI trên server
    // localSaveRelativePath là đường dẫn TƯƠNG ĐỐI so với watcher_root_path_ để lưu file
    void performDownload(const std::string& serverRelativePath, const std::string& localSaveRelativePath);
    void performDeleteOnServer(const std::string& serverRelativePath);
    void performRenameOnServer(const std::string& oldServerRelativePath, const std::string& newServerRelativePath);

    // Hàm chính để kích hoạt quá trình đồng bộ dựa trên manifest
    void triggerManifestSync();

    // (Tùy chọn) Lấy danh sách file từ server (có thể dùng để so sánh hoặc cho UI)
    // std::optional<std::vector<std::string>> get_files_list_from_server(const std::string& serverRelativePath);

private:
    FileWatcherHelper& watcher_; // Tham chiếu đến watcher để bỏ qua sự kiện

    std::unique_ptr<HttpClient> http_client_;
    std::unique_ptr<AuthManager> auth_manager_;
    std::unique_ptr<LocalFileSystem> local_fs_;

    std::string watcher_root_path_; // Đường dẫn tuyệt đối đến thư mục gốc đang theo dõi cục bộ
    app::AppData app_data_;         // Trạng thái file đã biết (từ app_data.json)
    std::string app_data_file_path_; // Đường dẫn đến file app_data.json

    // Hàm private để quản lý app_data.json
    void loadAppData();
    void saveAppData();
    void addPathToAppData(const std::string& relativePath);
    void removePathFromAppData(const std::string& relativePath);

    // Hàm private để thực hiện các bước trong triggerManifestSync
    json buildClientManifest();
    void processServerOperations(const json& operationsArray);
};