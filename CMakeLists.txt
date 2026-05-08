cmake_minimum_required(VERSION 3.13)

# Updated project name to your Sanskrit-inspired brand
project(DrutaDB)

# Modern C++23 standard as supported by your g++ 13.3 version
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Threading support for your poll-based concurrency
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Explicitly listing files fixes the "No SOURCES" error
# and makes the build process more robust 
add_executable(drutadb 
    src/main.cpp 
    src/store.cpp
    src/commands.cpp
    src/parser_resp.cpp
    src/aof.cpp
    src/lru.cpp
)

# Linking pthreads for Linux/WSL networking
target_link_libraries(drutadb PRIVATE Threads::Threads)