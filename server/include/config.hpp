#pragma once

#include <string>
#include <filesystem> // Still useful for server-side path manipulation

namespace fs = std::filesystem;

// const unsigned short SERVER_PORT = 12345; // Old TCP port
const unsigned short HTTP_SERVER_PORT = 8080; // Typical HTTP port for local dev
const std::string DATABASE_PATH = "db/file_server.db";
const std::string USER_DATA_ROOT = "data/users";
const std::string SHARED_DATA_ROOT = "data/shared";
const int PASSWORD_SALT_LENGTH = 16;
const int HASH_ITERATIONS = 10000;

// New: Base URL for constructing Location headers, etc.
const std::string SERVER_BASE_URL = "http://localhost:" + std::to_string(HTTP_SERVER_PORT);