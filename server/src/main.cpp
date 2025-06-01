#include "db.hpp"
#include "config.hpp"
#include "user_manager.hpp"
#include "file_manager.hpp"
#include "sync_manager.hpp"
#include "access_control.hpp"
#include "server.hpp" // Our new server.hpp with HTTP components

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Util/ServerApplication.h> // Using this for robustness
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/HelpFormatter.h>
#include <iostream>
#include <csignal>

// --- Define a ServerApplication ---
class FileServerApp : public Poco::Util::ServerApplication {
public:
    FileServerApp() : _helpRequested(false) {}

protected:
    void initialize(Application& self) override {
        loadConfiguration(); // Load default E_APPLICATION config file, if any
        ServerApplication::initialize(self);
        logger().information("FileServerApp initializing...");

        // Initialize Database
        db_ = std::make_unique<Database>(DATABASE_PATH);
        if (!db_->get_db_handle()) {
            logger().fatal("Failed to initialize database. Exiting.");
            terminate(); // This will stop the application
            return;
        }
        if (!db_->initialize_schema()) {
            logger().fatal("Failed to initialize database schema. Exiting.");
            terminate();
            return;
        }
        logger().information("Database initialized successfully.");

        // Initialize Managers
        userManager_ = std::make_unique<UserManager>(*db_);
        fileManager_ = std::make_unique<FileManager>(*db_);
        syncManager_ = std::make_unique<SyncManager>(*db_, *fileManager_);
        //accessControlManager_ = std::make_unique<AccessControlManager>(*db_);
        access_controlManager_ = std::make_unique<AccessControlManager>(*db_, *userManager_);  
        logger().information("Managers initialized.");
    }

    void uninitialize() override {
        logger().information("FileServerApp uninitializing...");
        // Managers and DB will be cleaned up by unique_ptr
        ServerApplication::uninitialize();
    }

    void defineOptions(Poco::Util::OptionSet& options) override {
        ServerApplication::defineOptions(options);
        options.addOption(
            Poco::Util::Option("help", "h", "Display help information.")
                .required(false)
                .repeatable(false)
                .callback(Poco::Util::OptionCallback<FileServerApp>(this, &FileServerApp::handleHelp)));
    }

    void handleHelp(const std::string& name, const std::string& value) {
        _helpRequested = true;
        displayHelp();
        stopOptionsProcessing();
    }

    void displayHelp() {
        Poco::Util::HelpFormatter helpFormatter(options());
        helpFormatter.setCommand(commandName());
        helpFormatter.setUsage("OPTIONS");
        helpFormatter.setHeader("A C++ File Server Application.");
        helpFormatter.format(std::cout);
    }

    int main(const std::vector<std::string>& args) override {
        if (_helpRequested) {
            return Application::EXIT_OK;
        }
        if (!db_ || !db_->get_db_handle()) { // Check if db failed during initialize
            return Application::EXIT_CONFIG;
        }


        Poco::Net::ServerSocket svs(HTTP_SERVER_PORT);
        Poco::Net::HTTPServerParams* pParams = new Poco::Net::HTTPServerParams;
        pParams->setMaxQueued(100);
        pParams->setMaxThreads(16); // Adjust as needed for a local server

        httpServer_ = std::make_unique<Poco::Net::HTTPServer>(
                new FileServerRequestHandlerFactory(*db_, *userManager_, *fileManager_, *syncManager_, *access_controlManager_), // SỬA Ở ĐÂY
                svs,
                pParams
            );
            

        httpServer_->start();
        logger().information("HTTP Server started on port " + std::to_string(HTTP_SERVER_PORT));

        waitForTerminationRequest(); // Blocks until CTRL-C or kill signal

        logger().information("Stopping HTTP server...");
        httpServer_->stop();
        logger().information("HTTP Server stopped.");

        return Application::EXIT_OK;
    }

private:
    bool _helpRequested;
    std::unique_ptr<Poco::Net::HTTPServer> httpServer_;
    // Own the core components
    std::unique_ptr<Database> db_;
    std::unique_ptr<UserManager> userManager_;
    std::unique_ptr<FileManager> fileManager_;
    std::unique_ptr<SyncManager> syncManager_;
    std::unique_ptr<AccessControlManager> access_controlManager_;
};

// --- main function to run the ServerApplication ---
int main(int argc, char** argv) {
    FileServerApp app;
    return app.run(argc, argv);
}