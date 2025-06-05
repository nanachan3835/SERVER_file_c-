#include "sync.h"

const char *sync_strerror(SyncErrorCode code) {
    // not implemented
    return "Not implemented";
}

// notify error codes through errno
Server *init_sync_server(const char *serverUrl, const char *username, const char *password) {
    // not implemented
    return NULL;
}

void free_sync_server(Server *server) {
    // not implemented
}

SyncErrorCode sync_upload(Server *server, const char *pathFromWatcherRoot) {
    // not implemented
    return SYNC_ERROR_UNKNOWN;
}

SyncErrorCode sync_download(Server *server, const char *path) {
    // not implemented
    return SYNC_ERROR_UNKNOWN;
}

SyncErrorCode sync_delete(Server *server, const char *path) {
    // not implemented
    return SYNC_ERROR_UNKNOWN;
}

SyncErrorCode sync_rename(Server *server, const char *oldPath, const char *newPath) {
    // not implemented
    return SYNC_ERROR_UNKNOWN;
}

SyncErrorCode sync_get_files_list_json(Server *server, const char *path, char *responseBuffer,
                                       size_t bufferSize) {
    // not implemented
    return SYNC_ERROR_UNKNOWN;
}