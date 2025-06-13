#include "sync_helper.hpp"
#include "protocol.hpp" // Cho JsonKeys
#include <Poco/Path.h>   // Cho Poco::Path để chuẩn hóa
#include <iostream>      // Cho std::cout, std::cerr
#include "http_client.hpp" 
#include <filesystem>
#include <chrono>
#include <format> 
namespace fs = std::filesystem;


SyncHelper::SyncHelper(FileWatcherHelper& watcher, const std::string& config_file_path)
    : watcher_(watcher), app_data_file_path_("app_data.json") { // Khởi tạo watcher_
    Config *config = config_read(config_file_path.c_str());
    if (!config) {
        throw std::runtime_error("SyncHelper: Không thể đọc file cấu hình: " + config_file_path);
    }

    const char *serverUrl_c = config_get(config, "server_url");
    const char *username_c = config_get(config, "username");
    const char *password_c = config_get(config, "password");
    const char *watcher_root_c = config_get(config, "watcher_root");

    if (!serverUrl_c || !username_c || !password_c || !watcher_root_c) {
        config_free(config);
        throw std::runtime_error("SyncHelper: Thiếu server_url, username, password, hoặc watcher_root trong config.");
    }
    std::string server_url_str = serverUrl_c;
    std::string username_str = username_c;
    std::string password_str = password_c;
    watcher_root_path_ = watcher_root_c; // Lưu đường dẫn gốc cục bộ
    config_free(config);

    http_client_ = std::make_unique<HttpClient>(server_url_str);
    auth_manager_ = std::make_unique<AuthManager>(*http_client_, username_str, password_str);
    local_fs_ = std::make_unique<LocalFileSystem>();

    if (!auth_manager_->ensureAuthenticated()) {
        // Không throw ở đây, nhưng main.cpp có thể quyết định thoát nếu login ban đầu thất bại
        std::cerr << "SyncHelper: Đăng nhập ban đầu thất bại. Client có thể không hoạt động đúng." << std::endl;
    }
    loadAppData();
}




SyncHelper::~SyncHelper() {
    // HttpClient, AuthManager, LocalFileSystem sẽ tự giải phóng qua unique_ptr
    // Có thể muốn gọi logout ở đây nếu có token
    auto token_opt = auth_manager_->getToken();
    if (token_opt && http_client_) {
        // std::cout << "[SyncHelper] Logging out on exit..." << std::endl;
        // http_client_->logout(*token_opt); // Tùy chọn
    }
}






void SyncHelper::performUpload(const std::string& pathFromWatcherRoot) {
    if (!auth_manager_->ensureAuthenticated()) {
        throw std::runtime_error("SyncHelper: Cần đăng nhập để upload.");
    }
    std::string token = *(auth_manager_->getToken()); // Nên kiểm tra optional trước khi dereference
    fs::path local_full_path = fs::path(watcher_root_path_) / pathFromWatcherRoot;

    std::cout << "[SyncHelper] Uploading: " << pathFromWatcherRoot << " (Local: " << local_full_path.string() << ")" << std::endl;
    ApiResponse res = http_client_->uploadFile(token, local_full_path.string(), pathFromWatcherRoot);

    if (res.statusCode == Poco::Net::HTTPResponse::HTTP_CREATED || res.statusCode == Poco::Net::HTTPResponse::HTTP_OK) {
        std::cout << "Upload thành công: " << pathFromWatcherRoot << std::endl;
        addPathToAppData(pathFromWatcherRoot);
        // TODO: Cập nhật metadata cục bộ (timestamp, checksum từ server nếu có) trong SyncStateManager
    } else {
        throw std::runtime_error("Lỗi upload '" + pathFromWatcherRoot + "': " + res.error_message + " (Code: " + std::to_string(res.statusCode) + ")");
    }
}








void SyncHelper::performDownload(const std::string& serverRelativePath, const std::string& localSaveRelativePath) {
    std::cout << "[SyncHelper] Attempting to ensure authentication for download..." << std::endl;
    if (!auth_manager_->ensureAuthenticated()) {
        throw std::runtime_error("SyncHelper: Cần đăng nhập để download (lần 1).");
    }
    auto token_opt = auth_manager_->getToken();
    if (!token_opt) {
        throw std::runtime_error("SyncHelper: Không lấy được token để download (lần 1).");
    }
    std::string token = *token_opt;

    fs::path local_full_save_path = fs::path(watcher_root_path_) / localSaveRelativePath;
    std::cout << "[SyncHelper] Downloading: '" << serverRelativePath << "' to '" << local_full_save_path.string() << "'" << std::endl;
    
    // Thông báo cho watcher bỏ qua sự kiện tạo/ghi file này
    // watcher_.ignoreEventOnce(localSaveRelativePath); // Đã gọi ở processServerOperations

    ClientSyncErrorCode dl_res = http_client_->downloadFile(token, serverRelativePath, local_full_save_path.string());

    if (dl_res == ClientSyncErrorCode::ERROR_AUTH_FAILED) {
        std::cerr << "[SyncHelper] Download '" << serverRelativePath << "' nhận lỗi 401. Thử đăng nhập lại." << std::endl;
        auth_manager_->invalidateToken();
        if (!auth_manager_->ensureAuthenticated()) {
            throw std::runtime_error("SyncHelper: Đăng nhập lại thất bại sau lỗi 401 khi download " + serverRelativePath);
        }
        token_opt = auth_manager_->getToken();
        if (!token_opt) throw std::runtime_error("SyncHelper: Không lấy được token mới sau khi đăng nhập lại cho download.");
        token = *token_opt;
        std::cout << "[SyncHelper] Thử download lại '" << serverRelativePath << "' với token mới." << std::endl;
        // watcher_.ignoreEventOnce(localSaveRelativePath); // Gọi lại ignore vì request trước thất bại và đây là lần thử mới
        dl_res = http_client_->downloadFile(token, serverRelativePath, local_full_save_path.string());
    }

    if (dl_res == ClientSyncErrorCode::SUCCESS) {
        std::cout << "Download thành công: " << serverRelativePath << std::endl;
        addPathToAppData(serverRelativePath); // File này giờ đã có cục bộ và (hy vọng) khớp server
        // TODO: Cập nhật metadata chi tiết hơn trong SyncStateManager (timestamp, checksum từ server)
        //       nếu HttpClient::downloadFile trả về các thông tin này qua header.
    } else {
        // Nếu download thất bại, file cục bộ có thể không được tạo/cập nhật.
        // Không cần lo lắng về việc watcher đã ignore (nếu ignore chỉ áp dụng cho thao tác thành công).
        // Hoặc nếu ignore được đặt trước, nó sẽ tự hết hiệu lực.
        throw std::runtime_error("Lỗi download '" + serverRelativePath + "': " + clientSyncErrorCodeToString(dl_res));
    }
}

void SyncHelper::performDeleteOnServer(const std::string& serverRelativePath) {
    if (!auth_manager_->ensureAuthenticated()) {
        throw std::runtime_error("SyncHelper: Cần đăng nhập để xóa trên server.");
    }
    std::string token = *(auth_manager_->getToken());
    std::cout << "[SyncHelper] Deleting on server: " << serverRelativePath << std::endl;
    ApiResponse res = http_client_->deletePath(token, serverRelativePath);

    if (res.statusCode == Poco::Net::HTTPResponse::HTTP_OK || res.statusCode == Poco::Net::HTTPResponse::HTTP_NO_CONTENT) {
        std::cout << "Xóa trên server thành công: " << serverRelativePath << std::endl;
        removePathFromAppData(serverRelativePath);
    } else {
        if (res.statusCode == Poco::Net::HTTPResponse::HTTP_NOT_FOUND) {
            std::cout << "File '" << serverRelativePath << "' không tìm thấy trên server (có thể đã xóa)." << std::endl;
            removePathFromAppData(serverRelativePath); // Vẫn xóa khỏi app_data nếu server xác nhận không có
        } else {
            throw std::runtime_error("Lỗi xóa trên server '" + serverRelativePath + "': " + res.error_message + " (Code: " + std::to_string(res.statusCode) + ")");
        }
    }
}

