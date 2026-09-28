cmake_minimum_required(VERSION 3.10)
project(P2PFileTransfer VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compile options for different platforms
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# OpenSSL is required by tls_context.cpp / peer_file_server.cpp
find_package(OpenSSL REQUIRED)

# Source files
set(SERVER_SOURCES
    src/server.cpp
    src/protocol.cpp
    src/file_manager.cpp
    src/tls_context.cpp
    src/peer_file_server.cpp
    src/main_server.cpp
)

# Create server executable
add_executable(p2p_server ${SERVER_SOURCES})

# Link libraries
target_link_libraries(p2p_server OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(p2p_server ws2_32)
else()
    target_link_libraries(p2p_server pthread)
endif()

# Installation
install(TARGETS p2p_server DESTINATION bin)

# Print build information
message(STATUS "=== P2P File Transfer Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
