cmake_minimum_required(VERSION 3.21)
project(FamilyVault VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_TEXT_EXTRACTION "Enable text extraction from documents (PDF, DOCX, etc.)" ON)

# Зависимости
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(fmt CONFIG REQUIRED)

# spdlog: compiled library on desktop, header-only on Android (glibc issues)
if(NOT ANDROID)
    find_package(spdlog CONFIG REQUIRED)
endif()

add_subdirectory(core)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests/core)
endif()

