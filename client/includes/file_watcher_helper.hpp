#pragma once

#include "file_watcher.h"
#include "utils.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include<set>
#ifdef DEBUG
#define LOG(x) std::cout << x << std::endl
#endif

// TODO
// TODO nhớ xóa hết cout sau khi debug xong
// TODO
class FileWatcherHelper {
  public:
    enum class InotifyEvent {
        CREATED,
        MODIFIED, // ko dùng, keep for consumer code compatibility reason
        DELETED,
        MOVED_TO,
        MOVED_FROM,
        CLOSED_WRITE,
        RENAME,
        Q_OVERFLOW // inotify queue overflow
    };
    struct WatchEvent {
        InotifyEvent inotify_event;
        std::string path_from_watcher_root;
        // empty if not rename event
        std::string rename_to;
    };
    using Callback = std::function<void(WatchEvent)>;

    FileWatcherHelper(const std::string &watcherRootPath) {
        inotify_fd = inotify_init1(IN_NONBLOCK);
        if (inotify_fd < 0) {
            auto errMsg = std::string("inotify_init failed: ") + strerror(errno) + "\n";
            std::cerr << errMsg;
            throw std::runtime_error(errMsg.c_str());
        }
        watching = true;
        add_watch_recursive(inotify_fd, watcherRootPath.c_str());
        // Bắt đầu luồng theo dõi
        watcher_thread = std::thread(&FileWatcherHelper::start_watching, this);
    }

    ~FileWatcherHelper() {
        watching = false;
        if (watcher_thread.joinable()) {
            watcher_thread.join();
            free_watch_list(inotify_fd);
        close(inotify_fd);
        }
    }

    FileWatcherHelper(FileWatcherHelper &&) = delete;
    FileWatcherHelper &operator=(FileWatcherHelper &&) = delete;

    void add_callback(Callback cb) { callbacks.push_back(std::move(cb)); }
    void ignoreEventOnce(const std::string& relativePath) {
        std::lock_guard<std::mutex> lock(ignored_paths_mutex_);
        ignored_paths_once_.insert(relativePath);
    }

  private:
    std::set<std::string> ignored_paths_once_;
    std::mutex ignored_paths_mutex_;
    struct PendingRenameEvent {
        std::chrono::steady_clock::time_point timestamp;
        std::string old_path;
    };

    using inotify_cookie_t = uint32_t;

    std::vector<Callback> callbacks;
    char buffer[EVENT_BUF_LEN];
    int inotify_fd;
    std::atomic<bool> watching;
    std::thread watcher_thread;
    std::unordered_map<inotify_cookie_t, PendingRenameEvent> pending_renames;

    constexpr static auto PendingRenameTimeout = std::chrono::seconds(2);

    void notify_callbacks(WatchEvent watch_event) {
        for (const auto &cb : callbacks) {
            cb(watch_event);
        }
    }

    // Xóa các sự kiện pending rename đã quá hạn mà không có sự kiện MOVED_TO tương ứng
    void cleanup_pending_renames() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = pending_renames.begin(); it != pending_renames.end();) {
            if (now - it->second.timestamp > PendingRenameTimeout) {
                notify_callbacks({InotifyEvent::MOVED_FROM, it->second.old_path});
                it = pending_renames.erase(it);
            } else {
                ++it;
            }
        }
    }

    void handle_event_create(inotify_event *event, const std::string &full_path) {
        if (event->mask & IN_ISDIR) {
            // Nếu là taoj thư mục, thêm watch đệ quy
            add_watch_recursive(inotify_fd, full_path.c_str());
            return;
        }
        std::cout << "Created: " << full_path << std::endl;
        notify_callbacks({InotifyEvent::CREATED, full_path});
    }

    void handle_event_moved_from(inotify_event *event, const std::string &full_path) {
        if (event->cookie == 0) {
            // not rename
            std::cout << "Moved from: " << full_path << std::endl;
            notify_callbacks({InotifyEvent::MOVED_FROM, full_path});
            return;
        }
        pending_renames[event->cookie] = {std::chrono::steady_clock::now(), full_path};
    }

    void handle_event_moved_to(inotify_event *event, const std::string &full_path) {
        if (event->cookie == 0) {
            // not rename
            std::cout << "Moved to: " << full_path << std::endl;
            notify_callbacks({InotifyEvent::MOVED_TO, full_path});
            return;
        }
        auto it = pending_renames.find(event->cookie);
        if (it != pending_renames.end()) {
            auto &rename_event = it->second;
            // Rename event
            std::cout << "Renamed from: " << rename_event.old_path << " to: " << full_path << std::endl;
            notify_callbacks({InotifyEvent::RENAME, rename_event.old_path, full_path});
            pending_renames.erase(it);
        } else {
            // related rename not found, just a move
            std::cout << "Moved to: " << full_path << std::endl;
            notify_callbacks({InotifyEvent::MOVED_TO, full_path});
        }
    }

    void start_watching() {
        set_interval([this]() { cleanup_pending_renames(); }, PendingRenameTimeout);

        while (true) {
            if (!watching) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::cout << "Đang tạm dừng theo dõi...\n";
                continue;
            }

            int length = read(inotify_fd, buffer, EVENT_BUF_LEN);
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
                if (event->mask & IN_IGNORED) {
                // Watch đã bị xóa (thường do thư mục bị xóa)
                // Dọn dẹp nó khỏi danh sách của chúng ta
                remove_watch_entry(event->wd);
                event_buf_offset += EVENT_SIZE + event->len;
                continue;
                }
                const char *base_path = get_path_from_wd(event->wd);
                if (event->len && base_path) {
                    const std::string full_path = std::string(base_path) + "/" + event->name;
                    {
                    std::lock_guard<std::mutex> lock(ignored_paths_mutex_);
                    //auto it = ignored_paths_once_.find(relative_path);
                    auto it = ignored_paths_once_.find(full_path);
                    if (it != ignored_paths_once_.end()) {
            // Tìm thấy, đây là sự kiện cần bỏ qua
                    //std::cout << "[Watcher] Ignoring event for: " << relative_path << std::endl;
                    ignored_paths_once_.erase(it); // Xóa đi để lần sau không bỏ qua nữa
                    event_buf_offset += EVENT_SIZE + event->len;
                    continue; // Chuyển sang sự kiện tiếp theo
                    }
                    }

                    if (event->mask & IN_CREATE) {
                        handle_event_create(event, full_path);
                    }
                    // if (event->mask & IN_MODIFY) {
                    //     notify_callbacks({InotifyEvent::MODIFIED, full_path});
                    // }
                    if (event->mask & IN_DELETE) {
                        notify_callbacks({InotifyEvent::DELETED, full_path});
                    }
                    if (event->mask & IN_MOVED_TO) {
                        handle_event_moved_to(event, full_path);
                    }
                    if (event->mask & IN_MOVED_FROM) {
                        handle_event_moved_from(event, full_path);
                    }

                    if (event->mask & IN_CLOSE_WRITE) {
                        notify_callbacks({InotifyEvent::CLOSED_WRITE, full_path});
                    }

                    if (event->mask & IN_Q_OVERFLOW) {
                        std::cerr << "Warning: inotify queue overflow. Events may have been lost.\n";
                        continue;
                    }

                } else {
                    std::cerr << "Không tìm thấy đường dẫn cho wd: " << event->wd << "\n";
                }
                event_buf_offset += EVENT_SIZE + event->len;
            }
        }
    }
};
