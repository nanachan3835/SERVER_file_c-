#include "config_reader.h"
#include "file_watcher.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

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

int main() {
    // ! note to my future self:
    // * Delete = IN_DELETE but may be IN_MOVED_TO if file is moved to trash, be careful to handle this GNOME
    // behavior
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
