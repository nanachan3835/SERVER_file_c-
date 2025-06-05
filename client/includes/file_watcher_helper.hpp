#pragma once

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

class FileWatcherHelper {
  public:
    enum class InotifyEvent {
        CREATED,
        MODIFIED,
        DELETED,
        MOVED_TO,
        MOVED_FROM,
        CLOSED_WRITE,
    };
    struct WatchEvent {
        InotifyEvent inotify_event;
        std::string path_from_watcher_root;
    };
    using Callback = std::function<void(WatchEvent)>;

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

    void notify_callbacks(InotifyEvent event, const std::string &pathFromWatcherRoot) {
        for (const auto &cb : callbacks) {
            cb({event, pathFromWatcherRoot});
        }
    }

    void handle_event_create(inotify_event *event, const std::string &full_path) {
        std::cout << "Created: " << full_path << std::endl;
        notify_callbacks(InotifyEvent::CREATED, full_path);
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
                        notify_callbacks(InotifyEvent::MODIFIED, full_path);
                    }
                    if (event->mask & IN_DELETE) {
                        notify_callbacks(InotifyEvent::DELETED, full_path);
                    }
                    if (event->mask & IN_MOVED_TO) {
                        notify_callbacks(InotifyEvent::MOVED_TO, full_path);
                    }
                    if (event->mask & IN_MOVED_FROM) {
                        notify_callbacks(InotifyEvent::MOVED_FROM, full_path);
                    }

                    if (event->mask & IN_CLOSE_WRITE) {
                        notify_callbacks(InotifyEvent::CLOSED_WRITE, full_path);
                    }
                } else {
                    std::cerr << "Không tìm thấy đường dẫn cho wd: " << event->wd << "\n";
                }
                event_buf_offset += EVENT_SIZE + event->len;
            }
        }
    }
};
