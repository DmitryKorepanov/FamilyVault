# Core library CMakeLists.txt

# Базовые исходники (всегда включены)
set(CORE_SOURCES
    src/Database/Database.cpp
    src/Database/Migrations.cpp
    src/Cloud/CloudAccountManager.cpp
    src/Index/IndexManager.cpp
    src/Index/FileScanner.cpp
    src/Index/ContentIndexer.cpp
    src/Search/SearchEngine.cpp
    src/Tags/TagManager.cpp
    src/Duplicates/DuplicateFinder.cpp
    src/Security/SecureStorage.cpp
    src/Security/FamilyPairing.cpp
    src/Network/Discovery.cpp
    src/Network/TlsPsk.cpp
    src/Network/NetworkProtocol.cpp
    src/Network/PeerConnection.cpp
    src/Network/NetworkManager.cpp
    src/Network/IndexSyncManager.cpp
    src/Network/RemoteFileAccess.cpp
    src/Network/PairingServer.cpp
    src/Utils/Types.cpp
    src/Utils/MimeTypeDetector.cpp
    src/ffi/familyvault_c.cpp
    src/ffi/ffi_cloud.cpp
    src/ffi/ffi_secure.cpp
    src/ffi/ffi_network.cpp
    src/ffi/ffi_network_manager.cpp
)

# Text extraction (опционально - требует poppler, libzip, pugixml)
if(ENABLE_TEXT_EXTRACTION)
    list(APPEND CORE_SOURCES
        src/TextExtractor/TextExtractorRegistry.cpp
        src/TextExtractor/PlainTextExtractor.cpp
        src/TextExtractor/PdfTextExtractor.cpp
        src/TextExtractor/OfficeTextExtractor.cpp
    )
endif()

# Shared library для FFI
add_library(familyvault SHARED ${CORE_SOURCES})

target_include_directories(familyvault
    PUBLIC include
    PRIVATE src
)

# Base dependencies
target_link_libraries(familyvault
    PRIVATE
        SQLite::SQLite3
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
)

# Platform-specific libraries for Security and Network modules
if(WIN32)
    target_link_libraries(familyvault PRIVATE advapi32 rpcrt4 ws2_32 iphlpapi)
elseif(NOT ANDROID)
    # Linux: uuid library
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(UUID IMPORTED_TARGET uuid)
        if(UUID_FOUND)
            target_link_libraries(familyvault PRIVATE PkgConfig::UUID)
        endif()
    endif()
endif()

# spdlog: header-only on Android (compiled lib uses glibc-specific functions)
if(ANDROID)
    find_package(fmt CONFIG REQUIRED)
    target_link_libraries(familyvault PRIVATE fmt::fmt)
    target_include_directories(familyvault PRIVATE ${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include)
    target_compile_definitions(familyvault PRIVATE SPDLOG_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
else()
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(familyvault PRIVATE spdlog::spdlog)
endif()

# Text extraction dependencies (опционально)
if(ENABLE_TEXT_EXTRACTION)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)
    find_package(libzip CONFIG REQUIRED)
    find_package(pugixml CONFIG REQUIRED)
    
    target_link_libraries(familyvault
        PRIVATE
            PkgConfig::POPPLER_CPP
            libzip::zip
            pugixml::pugixml
    )
    
    target_compile_definitions(familyvault PRIVATE ENABLE_TEXT_EXTRACTION=1)
else()
    target_compile_definitions(familyvault PRIVATE ENABLE_TEXT_EXTRACTION=0)
endif()

# C++20 features
target_compile_features(familyvault PUBLIC cxx_std_20)

# Export макрос
target_compile_definitions(familyvault PRIVATE FAMILYVAULT_EXPORTS)

# Для Windows: экспорт символов (ON для тестов)
if(WIN32)
    set_target_properties(familyvault PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
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
    
    # Core dependencies + TextExtractor dependencies (poppler, libzip, pugixml)
    set(REQUIRED_DLLS
        sqlite3.dll spdlog.dll fmt.dll libcrypto-3-x64.dll libssl-3-x64.dll
        poppler.dll poppler-cpp.dll pugixml.dll zip.dll
        freetype.dll libpng16.dll bz2.dll zlib1.dll
        brotlicommon.dll brotlidec.dll tiff.dll openjp2.dll
        jpeg62.dll liblzma.dll iconv-2.dll charset-1.dll
    )
    
    foreach(DLL_NAME ${REQUIRED_DLLS})
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
    
    # Copy MSVCP140_ATOMIC_WAIT.dll (needed for std::mutex in C++20)
    if(EXISTS "C:/Windows/System32/MSVCP140_ATOMIC_WAIT.dll")
        add_custom_command(TARGET familyvault POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/Windows/System32/MSVCP140_ATOMIC_WAIT.dll"
                "${FLUTTER_WINDOWS_DIR}/"
            COMMENT "Copying MSVCP140_ATOMIC_WAIT.dll to app/windows/"
        )
    endif()
elseif(ANDROID)
    # Android: copy to jniLibs
    set(ANDROID_JNILIBS_DIR "${CMAKE_SOURCE_DIR}/app/android/app/src/main/jniLibs/${ANDROID_ABI}")
    file(MAKE_DIRECTORY "${ANDROID_JNILIBS_DIR}")
    add_custom_command(TARGET familyvault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            "${ANDROID_JNILIBS_DIR}/"
        COMMENT "Copying libfamilyvault.so to ${ANDROID_JNILIBS_DIR}/"
    )
else()
    # Linux
    add_custom_command(TARGET familyvault POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            ${CMAKE_SOURCE_DIR}/app/linux/
        COMMENT "Copying libfamilyvault.so to app/linux/"
    )
endif()
