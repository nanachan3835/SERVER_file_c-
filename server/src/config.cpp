#include "config.hpp"
#include <Poco/Util/PropertyFileConfiguration.h>
#include <Poco/AutoPtr.h>
#include <Poco/Logger.h>
#include <iostream>

using namespace Poco::Util;
using Poco::AutoPtr;

// Định nghĩa biến static
unsigned short Config::HTTP_SERVER_PORT = 8080;
std::string Config::SERVER_BASE_URL = "";
std::string Config::DATABASE_PATH = "db/file_server.db";
std::string Config::USER_DATA_ROOT = "data/users";
std::string Config::SHARED_DATA_ROOT = "data/shared";
int Config::PASSWORD_SALT_LENGTH = 16;
int Config::HASH_ITERATIONS = 10000;

void loadConfigFromFile(const std::string& filePath) {
    try {
        AutoPtr<PropertyFileConfiguration> config = new PropertyFileConfiguration(filePath);

        Config::HTTP_SERVER_PORT = static_cast<unsigned short>(config->getUInt("server.port", 8080));
        Config::DATABASE_PATH = config->getString("database.path", "db/file_server.db");
        Config::USER_DATA_ROOT = config->getString("storage.users_root", "data/users");
        Config::SHARED_DATA_ROOT = config->getString("storage.shared_root", "data/shared");
        Config::PASSWORD_SALT_LENGTH = config->getInt("security.salt_length", 16);
        Config::HASH_ITERATIONS = config->getInt("security.hash_iterations", 10000);

        Config::SERVER_BASE_URL = "http://localhost:" + std::to_string(Config::HTTP_SERVER_PORT);

        Poco::Logger::get("Config").information("Configuration loaded successfully.");
        Poco::Logger::get("Config").information("Port: " + std::to_string(Config::HTTP_SERVER_PORT));
    }
    catch (const Poco::Exception& ex) {
        std::cerr << "Error loading config file: " << ex.displayText() << std::endl;
        throw;
    }
}
