#include "config_reader.h"
#include "file_watcher.h"
#include "sync.h"

#include "json.hpp"
#include "utils.hpp"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

const char *get_watch_dir() {
    Config *config = config_read("config.conf");
    const char *watch_dir = config_get(config, "watcher_root");
    if (!watch_dir) {
        std::cerr << "Không tìm thấy watcher_root trong file cấu hình.\n";
        std::runtime_error("Không tìm thấy watcher_root trong file cấu hình.");
    }
    if (watch_dir[0] != '/') {
        std::cerr << (stderr, "watcher_root phải là đường dẫn tuyệt đối.\n");
        std::runtime_error("watcher_root phải là đường dẫn tuyệt đối.");
    }
    if (access(watch_dir, F_OK) != 0) {
        auto errMsg = std::string("Thư mục ") + watch_dir + " không tồn tại.\n";
        std::cerr << errMsg;
        std::runtime_error(errMsg.c_str());
    }
    if (access(watch_dir, R_OK | W_OK) != 0) {
        auto errMsg = std::string("Không có quyền đọc/ghi thư mục ") + watch_dir + ".\n";
        std::cerr << errMsg;
        std::runtime_error(errMsg.c_str());
    }
    config_free(config);
    return watch_dir;
}

class FileWatcherHelper {
  public:
    enum class FileEvent {
        CREATED,
        MODIFIED,
        DELETED,
        MOVED_TO,
        MOVED_FROM,
        CLOSED_WRITE,
    };
    using Callback = std::function<void(FileEvent, const std::string &pathFromWatcherRoot)>;

    FileWatcherHelper(const std::string &watcherRootPath) : fd(inotify_init1(IN_NONBLOCK)) {
        if (fd < 0) {
            auto errMsg = std::string("inotify_init failed: ") + strerror(errno) + "\n";
            std::cerr << errMsg;
            std::runtime_error(errMsg.c_str());
        }
        watching = true;
        add_watch_recursive(fd, watcherRootPath.c_str());
        // Bắt đầu luồng theo dõi
        watcher_thread = std::thread(&FileWatcherHelper::start_watching, this);
    }

    ~FileWatcherHelper() {
        watching = false;
        if (watcher_thread.joinable()) {
            watcher_thread.join();
        }
        free_watch_list(fd);
        close(fd);
    }

    void add_callback(Callback cb) { callbacks.push_back(std::move(cb)); }

  private:
    std::vector<Callback> callbacks;
    char buffer[EVENT_BUF_LEN];
    int fd;
    std::atomic<bool> watching;
    std::thread watcher_thread;

    void notify_callbacks(FileEvent event, const std::string &pathFromWatcherRoot) {
        for (const auto &cb : callbacks) {
            cb(event, pathFromWatcherRoot);
        }
    }

    void handle_event_create(inotify_event *event, const std::string &full_path) {
        std::cout << "Created: " << full_path << std::endl;
        notify_callbacks(FileEvent::CREATED, full_path);
        if (event->mask & IN_ISDIR) {
            // Nếu là taoj thư mục, thêm watch đệ quy
            add_watch_recursive(fd, full_path.c_str());
        }
    }

    void start_watching() {
        while (true) {
            if (!watching) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                std::cout << "Đang tạm dừng theo dõi...\n";
                continue;
            }
            int length = read(fd, buffer, EVENT_BUF_LEN);
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                std::cerr << "read() failed: " << strerror(errno) << "\n";
                break;
            }

            int event_buf_offset = 0;
            while (watching && event_buf_offset < length) {
                inotify_event *event = reinterpret_cast<inotify_event *>(&buffer[event_buf_offset]);
                const char *base_path = get_path_from_wd(event->wd);
                if (event->len && base_path) {
                    const std::string full_path = std::string(base_path) + "/" + event->name;
                    if (event->mask & IN_CREATE) {
                        handle_event_create(event, full_path);
                    }
                    if (event->mask & IN_MODIFY) {
                        notify_callbacks(FileEvent::MODIFIED, full_path);
                    }
                    if (event->mask & IN_DELETE) {
                        notify_callbacks(FileEvent::DELETED, full_path);
                    }
                    if (event->mask & IN_MOVED_TO) {
                        notify_callbacks(FileEvent::MOVED_TO, full_path);
                    }
                    if (event->mask & IN_MOVED_FROM) {
                        notify_callbacks(FileEvent::MOVED_FROM, full_path);
                    }

                    if (event->mask & IN_CLOSE_WRITE) {
                        notify_callbacks(FileEvent::CLOSED_WRITE, full_path);
                    }
                } else {
                    std::cerr << "Không tìm thấy đường dẫn cho wd: " << event->wd << "\n";
                }
                event_buf_offset += EVENT_SIZE + event->len;
            }
        }
    }
};

