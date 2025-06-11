#include "file_watcher.h"
#define _DEFAULT_SOURCE
#define _GNU_SOURCE 
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/inotify.h>
#include <linux/limits.h>
#include <unistd.h>
static WatchNode *watch_list = NULL;
#define DT_DIR 4 



// Thêm watch vào danh sách và ghi nhớ wd và đường dẫn
static void add_watch_entry(int wd, const char *path) {
    WatchNode *node = (WatchNode *)malloc(sizeof(WatchNode));
    node->wd = wd;
    node->path = strdup(path);
    node->next = watch_list;
    watch_list = node;
}

const char *get_path_from_wd(int wd) {
    WatchNode *curr = watch_list;
    while (curr) {
        if (curr->wd == wd)
            return curr->path;
        curr = curr->next;
    }
    return NULL;
}

// Giải phóng danh sách watch
void free_watch_list(int inotify_fd) {
    WatchNode *curr = watch_list;
    while (curr) {
        inotify_rm_watch(inotify_fd, curr->wd);
        WatchNode *tmp = curr;
        curr = curr->next;
        free(tmp->path);
        free(tmp);
    }
    watch_list = NULL;
}

// Thêm watch đệ quy vào thư mục và thư mục con
void add_watch_recursive(int fd, const char *root_path) {
    DIR *dir = opendir(root_path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    char path[PATH_MAX];

    int wd = inotify_add_watch(fd, root_path, WATCH_FLAGS);
    if (wd == -1) {
        perror("inotify_add_watch error");
    } else {
        printf("Watching: %s (wd = %d)\n", root_path, wd);
        add_watch_entry(wd, root_path);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, PATH_MAX, "%s/%s", root_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            add_watch_recursive(fd, path);
        }
    }

    closedir(dir);
}