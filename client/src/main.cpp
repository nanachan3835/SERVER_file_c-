#include "config_reader.h"
#include "file_watcher_helper.hpp"
#include "sync_helper.hpp"
#include "utils.hpp"

#include <iostream>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic> // Cho std::atomic

namespace fs = std::filesystem;

std::atomic<bool> keep_running(true);

void signal_handler(int signum) {
    std::cout << "\nSignal " << signum << " received, shutting down..." << std::endl;
    keep_running.store(false);
}

std::string get_watcher_root_from_config() { // Đổi tên hàm
    Config *config = config_read("config.conf");
    if (!config) {
        throw std::runtime_error("Không thể đọc config.conf");
    }
    const char *watch_dir_c = config_get(config, "watcher_root");
    if (!watch_dir_c) {
        config_free(config);
        throw std::runtime_error("Không tìm thấy watcher_root trong file cấu hình.");
    }
    std::string watch_dir_str = watch_dir_c;
    config_free(config);

    fs::path watch_path(watch_dir_str);
    if (!watch_path.is_absolute()) {
         throw std::runtime_error("watcher_root phải là đường dẫn tuyệt đối: " + watch_dir_str);
    }
    if (!fs::exists(watch_path) || !fs::is_directory(watch_path)) {
        throw std::runtime_error("Thư mục watcher_root không tồn tại hoặc không phải là thư mục: " + watch_dir_str);
    }
    return watch_dir_str;
}


int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string watch_dir_str = get_watcher_root_from_config();
    std::unique_ptr<SyncHelper> sync_helper_ptr; // Dùng unique_ptr
    FileWatcherHelper watcher(watch_dir_str);

    try {
        watch_dir_str = get_watcher_root_from_config();
        sync_helper_ptr = std::make_unique<SyncHelper>(watcher, "config.conf"); // Truyền đường dẫn config
        
        // Đồng bộ lần đầu khi khởi động
        std::cout << "Thực hiện đồng bộ manifest lần đầu..." << std::endl;
        sync_helper_ptr->triggerManifestSync();

    } catch (const std::exception& e) {
        std::cerr << "Lỗi khởi tạo: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Đang theo dõi thư mục: " << watch_dir_str << std::endl;
    // watcher_ptr = std::make_unique<FileWatcherHelper>(watch_dir_str);

    TSQueue<FileWatcherHelper::WatchEvent> event_queue;

    watcher.add_callback([&](const FileWatcherHelper::WatchEvent& event) {
        event_queue.push(event);
    });

    std::thread event_processor_thread([&]() {
        auto last_sync_time = std::chrono::steady_clock::now();
        const auto sync_interval = std::chrono::seconds(10); // Đồng bộ manifest mỗi 10 giây nếu có thay đổi

        while (keep_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Kiểm tra hàng đợi thường xuyên hơn

            if (!event_queue.empty()) {
                std::cout << "[EventProcessor] Processing batched events..." << std::endl;
                FileWatcherHelper::WatchEvent current_event;
                // Xử lý một lô sự kiện nhỏ để tránh block quá lâu
                int processed_count = 0;
                while(processed_count < 10 && event_queue.pop(current_event)) { // Xử lý tối đa 10 sự kiện mỗi lần
                    processed_count++;
                    std::string full_local_path_str = (fs::path(watch_dir_str) / current_event.path_from_watcher_root).string();
                    std::string full_local_rename_to_str;
                    if (!current_event.rename_to.empty()){
                        full_local_rename_to_str = (fs::path(watch_dir_str) / current_event.rename_to).string();
                    }

                    try {
                        // Không gọi sync_helper trực tiếp ở đây nữa, chỉ đánh dấu cần đồng bộ manifest
                        // Logic xử lý sự kiện chi tiết sẽ nằm trong triggerManifestSync
                        std::cout << "Sự kiện cục bộ: " << static_cast<int>(current_event.inotify_event)
                                  << " cho " << current_event.path_from_watcher_root << std::endl;
                        // Đánh dấu cần đồng bộ manifest
                        last_sync_time = std::chrono::steady_clock::now() - sync_interval - std::chrono::seconds(1); // Buộc đồng bộ ở lần check tiếp theo
                    } catch (const std::exception& e) {
                        std::cerr << "Lỗi khi xử lý sự kiện cho '" << current_event.path_from_watcher_root << "': " << e.what() << std::endl;
                    }
                }
            }

            // Đồng bộ manifest định kỳ nếu có thay đổi hoặc theo lịch
            if (std::chrono::steady_clock::now() - last_sync_time > sync_interval) {
                try {
                    std::cout << "Kích hoạt đồng bộ manifest định kỳ..." << std::endl;
                    sync_helper_ptr->triggerManifestSync();
                    last_sync_time = std::chrono::steady_clock::now();
                } catch (const std::exception& e) {
                     std::cerr << "Lỗi trong quá trình đồng bộ manifest định kỳ: " << e.what() << std::endl;
                     // Có thể thử lại sau một khoảng thời gian dài hơn
                     last_sync_time = std::chrono::steady_clock::now(); // Reset để tránh lặp lỗi ngay
                }
            }
        }
        std::cout << "Event processor thread stopped." << std::endl;
    });

    std::cout << "Client đang chạy. Nhấn Ctrl+C để thoát." << std::endl;
    while (keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Đang dừng client..." << std::endl;
    if (event_processor_thread.joinable()) {
        event_processor_thread.join();
    }
    // watcher_ptr và sync_helper_ptr sẽ tự hủy

    std::cout << "Client đã dừng." << std::endl;
    return 0;
}