cmake_minimum_required(VERSION 3.10)
project(SyncClient C CXX)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(includes)

# Copy configuration file
configure_file(${CMAKE_SOURCE_DIR}/config.conf ${CMAKE_BINARY_DIR}/config.conf COPYONLY)

# Source files
file(GLOB SOURCES "src/*.c" "src/*.cpp")

add_executable(SyncClient ${SOURCES})