namespace app {
struct AppData {
    std::vector<std::string> paths_on_server;
};
void to_json(json &j, const AppData &p) { j = json{{"paths_on_server", p.paths_on_server}}; }

void from_json(const json &j, AppData &p) { j.at("paths_on_server").get_to(p.paths_on_server); }
} // namespace app
class SyncHelper {
  public:
    SyncHelper(const char *serverUrl, const char *username, const char *password) {
        server = init_sync_server(serverUrl, username, password);
        if (!server) {
            throw std::runtime_error("Không thể khởi tạo máy chủ đồng bộ hóa.");
        }
        app_data = {};
        get_app_data();
    }

    ~SyncHelper() {
        if (server) {
            free_sync_server(server);
            server = nullptr;
        }
        if (app_data_file.is_open()) {
            save_app_data();
            app_data_file.close();
        }
    }

    void upload(const char *pathFromWatcherRoot) {
        SyncErrorCode sync_errno = sync_upload(server, pathFromWatcherRoot);

        throw_if_error(sync_errno, pathFromWatcherRoot);
    }
    SyncErrorCode download(const char *path) { return sync_download(server, path); }
    SyncErrorCode delete_file(const char *path) { return sync_delete(server, path); }
    SyncErrorCode get_file_list(const char *path, char *responseBuffer, size_t bufferSize) {
        return sync_get_file_list(server, path, responseBuffer, bufferSize);
    }

  private:
    Server *server;
    app::AppData app_data;
    std::fstream app_data_file;

    void throw_if_error(SyncErrorCode code, const std::string &context) {
        switch (code) {
        case SYNC_SUCCESS:
            // std::cout << "Tải lên thành công: " << context << std::endl;
            break;
        case SYNC_ERROR_INVALID_URL:
            std::runtime_error("Lỗi: Địa chỉ máy chủ không hợp lệ");
            break;
        case SYNC_ERROR_CONNECTION_FAILED:
            std::runtime_error("Lỗi: Kết nối đến máy chủ không thành công.");
            break;
        case SYNC_ERROR_AUTH_FAILED:
            std::runtime_error("Lỗi: Xác thực không thành công.");
            break;
        case SYNC_ERROR_TIMEOUT:
            std::runtime_error("Lỗi: Thời gian chờ hết hạn khi kết nối đến máy chủ.");
            break;
        case SYNC_ERROR_FILE_NOT_FOUND:
            std::runtime_error(std::string("Lỗi: Tệp không tìm thấy: ") + context);
            break;
        case SYNC_ERROR_UNKNOWN:
            std::runtime_error(std::string("Lỗi: Lỗi không xác định khi tải lên tệp: ") + context);
            break;
        default:
            std::runtime_error(std::string("Lỗi không xác định: ") + sync_strerror(code));
            break;
        }
    }

    void get_app_data() {
        if (!app_data_file.is_open()) {
            app_data_file.open("app_data.json");
            if (!app_data_file) {
                throw std::runtime_error("Không thể mở file app_data.json");
            }
        }

        std::stringstream buffer;
        buffer << app_data_file.rdbuf();

        std::string content = buffer.str();

        trim(content);

        if (content.empty()) {
            app_data.paths_on_server = {};
        } else {
            json data = json::parse(app_data_file);
            app_data.paths_on_server = data.template get<std::vector<std::string>>();
        }
    }

    void save_app_data() {
        if (!app_data_file.is_open()) {
            app_data_file.open("app_data.json", std::ios::out | std::ios::trunc);
            if (!app_data_file) {
                throw std::runtime_error("Không thể mở file app_data.json để ghi");
            }
        }

        json j = app_data;
    }
};

int main() {
    // ! note to my future self:
    // * Delete = IN_DELETE but may be IN_MOVED_TO if file is moved to trash, be careful to handle this GNOME
    // * behavior
    // * Rename = IN_MOVED_TO then IN_MOVED_FROM in 2 continuous but different callbacks
    // * so callback should not directly call sync functions
    // * instead callback should append to queue buffer then sync functions should handle the queue
    const char *watch_dir = get_watch_dir();
    std::cout << "Đang theo dõi thư mục: " << watch_dir << std::endl;
    FileWatcherHelper watcher(watch_dir);
    watcher.add_callback([](FileWatcherHelper::FileEvent event, const std::string &path) {
        switch (event) {
        case FileWatcherHelper::FileEvent::CREATED:
            std::cout << "Callback: File created: " << path << std::endl;
            break;
        case FileWatcherHelper::FileEvent::MODIFIED:
            std::cout << "Callback: File modified: " << path << std::endl;
            break;
        case FileWatcherHelper::FileEvent::DELETED:
            std::cout << "Callback: File deleted: " << path << std::endl;
            break;
        case FileWatcherHelper::FileEvent::CLOSED_WRITE:
            std::cout << "Callback: File CLOSED_WRITE: " << path << std::endl;
            break;
        case FileWatcherHelper::FileEvent::MOVED_TO:
            std::cout << "Callback: File moved to here: " << path << std::endl;
            break;
        case FileWatcherHelper::FileEvent::MOVED_FROM:
            std::cout << "Callback: File moved away: " << path << std::endl;
            break;
        default:
            std::cerr << "Callback: Unknown event for path: " << path << std::endl;
            break;
        }
    });

    while (true) {
        // Giữ chương trình chạy để theo dõi sự kiện
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
