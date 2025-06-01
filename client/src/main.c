#include "config_reader.h"
#include "file_watcher.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))

const char *get_watch_dir() {
    Config *config = config_read("config.conf");
    const char *watch_dir = config_get(config, "watcher_root");
    if (!watch_dir) {
        fprintf(stderr, "Không tìm thấy watcher_root trong file cấu hình.\n");
        exit(1);
    }
    if (watch_dir[0] != '/') {
        fprintf(stderr, "watcher_root phải là đường dẫn tuyệt đối.\n");
        exit(1);
    }
    if (access(watch_dir, F_OK) != 0) {
        fprintf(stderr, "Thư mục %s không tồn tại.\n", watch_dir);
        exit(1);
    }
    if (access(watch_dir, R_OK | W_OK) != 0) {
        fprintf(stderr, "Không có quyền đọc/ghi thư mục %s.\n", watch_dir);
        exit(1);
    }
    config_free(config);
    return watch_dir;
}

int main() {

    int fd = inotify_init(); // Tạo file descriptor
    if (fd < 0) {
        perror("inotify_init");
        exit(1);
    }

    const char *watch_dir = get_watch_dir();

    printf("Đang theo dõi thư mục: %s\n", watch_dir);

    // Giám sát watch_dir với các sự kiện thay đổi nội dung, đóng sau ghi
    add_watch_recursive(fd, watch_dir);

    // perror("inotify_add_watch");
    // close(fd);
    // exit(1);

    char buffer[BUF_LEN];

    while (1) {
        int length = read(fd, buffer, BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            const char *base_path = get_path_from_wd(event->wd);
            if (event->len && base_path) {
                char full_path[PATH_MAX];
                snprintf(full_path, PATH_MAX, "%s/%s", base_path, event->name);
                if (event->mask & IN_CREATE) {
                    printf("Created: %s\n", full_path);
                    if (event->mask & IN_ISDIR) {
                        // Nếu là taoj thư mục, thêm watch đệ quy
                        add_watch_recursive(fd, full_path);
                    }
                }
                if (event->mask & IN_MODIFY) {
                    printf("Modify: %s\n", full_path);
                }

                if (event->mask & IN_CLOSE_WRITE) {
                    printf("IN_CLOSE_WRITE (đóng sau khi ghi): %s\n", full_path);
                }
            }
            // else {
            //     if (event->mask & IN_CREATE) {
            //         printf("Created: %s\n", full_path);
            //         if (event->mask & IN_ISDIR) {
            //             // Nếu là taoj thư mục, thêm watch đệ quy
            //             add_watch_recursive(inotify_fd, full_path);
            //         }
            //     }
            //     if (event->mask & IN_MODIFY) {
            //         printf("File %s đã bị sửa.\n", event->name);
            //     }

            //     if (event->mask & IN_CLOSE_WRITE) {
            //         if (event->len > 0)
            //             printf("File %s đã được đóng sau khi ghi.\n", event->name);
            //         else
            //             printf("File đã được đóng sau khi ghi.\n");
            //     }
            // }
            i += EVENT_SIZE + event->len;
        }
    }

    // Dọn dẹp
    free_watch_list(fd);
    close(fd);
    return 0;
}
