#pragma once

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *placeholder;
} Server;

typedef enum {
    SYNC_SUCCESS = 0,
    SYNC_ERROR_INVALID_URL = 1001,
    SYNC_ERROR_CONNECTION_FAILED,
    SYNC_ERROR_AUTH_FAILED,
    SYNC_ERROR_TIMEOUT,
    SYNC_ERROR_FILE_NOT_FOUND,
    SYNC_ERROR_UNKNOWN
} SyncErrorCode;

const char* sync_strerror(SyncErrorCode code);

// notify error codes through errno
Server* init_sync_server(const char *serverUrl, const char* username, const char* password);

void free_sync_server(Server *server);

SyncErrorCode sync_upload(Server *server, const char *pathFromWatcherRoot);

SyncErrorCode sync_download(Server *server, const char *path);

SyncErrorCode sync_delete(Server *server, const char *path);

SyncErrorCode sync_rename(Server *server, const char *oldPath, const char *newPath);

SyncErrorCode sync_get_files_list_json(Server *server, const char *path, char* responseBuffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif