# Core library CMakeLists.txt

# Исходники
set(CORE_SOURCES
    src/Database/Database.cpp
    src/Database/Migrations.cpp
    src/Index/IndexManager.cpp
    src/Index/FileScanner.cpp
    src/Search/SearchEngine.cpp
    src/Tags/TagManager.cpp
    src/Duplicates/DuplicateFinder.cpp
    src/Utils/Types.cpp
    src/Utils/MimeTypeDetector.cpp
    src/ffi/familyvault_c.cpp
)

# Shared library для FFI
add_library(familyvault SHARED ${CORE_SOURCES})

target_include_directories(familyvault
    PUBLIC include
    PRIVATE src
)

target_link_libraries(familyvault
    PRIVATE
        SQLite::SQLite3
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

# C++20 features
target_compile_features(familyvault PUBLIC cxx_std_20)

# Export макрос
target_compile_definitions(familyvault PRIVATE FAMILYVAULT_EXPORTS)

# Для Windows: экспорт символов
if(WIN32)
    set_target_properties(familyvault PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
endif()

# Visibility для non-Windows
if(NOT WIN32)
    set_target_properties(familyvault PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endif()

# Копировать .dll/.so рядом с Flutter app
if(WIN32)
    set(FLUTTER_WINDOWS_DIR "${CMAKE_SOURCE_DIR}/app/windows")
    
    # Copy familyvault.dll
    add_custom_command(TARGET familyvault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            "${FLUTTER_WINDOWS_DIR}/"
        COMMENT "Copying familyvault.dll to app/windows/"
    )
    
    # Copy runtime dependencies (vcpkg DLLs)
    # These are located in vcpkg_installed/x64-windows/bin/
    set(VCPKG_BIN_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
    
    foreach(DLL_NAME sqlite3.dll spdlog.dll fmt.dll libcrypto-3-x64.dll libssl-3-x64.dll)
        set(DLL_PATH "${VCPKG_BIN_DIR}/${DLL_NAME}")
        if(EXISTS "${DLL_PATH}")
            add_custom_command(TARGET familyvault POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL_PATH}"
                    "${FLUTTER_WINDOWS_DIR}/"
                COMMENT "Copying ${DLL_NAME} to app/windows/"
            )
        endif()
    endforeach()
else()
    add_custom_command(TARGET familyvault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            ${CMAKE_SOURCE_DIR}/app/linux/
        COMMENT "Copying libfamilyvault.so to app/linux/"
    )
endif()
