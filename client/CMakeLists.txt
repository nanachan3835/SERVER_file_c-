cmake_minimum_required(VERSION 3.10)
project(ConfigReader C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(includes)

configure_file(${CMAKE_SOURCE_DIR}/config.conf ${CMAKE_BINARY_DIR}/config.conf COPYONLY)

# Source files
file(GLOB SOURCES "src/*.c")

# Create executable
add_executable(main ${SOURCES})