void SyncHelper::performRenameOnServer(const std::string& oldServerRelativePath, const std::string& newServerRelativePath) {
    if (!auth_manager_->ensureAuthenticated()) {
        throw std::runtime_error("SyncHelper: Cần đăng nhập để đổi tên trên server.");
    }
    std::string token = *(auth_manager_->getToken());
    std::cout << "[SyncHelper] Renaming on server: " << oldServerRelativePath << " -> " << newServerRelativePath << std::endl;
    ApiResponse res = http_client_->renamePath(token, oldServerRelativePath, newServerRelativePath);

    if (res.statusCode == Poco::Net::HTTPResponse::HTTP_OK) {
        std::cout << "Đổi tên trên server thành công." << std::endl;
        removePathFromAppData(oldServerRelativePath);
        addPathToAppData(newServerRelativePath);
        // Không cần gọi saveAppData() ở đây vì add/remove đã gọi rồi
    } else {
        throw std::runtime_error("Lỗi đổi tên trên server: " + res.error_message + " (Code: " + std::to_string(res.statusCode) + ")");
    }
}

json SyncHelper::buildClientManifest() {
    std::cout << "[SyncHelper] Building client manifest..." << std::endl;
    std::vector<LocalFileInfo> local_files = local_fs_->scanDirectoryRecursive(watcher_root_path_, watcher_root_path_);
    
    json client_manifest_payload = { {JsonKeys::CLIENT_FILES, json::array()} };
    std::set<std::string> local_paths_set;

    for (const auto& local_file : local_files) {
        local_paths_set.insert(local_file.relativePath);
        client_manifest_payload[JsonKeys::CLIENT_FILES].push_back({
            {JsonKeys::RELATIVE_PATH, local_file.relativePath},
            {JsonKeys::LAST_MODIFIED, local_file.lastModifiedPoco.epochTime()},
            {JsonKeys::CHECKSUM, local_file.checksum},
            {JsonKeys::IS_DIRECTORY, local_file.isDirectory}, // Thêm thông tin thư mục
            {"is_deleted", false} // Thêm cờ is_deleted
        });
    }

    // --- THÊM LOGIC PHÁT HIỆN XÓA Ở ĐÂY ---
    for (const std::string& path_in_app_data : app_data_.paths_on_server) {
        if (local_paths_set.find(path_in_app_data) == local_paths_set.end()) {
            // File này có trong app_data nhưng không có trên đĩa -> đã bị xóa
            std::cout << "[Manifest] Detected deleted local file: " << path_in_app_data << std::endl;
            client_manifest_payload[JsonKeys::CLIENT_FILES].push_back({
                {JsonKeys::RELATIVE_PATH, path_in_app_data},
                {"is_deleted", true} // Báo cho server biết file này đã bị xóa
            });
        }
    }
    // --- KẾT THÚC THÊM LOGIC ---

    std::cout << "[SyncHelper] Client manifest built with " << client_manifest_payload[JsonKeys::CLIENT_FILES].size() << " items." << std::endl;
    return client_manifest_payload;
}

