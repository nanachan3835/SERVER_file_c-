#include "db.hpp"
#include "config.hpp" // Bao gồm file config mới
#include "user_manager.hpp"
#include "file_manager.hpp"
#include "sync_manager.hpp"
#include "access_control.hpp"
#include "server.hpp"
#include <filesystem>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/HelpFormatter.h>
#include <iostream>
#include <csignal>
#include <memory>

namespace fs = std::filesystem;

// Khởi tạo các biến static trong file .cpp để tránh lỗi "multiple definition"
// unsigned short Config::HTTP_SERVER_PORT;
// std::string Config::SERVER_BASE_URL;
// std::string Config::DATABASE_PATH;
// std::string Config::USER_DATA_ROOT;
// std::string Config::SHARED_DATA_ROOT;
// int Config::PASSWORD_SALT_LENGTH;
// int Config::HASH_ITERATIONS;


class FileServerApp : public Poco::Util::ServerApplication {
public:
    FileServerApp() : _helpRequested(false) {}

protected:
    void initialize(Application& self) override {
        // Load cấu hình từ file `config.properties` (tên file được suy ra từ tên executable)
        // hoặc file được chỉ định qua command line --config-file=myconfig.properties
        loadConfiguration(); 
        ServerApplication::initialize(self);
        logger().information("FileServerApp initializing...");
        loadConfigFromFile("../config.properties"); // Load cấu hình từ file config
        // *** BẮT ĐẦU LOAD CẤU HÌNH VÀO STRUCT CONFIG ***
        // Config::HTTP_SERVER_PORT = (unsigned short)config().getUInt("server.port", 8080);
        // Config::DATABASE_PATH = config().getString("database.path", "db/file_server.db");
        // Config::USER_DATA_ROOT = config().getString("storage.users_root", "data/users");
        // Config::SHARED_DATA_ROOT = config().getString("storage.shared_root", "data/shared");
        // Config::PASSWORD_SALT_LENGTH = config().getInt("security.salt_length", 16);
        // Config::HASH_ITERATIONS = config().getInt("security.hash_iterations", 10000);
        // Config::SERVER_BASE_URL = "http://localhost:" + std::to_string(Config::HTTP_SERVER_PORT);
        logger().information("Configuration loaded. Port: " + std::to_string(Config::HTTP_SERVER_PORT));
        // *** KẾT THÚC LOAD CẤU HÌNH ***

        // Sử dụng các biến từ Config struct
        db_ = std::make_unique<Database>(Config::DATABASE_PATH);
        if (!db_->get_db_handle()) { logger().fatal("DB init failed."); terminate(); return; }
        if (!db_->initialize_schema()) { logger().fatal("DB schema init failed."); terminate(); return; }
        logger().information("Database initialized.");

        userManager_ = std::make_unique<UserManager>(*db_);
        fileManager_ = std::make_unique<FileManager>(*db_);
        syncManager_ = std::make_unique<SyncManager>(*db_, *fileManager_);
        access_controlManager_ = std::make_unique<AccessControlManager>(*db_, *userManager_);

        logger().information("Managers initialized.");
    }

    void uninitialize() override {
        logger().information("FileServerApp uninitializing...");
        ServerApplication::uninitialize();
    }

    void defineOptions(Poco::Util::OptionSet& options) override {
        ServerApplication::defineOptions(options);
        options.addOption(
            Poco::Util::Option("help", "h", "Display help.")
                .required(false).repeatable(false)
                .callback(Poco::Util::OptionCallback<FileServerApp>(this, &FileServerApp::handleHelp)));
    }

    void handleHelp(const std::string&, const std::string&) {
        _helpRequested = true; displayHelp(); stopOptionsProcessing();
    }

    void displayHelp() {
        Poco::Util::HelpFormatter hf(options()); hf.setCommand(commandName());
        hf.setUsage("OPTIONS"); hf.setHeader("File Server App."); hf.format(std::cout);
    }

    int main(const std::vector<std::string>& args) override {
        if (_helpRequested) return Application::EXIT_OK;
        if (!db_ || !userManager_ || !fileManager_ || !syncManager_ || !access_controlManager_) {
            logger().fatal("Core components not initialized."); return Application::EXIT_CONFIG;
        }

        Poco::Net::ServerSocket svs(Config::HTTP_SERVER_PORT); // Sử dụng config
        Poco::Net::HTTPServerParams::Ptr pParams = new Poco::Net::HTTPServerParams;
        pParams->setMaxQueued(100); pParams->setMaxThreads(16);

        httpServer_ = std::make_unique<Poco::Net::HTTPServer>(
            new FileServerRequestHandlerFactory(*db_, *userManager_, *fileManager_, *syncManager_, *access_controlManager_),
            svs, pParams
        );

        httpServer_->start();
        logger().information("HTTP Server started on port " + std::to_string(Config::HTTP_SERVER_PORT));
        waitForTerminationRequest();
        logger().information("Stopping HTTP server...");
        httpServer_->stop();
        logger().information("HTTP Server stopped.");
        return Application::EXIT_OK;
    }

private:
    bool _helpRequested;
    std::unique_ptr<Poco::Net::HTTPServer> httpServer_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<UserManager> userManager_;
    std::unique_ptr<FileManager> fileManager_;
    std::unique_ptr<SyncManager> syncManager_;
    std::unique_ptr<AccessControlManager> access_controlManager_;
};

int main(int argc, char** argv) {
    try {
        std::cout << "Server Current Working Directory: " << fs::current_path().string() << std::endl;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error getting current working directory: " << e.what() << std::endl;
    }
    FileServerApp app;
    return app.run(argc, argv);
}