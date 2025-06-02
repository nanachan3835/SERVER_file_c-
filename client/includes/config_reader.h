#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LINE_LENGTH 256
#define MAX_KEY_LENGTH 128
#define MAX_VALUE_LENGTH 128

typedef struct {
    char key[MAX_KEY_LENGTH];
    char value[MAX_VALUE_LENGTH];
} ConfigEntry;

typedef struct {
    ConfigEntry *entries;
    size_t count;
    size_t capacity;
} Config;

Config *config_create(void);
Config *config_read(const char *filename);
int config_add(Config *config, const char *key, const char *value);
const char *config_get(const Config *config, const char *key);
void config_free(Config *config);

#ifdef __cplusplus
}
#endif