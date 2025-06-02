#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/inotify.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define WATCH_FLAGS ( IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_ISDIR )

// Cấu trúc liên kết watch descriptor (wd) với đường dẫn
typedef struct WatchNode {
    int wd;
    char *path;
    struct WatchNode *next;
} WatchNode;

// Lấy đường dẫn từ wd
const char *get_path_from_wd(int wd);

void free_watch_list(int fd);

// Thêm watch đệ quy vào thư mục và thư mục con
void add_watch_recursive(int fd, const char *root_path);

#ifdef __cplusplus
}
#endif