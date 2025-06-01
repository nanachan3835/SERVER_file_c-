#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
#define WATCH_FLAGS ( IN_CREATE | IN_MODIFY | IN_DELETE | IN_ISDIR )

// Cấu trúc liên kết watch descriptor (wd) với đường dẫn
typedef struct WatchNode {
    int wd;
    char *path;
    struct WatchNode *next;
} WatchNode;

// Thêm watch vào danh sách quản lý
void add_watch_entry(int wd, const char *path);

// Lấy đường dẫn từ wd
const char *get_path_from_wd(int wd);

// Giải phóng danh sách watch
void free_watch_list();

// Thêm watch đệ quy vào thư mục và thư mục con
void add_watch_recursive(int fd, const char *root_path);

#ifdef __cplusplus
}
#endif