#include "config_reader.h"

#include <errno.h>
#include <stdio.h>

Config *config_create(void) {
    Config *config = (Config*) malloc(sizeof(Config));
    if (!config) return NULL;
    config->count = 0;
    config->capacity = 10;
    config->entries = (ConfigEntry*) malloc(config->capacity * sizeof(ConfigEntry));
    if (!config->entries) {
        free(config);
        return NULL;
    }
    return config;
}

int config_add(Config *config, const char *key, const char *value) {
    if (config->count >= config->capacity) {
        config->capacity *= 2;
        ConfigEntry *new_entries = (ConfigEntry*) realloc(config->entries, config->capacity * sizeof(ConfigEntry));
        if (!new_entries) return 0;
        config->entries = new_entries;
    }
    strncpy(config->entries[config->count].key, key, MAX_KEY_LENGTH - 1);
    config->entries[config->count].key[MAX_KEY_LENGTH - 1] = '\0';
    strncpy(config->entries[config->count].value, value, MAX_VALUE_LENGTH - 1);
    config->entries[config->count].value[MAX_VALUE_LENGTH - 1] = '\0';
    config->count++;
    return 1;
}

// Free config structure
void config_free(Config *config) {
    if (config) {
        free(config->entries);
        free(config);
    }
}

Config *config_read(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Không thể mở file cấu hình");
        printf("Lỗi: %s\n", strerror(errno));
        return NULL;
    }

    Config *config = config_create();
    if (!config) {
        fclose(file);
        return NULL;
    }

    char line[MAX_LINE_LENGTH];
    while (fgets(line, MAX_LINE_LENGTH, file)) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Find the '=' separator
        char *delimiter = strchr(line, '=');
        if (!delimiter) continue; // Skip malformed lines

        // Split into key and value
        *delimiter = '\0';
        char *key = line;
        char *value = delimiter + 1;

        // Trim leading/trailing whitespace (basic)
        while (*key == ' ') key++;
        while (*value == ' ') value++;
        char *end = key + strlen(key) - 1;
        while (end > key && *end == ' ') *end-- = '\0';
        end = value + strlen(value) - 1;
        while (end > value && *end == ' ') *end-- = '\0';

        // Add to config
        if (!config_add(config, key, value)) {
            config_free(config);
            fclose(file);
            return NULL;
        }
    }

    fclose(file);
    return config;
}

const char *config_get(const Config *config, const char *key) {
    for (size_t i = 0; i < config->count; i++) {
        if (strcmp(config->entries[i].key, key) == 0) {
            return config->entries[i].value;
        }
    }
    return NULL;
}