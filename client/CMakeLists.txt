# 

cmake_minimum_required(VERSION 3.18) # Nên dùng version mới hơn cho vcpkg manifest
project(SyncClient LANGUAGES CXX C) # Giữ lại C nếu file_watcher.c và config_reader.c vẫn dùng

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11) # Nếu có file C
set(CMAKE_C_STANDARD_REQUIRED ON)

# Vcpkg Integration - QUAN TRỌNG: Đảm bảo bạn truyền -DCMAKE_TOOLCHAIN_FILE khi chạy cmake
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain")
    message(STATUS "Using VCPKG_ROOT for toolchain: ${CMAKE_TOOLCHAIN_FILE}")
elseif(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain from submodule")
    message(STATUS "Using vcpkg submodule for toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Find Packages
find_package(Poco REQUIRED COMPONENTS Net Foundation Util JSON) # Thêm JSON nếu dùng Poco::JSON
#find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED) # Nếu client tính hash

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/includes # Nếu bạn có thư mục includes riêng cho .h/.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src     # Cho phép .hpp đặt cùng .cpp trong src
)

# Copy configuration file#include "config_reader.h"
#include "file_watcher_helper.hpp"
#include "sync_helper.hpp" // Đã bao gồm http_client.hpp
#include "utils.hpp"       // Cho trim

#include <iostream>
#include <string>
#include <stdexcept> // Cho std::runtime_error
#include <filesystem> // Cho fs::path
#include <thread>     // Cho std::this_thread::sleep_for
#include <chrono>     // Cho std::chrono::seconds
#include <unistd.h>   // Cho access (nếu vẫn dùng)
#include <csignal>    // Cho signal handling



configure_file(${CMAKE_SOURCE_DIR}/config.conf ${CMAKE_BINARY_DIR}/config.conf COPYONLY)
#configure_file(${CMAKE_SOURCE_DIR}/app_data.json ${CMAKE_BINARY_DIR}/app_data.json COPYONLY) # Copy file data mẫu nếu có

# Source files
# Liệt kê rõ ràng hoặc dùng GLOB cẩn thận
set(CLIENT_SOURCES
    src/main.cpp
    src/config_reader.c       # File C
    src/file_watcher.c        # File C
    #src/file_watcher.c # Đổi tên từ .hpp nếu nó chứa định nghĩa hàm
    src/sync_helper.cpp       # Đổi tên từ .hpp nếu nó chứa định nghĩa hàm
    src/http_client.cpp       # File mới
    src/local_file_system.cpp # File mới (nếu tách ra)
    src/auth_manager.cpp      # File mới
    # Thêm các file .cpp khác nếu có
)
# Nếu file_watcher_helper.hpp và sync_helper.hpp chỉ là header, không cần thêm vào SOURCES

add_executable(SyncClient ${CLIENT_SOURCES})

# Link libraries
target_link_libraries(SyncClient PRIVATE
    Poco::Net
    Poco::Foundation
    Poco::Util
    Poco::JSON # Nếu dùng
    #nlohmann_json::nlohmann_json
    OpenSSL::SSL   # Link OpenSSL
    OpenSSL::Crypto
    # Thêm pthread nếu cần cho std::thread trên một số hệ thống Linux cũ (thường không cần với g++ hiện đại)
    # Threads::Threads # Cách chuẩn của CMake để link thư viện thread
)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(SyncClient PRIVATE Threads::Threads) # Đảm bảo link với pthread
endif()


# Output paths
set_target_properties(SyncClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR} # Để config.conf và app_data.json cùng cấp với executable
)

message(STATUS "CMAKE_TOOLCHAIN_FILE is: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "Poco_FOUND: ${Poco_FOUND}")
#message(STATUS "nlohmann_json_FOUND: ${nlohmann_json_FOUND}")
message(STATUS "OpenSSL_FOUND: ${OpenSSL_FOUND}")