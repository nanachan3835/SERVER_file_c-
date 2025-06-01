#include <gtest/gtest.h>
#include "user_manager.hpp"
#include "db.hpp"
#include "config.hpp" // For DATABASE_PATH
#include <filesystem>

namespace fs = std::filesystem;

// Test fixture for UserManager tests
class UserManagerTest : public ::testing::Test {
protected:
    Database* db;
    UserManager* userManager;
    std::string test_db_path = "test_file_server.db";
    std::string test_user_data_root = "test_data/users";


    void SetUp() override {
        // Ensure clean state for each test
        fs::remove(test_db_path); // Delete previous test DB if exists
        fs::remove_all(test_user_data_root); // Delete previous test user data
        fs::create_directories(test_user_data_root); // Recreate root for user dirs


        // Override config paths for testing
        // This is tricky because USER_DATA_ROOT is a global const in config.hpp
        // A better way would be to make these configurable at runtime or pass to constructors.
        // For this example, UserManager::create_user_directory uses USER_DATA_ROOT
        // We can't easily change that const per test.
        // So, the test will use the actual USER_DATA_ROOT from config.hpp
        // This means tests might interfere if USER_DATA_ROOT is not cleaned up.
        // Let's assume for this simple test, we use a dedicated test DB path.
        // And we ensure create_user_directory uses a test-specific path if possible,
        // or we test against the production USER_DATA_ROOT path and clean up.

        // For UserManager, the home_dir path is constructed using USER_DATA_ROOT.
        // We need to ensure USER_DATA_ROOT is either a test path or cleaned.
        // Simplest for now: let create_user_directory use the original USER_DATA_ROOT
        // and ensure it's cleaned after tests or tests are isolated.

        db = new Database(test_db_path);
        ASSERT_TRUE(db->get_db_handle() != nullptr) << "Database failed to open for test.";
        ASSERT_TRUE(db->initialize_schema()) << "Failed to initialize schema for test.";
        userManager = new UserManager(*db);
    }

    void TearDown() override {
        delete userManager;
        delete db; // This will close the database
        fs::remove(test_db_path); // Clean up test database file
        // Clean up user directories created during tests
        // Be careful with this path, ensure it's indeed for testing.
        // If USER_DATA_ROOT from config.hpp was used, clean that.
        fs::path user_root(USER_DATA_ROOT);
        if (fs::exists(user_root)) {
             for (const auto& entry : fs::directory_iterator(user_root)) {
                 if (fs::is_directory(entry.path())) {
                     // A simple heuristic: if user directory name starts with "testuser"
                     if (entry.path().filename().string().rfind("testuser", 0) == 0) {
                         fs::remove_all(entry.path());
                     }
                 }
             }
        }
    }
};

TEST_F(UserManagerTest, RegisterNewUser) {
    auto user_id = userManager->register_user("testuser1", "password123");
    ASSERT_TRUE(user_id.has_value());
    ASSERT_GT(*user_id, 0);

    // Verify user directory was created
    fs::path user_dir = fs::path(USER_DATA_ROOT) / "testuser1";
    ASSERT_TRUE(fs::exists(user_dir)) << "User directory " << user_dir << " was not created.";
    ASSERT_TRUE(fs::is_directory(user_dir));
}

TEST_F(UserManagerTest, RegisterExistingUserFails) {
    userManager->register_user("testuser2", "password123"); // First registration
    auto user_id = userManager->register_user("testuser2", "anotherpassword"); // Attempt to re-register
    ASSERT_FALSE(user_id.has_value());
}

TEST_F(UserManagerTest, LoginValidUser) {
    userManager->register_user("testuser3", "securepass");
    auto user_id = userManager->login_user("testuser3", "securepass");
    ASSERT_TRUE(user_id.has_value());
    ASSERT_GT(*user_id, 0);
}

TEST_F(UserManagerTest, LoginInvalidPassword) {
    userManager->register_user("testuser4", "securepass");
    auto user_id = userManager->login_user("testuser4", "wrongpassword");
    ASSERT_FALSE(user_id.has_value());
}

TEST_F(UserManagerTest, LoginNonExistentUser) {
    auto user_id = userManager->login_user("nosuchuser", "password");
    ASSERT_FALSE(user_id.has_value());
}

TEST_F(UserManagerTest, DeleteUser) {
    auto user_id_opt = userManager->register_user("testuser_to_delete", "password");
    ASSERT_TRUE(user_id_opt.has_value());
    int user_id = *user_id_opt;

    fs::path user_dir = fs::path(USER_DATA_ROOT) / "testuser_to_delete";
    ASSERT_TRUE(fs::exists(user_dir)) << "User directory for deletion test not created.";


    ASSERT_TRUE(userManager->delete_user(user_id));

    // Verify user is deleted from DB
    auto login_attempt = userManager->login_user("testuser_to_delete", "password");
    ASSERT_FALSE(login_attempt.has_value());

    // Verify user directory is deleted
    ASSERT_FALSE(fs::exists(user_dir)) << "User directory " << user_dir << " was not deleted.";
}

TEST_F(UserManagerTest, GetHomeDir) {
    auto user_id_opt = userManager->register_user("testuser_home", "password");
    ASSERT_TRUE(user_id_opt.has_value());
    
    auto home_dir_opt = userManager->get_user_home_dir(*user_id_opt);
    ASSERT_TRUE(home_dir_opt.has_value());

    fs::path expected_home_dir = fs::path(USER_DATA_ROOT) / "testuser_home";
    // Compare canonical paths to handle potential differences in string representation (e.g. trailing slashes)
    ASSERT_EQ(fs::weakly_canonical(fs::path(*home_dir_opt)), fs::weakly_canonical(expected_home_dir));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}