/// @brief ////////////////////////////////////////////////////////////////////////
/// @param operationsArray 
void SyncHelper::processServerOperations(const json& operationsArray) {
    std::cout << "[SyncHelper] Processing " << operationsArray.size() << " server operations..." << std::endl;
    if (!auth_manager_->ensureAuthenticated()) { // Đảm bảo xác thực trước khi bắt đầu một loạt operations
        std::cerr << "[SyncHelper] Authentication failed before processing server operations. Aborting." << std::endl;
        return;
    }
    std::vector<json> createDirOps;
    std::vector<json> otherOps;

    for (const auto& op_json : operationsArray) {
       std::string action_str = op_json.value(JsonKeys::SYNC_ACTION_TYPE, "");
    std::string rel_path = op_json.value(JsonKeys::RELATIVE_PATH, "");
    if (rel_path.empty()) continue;

    // QUAN TRỌNG: Luôn dùng đường dẫn đầy đủ để kiểm tra
    fs::path local_full_path = fs::path(watcher_root_path_) / rel_path;

    if (action_str == "UPLOAD_TO_SERVER") {
        // Kiểm tra lại điều kiện fs::exists() để tránh lỗi
        if (fs::exists(local_full_path) && fs::is_directory(local_full_path)) {
            createDirOps.push_back(op_json);
        } else {
            otherOps.push_back(op_json);
        }
    } else {
        otherOps.push_back(op_json);
    }
}

    std::sort(createDirOps.begin(), createDirOps.end(), [](const json& a, const json& b) {
        std::string path_a = a.value(JsonKeys::RELATIVE_PATH, "");
        std::string path_b = b.value(JsonKeys::RELATIVE_PATH, "");
        return std::count(path_a.begin(), path_a.end(), '/') < std::count(path_b.begin(), path_b.end(), '/');
    });
    

    std::cout << "[SyncHelper] Executing " << createDirOps.size() << " CREATE_DIRECTORY operations first..." << std::endl;
    for (const auto& op_json : createDirOps) {
        std::string rel_path = op_json.value(JsonKeys::RELATIVE_PATH, "");
        try {
            std::cout << "[SyncHelper] Creating directory on server: " << rel_path << std::endl;
            ApiResponse res = http_client_->createDirectory(*(auth_manager_->getToken()), rel_path);
            if (!res.isSuccess() && res.error_code != ClientSyncErrorCode::ERROR_CONFLICT) { // Bỏ qua lỗi "đã tồn tại"
                std::cerr << "Failed to create directory '" << rel_path << "': " << res.error_message << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception while creating directory '" << rel_path << "': " << e.what() << std::endl;
        }
    }
    std::cout << "[SyncHelper] Executing " << otherOps.size() << " other operations..." << std::endl;
    for (const auto& op_json : operationsArray) {
        std::string action_str = op_json.value(JsonKeys::SYNC_ACTION_TYPE, "");
        std::string rel_path = op_json.value(JsonKeys::RELATIVE_PATH, "");

        if (rel_path.empty()) {
            std::cerr << "[SyncHelper] Operation missing relative_path, skipping." << std::endl;
            continue;
        }

        fs::path local_full_path_for_op = fs::path(watcher_root_path_) / rel_path;

        std::cout << "[SyncHelper] Server operation: " << action_str << " for '" << rel_path << "'" << std::endl;
        
        try {
            if (action_str == "UPLOAD_TO_SERVER") {
                // Logic này giờ chỉ chạy cho file, vì thư mục đã được lọc ra
                fs::path local_full_path_for_op = fs::path(watcher_root_path_) / rel_path;
                if (fs::exists(local_full_path_for_op)) {
                    performUpload(rel_path);
                } else {
                    std::cerr << "[SyncHelper] Server requested UPLOAD for non-existent local file: " << rel_path << std::endl;
                }
            } else if (action_str == "DOWNLOAD_TO_CLIENT") {
                std::cout << "[SyncHelper] Preparing to download: " << rel_path << std::endl;
                watcher_.ignoreEventOnce(rel_path); // BÁO CHO WATCHER BỎ QUA sự kiện tạo/ghi file này
                performDownload(rel_path, rel_path); // serverRelativePath và localSaveRelativePath là như nhau
            } else if (action_str == "DELETE_ON_CLIENT") {
                std::cout << "[SyncHelper] Preparing to delete local: " << rel_path << std::endl;
                watcher_.ignoreEventOnce(rel_path); // BÁO CHO WATCHER BỎ QUA sự kiện xóa file này
                if (local_fs_->deletePathRecursive(local_full_path_for_op)) {
                    std::cout << "Đã xóa cục bộ theo yêu cầu server: " << rel_path << std::endl;
                    removePathFromAppData(rel_path); // Cập nhật app_data sau khi xóa cục bộ
                } else {
                     std::cerr << "Lỗi xóa cục bộ theo yêu cầu server: " << rel_path << std::endl;
                }
            } else if (action_str == "CONFLICT_SERVER_WINS") {
                std::cout << "[SyncHelper] Conflict: Server wins for '" << rel_path << "'. Preparing to download." << std::endl;
                // TODO: Xử lý conflict tốt hơn (ví dụ đổi tên file cục bộ trước khi download)
                // Hiện tại, chỉ ghi đè.
                fs::path conflict_local_path = local_full_path_for_op; // Đường dẫn file cục bộ hiện tại
                if (fs::exists(conflict_local_path)) {
                    // Tạo tên file conflict
                    std::string stem = conflict_local_path.stem().string();
                    std::string ext = conflict_local_path.extension().string();
                    Poco::Timestamp now;
                    std::string conflict_suffix = "_conflict_local_" + Poco::format("{:%Y%m%d%H%M%S}", now);
                    fs::path renamed_conflict_path = conflict_local_path.parent_path() / (stem + conflict_suffix + ext);
                    
                    std::cout << "[SyncHelper] Renaming local conflicting file to: " << renamed_conflict_path.string() << std::endl;
                    watcher_.ignoreEventOnce(fs::relative(renamed_conflict_path, watcher_root_path_).string()); // Bỏ qua sự kiện tạo file conflict
                    watcher_.ignoreEventOnce(rel_path); // Bỏ qua sự kiện xóa (do rename) và sự kiện tạo (do download)
                    try {
                        local_fs_->renamePath(conflict_local_path, renamed_conflict_path);
                    } catch (const std::exception& e_rename) {
                        std::cerr << "[SyncHelper] Could not rename conflicting local file '" << rel_path << "': " << e_rename.what() << ". Proceeding with overwrite." << std::endl;
                    }
                }
                performDownload(rel_path, rel_path);
            } else if (action_str == "DELETE_ON_SERVER") {
                // Server thông báo file này đã bị xóa trên server, client không cần làm gì với file cục bộ
                // nhưng cần cập nhật app_data_
                std::cout << "[SyncHelper] Server confirms '" << rel_path << "' is deleted on server. Updating local state." << std::endl;
                removePathFromAppData(rel_path);
            } else if (action_str == "NO_ACTION") {
                std::cout << "[SyncHelper] No action needed for: " << rel_path << std::endl;
            } else {
                std::cerr << "[SyncHelper] Unknown server operation: " << action_str << " for " << rel_path << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[SyncHelper] Lỗi khi thực thi operation '" << action_str << "' cho '" << rel_path << "': " << e.what() << std::endl;
        }
    }
}










void SyncHelper::triggerManifestSync() {
    std::cout << "[SyncHelper] Attempting to ensure authentication for manifest sync..." << std::endl;
     if (!auth_manager_->ensureAuthenticated()) { // Gọi ensureAuthenticated trước
        std::cerr << "[SyncHelper] Authentication failed before sending manifest. Cannot proceed." << std::endl;
        // Không nên throw ở đây nếu muốn client tiếp tục chạy và thử lại sau,
        // nhưng cần có cơ chế báo lỗi cho người dùng hoặc ghi log rõ ràng.
        // Hoặc, nếu đây là thao tác quan trọng, có thể throw:
        // throw std::runtime_error("Authentication required for manifest sync and failed.");
        return; // Hoặc return một trạng thái lỗi
    }








    auto token_opt = auth_manager_->getToken();
    if (!token_opt) {
        std::cerr << "[SyncHelper] Failed to get token after ensureAuthenticated for manifest sync." << std::endl;
        return; // Hoặc throw
    }
    std::string token = *token_opt;
    std::cout << "[SyncHelper] Authentication successful. Proceeding with manifest sync with token: "
              << token.substr(0, std::min((size_t)15, token.length())) << "..." << std::endl;


    //std::string token = *(auth_manager_->getToken());
    
    try {
        json client_manifest = buildClientManifest();
        if (client_manifest[JsonKeys::CLIENT_FILES].empty()) {
            std::cout << "[SyncHelper] Manifest rỗng, không có gì để gửi (hoặc chưa xử lý xóa)." << std::endl;
            // Có thể vẫn cần gửi manifest rỗng để server biết client không có file nào (nếu đó là logic)
            // Hoặc nếu đây là lần đầu, server sẽ gửi lại toàn bộ file.
        }

        ApiResponse res = http_client_->postSyncManifest(token, client_manifest);
        if (res.statusCode == Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED) {
            std::cerr << "[SyncHelper] Manifest sync received 401. Token might have expired during operation. Invalidating and retrying login." << std::endl;
            auth_manager_->invalidateToken(); // Vô hiệu hóa token cũ
            if (!auth_manager_->ensureAuthenticated()) { // Thử login lại
                throw std::runtime_error("SyncHelper: Đăng nhập lại thất bại sau lỗi 401 khi gửi manifest.");
            }
            token_opt = auth_manager_->getToken(); // Lấy token mới
            if (!token_opt) {
                 throw std::runtime_error("SyncHelper: Không lấy được token mới sau khi đăng nhập lại.");
            }
            token = *token_opt;
            std::cout << "[SyncHelper] Retrying manifest sync with new token." << std::endl;
            res = http_client_->postSyncManifest(token, client_manifest); // Thử lại request
        }

        if (!res.isSuccess()) {
            std::cerr << "[SyncHelper] Lỗi gửi manifest (sau khi có thể đã thử lại): " << res.error_message << " (Code: " << res.statusCode << ")" << std::endl;
            return;
        }








        if (res.body.contains(JsonKeys::SYNC_OPERATIONS) && res.body[JsonKeys::SYNC_OPERATIONS].is_array()) {
            processServerOperations(res.body[JsonKeys::SYNC_OPERATIONS]);
        } else {
            std::cerr << "[SyncHelper] Phản hồi manifest từ server không hợp lệ hoặc không có operations." << std::endl;
        }
        std::cout << "[SyncHelper] Đồng bộ manifest hoàn tất." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[SyncHelper] Lỗi nghiêm trọng trong triggerManifestSync: " << e.what() << std::endl;
    }
}


// --- Private methods for app_data.json management ---
void SyncHelper::loadAppData() {
    std::ifstream file(app_data_file_path_);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::string content = buffer.str();
        trim(content);
        if (!content.empty()) {
            try {
                json data = json::parse(content);
                app_data_ = data.get<app::AppData>(); // Sử dụng from_json đã định nghĩa
            } catch (const json::exception& e) {
                std::cerr << "Lỗi parse " << app_data_file_path_ << ": " << e.what() << ". Sử dụng danh sách rỗng." << std::endl;
                app_data_.paths_on_server.clear();
            }
        } else {
             app_data_.paths_on_server.clear();
        }
    } else {
        std::cout << "Không tìm thấy file " << app_data_file_path_ << ", sẽ được tạo mới." << std::endl;
        app_data_.paths_on_server.clear();
    }
}

void SyncHelper::saveAppData() {
    std::ofstream file(app_data_file_path_, std::ios::out | std::ios::trunc);
    if (!file) {
        std::cerr << "Lỗi: Không thể mở " << app_data_file_path_ << " để ghi." << std::endl;
        return;
    }
    json j = app_data_; // Sử dụng to_json đã định nghĩa
    file << j.dump(4);
    if (!file.good()) {
        std::cerr << "Lỗi khi ghi vào " << app_data_file_path_ << std::endl;
    }
    file.close();
}

void SyncHelper::addPathToAppData(const std::string& relativePath) {
    Poco::Path p(relativePath);
    std::string normalized_path = p.toString(Poco::Path::PATH_UNIX);
    auto it = std::find(app_data_.paths_on_server.begin(), app_data_.paths_on_server.end(), normalized_path);
    if (it == app_data_.paths_on_server.end()) {
        app_data_.paths_on_server.push_back(normalized_path);
        saveAppData();
    }
}

void SyncHelper::removePathFromAppData(const std::string& relativePath) {
    Poco::Path p(relativePath);
    std::string normalized_path = p.toString(Poco::Path::PATH_UNIX);
    auto new_end = std::remove(app_data_.paths_on_server.begin(), app_data_.paths_on_server.end(), normalized_path);
    if (new_end != app_data_.paths_on_server.end()) {
        app_data_.paths_on_server.erase(new_end, app_data_.paths_on_server.end());
        saveAppData();
    }
}