#include "db.hpp"
#include "config.hpp"
#include "user_manager.hpp"
#include "file_manager.hpp"
#include "sync_manager.hpp"
#include "access_control.hpp"
#include "server.hpp"

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

class FileServerApp : public Poco::Util::ServerApplication {
public:
    FileServerApp() : _helpRequested(false) {}

protected:
    void initialize(Application& self) override {
        loadConfiguration();
        ServerApplication::initialize(self);
        logger().information("FileServerApp initializing...");

        db_ = std::make_unique<Database>(DATABASE_PATH);
        if (!db_->get_db_handle()) { logger().fatal("DB init failed."); terminate(); return; }
        if (!db_->initialize_schema()) { logger().fatal("DB schema init failed."); terminate(); return; }
        logger().information("Database initialized.");

        userManager_ = std::make_unique<UserManager>(*db_);
        fileManager_ = std::make_unique<FileManager>(*db_);
        syncManager_ = std::make_unique<SyncManager>(*db_, *fileManager_);
        // SỬA LỖI: Tên biến và truyền đủ tham số
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

        Poco::Net::ServerSocket svs(HTTP_SERVER_PORT);
        Poco::Net::HTTPServerParams::Ptr pParams = new Poco::Net::HTTPServerParams;
        pParams->setMaxQueued(100); pParams->setMaxThreads(16);

        httpServer_ = std::make_unique<Poco::Net::HTTPServer>(
            new FileServerRequestHandlerFactory(*db_, *userManager_, *fileManager_, *syncManager_, *access_controlManager_), // SỬA LỖI: Tên biến
            svs, pParams
        );

        httpServer_->start();
        logger().information("HTTP Server started on port " + std::to_string(HTTP_SERVER_PORT));
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
    std::unique_ptr<AccessControlManager> access_controlManager_; // Tên biến đúng
};

int main(int argc, char** argv) {
    FileServerApp app;
    return app.run(argc, argv);
}