#include "config_reader.h"
#include "file_watcher_helper.hpp"
#include "sync_helper.hpp"

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
        throw std::runtime_error("Không tìm thấy watcher_root trong file cấu hình.");
    }
    if (watch_dir[0] != '/') {
        std::cerr << "watcher_root phải là đường dẫn tuyệt đối.\n";
        throw std::runtime_error("watcher_root phải là đường dẫn tuyệt đối.");
    }
    if (access(watch_dir, F_OK) != 0) {
        auto errMsg = std::string("Thư mục ") + watch_dir + " không tồn tại.\n";
        std::cerr << errMsg;
        throw std::runtime_error(errMsg.c_str());
    }
    if (access(watch_dir, R_OK | W_OK) != 0) {
        auto errMsg = std::string("Không có quyền đọc/ghi thư mục ") + watch_dir + ".\n";
        std::cerr << errMsg;
        throw std::runtime_error(errMsg.c_str());
    }
    config_free(config);
    return watch_dir;
}

int main() {
    const char *watch_dir = get_watch_dir();
    // SyncHelper sync_helper;
    // sync_helper.get_files_list(watch_dir);

    //* do something with the files list

    std::cout << "Đang theo dõi thư mục: " << watch_dir << std::endl;
    FileWatcherHelper watcher(watch_dir);
    watcher.add_callback([&](FileWatcherHelper::WatchEvent event) {
        switch (event.inotify_event) {
        case FileWatcherHelper::InotifyEvent::CREATED:
            std::cout << "Callback: File created: " << event.path_from_watcher_root << std::endl;
            break;
        case FileWatcherHelper::InotifyEvent::MODIFIED: // not use
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
        case FileWatcherHelper::InotifyEvent::RENAME:
            std::cout << "Callback: File renamed from " << event.path_from_watcher_root << " to "
                      << event.rename_to << std::endl;
            // sync_helper.rename(event.path_from_watcher_root.c_str(), event.rename_to.c_str());
            break;
        default:
            std::cerr << "Callback: Unknown event for path: " << event.path_from_watcher_root << std::endl;
            break;
        }
    });

    while (true) {
        // keep the program running to watch for file changes
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
