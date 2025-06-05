#include "config_reader.h"
#include "sync_helper.hpp"
#include "file_watcher_helper.hpp"
#include "utils.hpp"

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

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

int main() {
    // ! note to my future self:
    // * Delete = IN_DELETE but may be IN_MOVED_TO if file is moved to trash, be careful to handle this GNOME
    // * behavior
    // * Rename = IN_MOVED_TO then IN_MOVED_FROM in 2 continuous but different callbacks
    // * so callback should not directly call sync functions
    // * instead callback should append to queue buffer then sync functions should handle the queue
    const char *watch_dir = get_watch_dir();
    SyncHelper sync_helper;
    TSQueue<FileWatcherHelper::WatchEvent> watch_events_buffer;
    int last_watch_time = 0;
    sync_helper.get_files_list(watch_dir);

    //* do something with the files list

    std::cout << "Đang theo dõi thư mục: " << watch_dir << std::endl;
    FileWatcherHelper watcher(watch_dir);
    watcher.add_callback([&](FileWatcherHelper::WatchEvent event) {
        switch (event.inotify_event) {
        case FileWatcherHelper::InotifyEvent::CREATED:
            std::cout << "Callback: File created: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::MODIFIED:
            std::cout << "Callback: File modified: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::DELETED:
            std::cout << "Callback: File deleted: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::CLOSED_WRITE:
            std::cout << "Callback: File CLOSED_WRITE: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::MOVED_TO:
            std::cout << "Callback: File moved to here: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::MOVED_FROM:
            std::cout << "Callback: File moved away: " << event.path_from_watcher_root << std::endl;
            break;
        default:
            std::cerr << "Callback: Unknown event for path: " << event.path_from_watcher_root << std::endl;
            break;
        }
        last_watch_time = time(nullptr);
    });

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (time(nullptr) - last_watch_time < 1) {
            // recent activity, skip processing
            std::cout << "gần đây vừa có sự thay đổi file, tạm dừng xử lý hàng đợi sự kiện" << std::endl;
            continue;
        }
        try {
            sync_helper.process_watch_events(watch_events_buffer);
        } catch (const std::runtime_error &e) {
            std::cerr << "Lỗi runtime trong quá trình đồng bộ: " << e.what() << std::endl;
        }
    }
}
