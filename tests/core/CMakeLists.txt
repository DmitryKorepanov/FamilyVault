# Tests CMakeLists.txt

set(TEST_SOURCES
    test_main.cpp
    test_version.cpp
    test_database.cpp
    test_mime_type.cpp
    test_index_manager.cpp
    test_search_engine.cpp
    test_tags.cpp
    test_content_indexer.cpp
    test_file_scanner.cpp
    test_security.cpp
    test_network.cpp
    test_tls_psk.cpp
    test_index_sync.cpp
    test_remote_file_access.cpp
    test_network_manager.cpp
    test_pairing_protocol.cpp
    test_file_transfer.cpp
    test_p2p_integration.cpp
    # Wiring/Integration tests (catch "code exists but not called" bugs)
    test_ffi_initialization.cpp
    test_json_contracts.cpp
    test_state_machine.cpp
    test_loopback_integration.cpp
)

# Text extractor tests только когда ENABLE_TEXT_EXTRACTION=ON
if(ENABLE_TEXT_EXTRACTION)
    list(APPEND TEST_SOURCES test_text_extractor.cpp)
endif()

add_executable(familyvault_tests ${TEST_SOURCES})

target_include_directories(familyvault_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(familyvault_tests
    PRIVATE
        familyvault
        GTest::gtest
        GTest::gtest_main
)

# C++20
target_compile_features(familyvault_tests PUBLIC cxx_std_20)

# Discover tests
# Note: gtest_discover_tests requires running the test executable at build time.
# We use a simpler approach: just add the test executable as a single test.
# Individual tests can be filtered at runtime with --gtest_filter
enable_testing()
add_test(NAME familyvault_tests 
         COMMAND familyvault_tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Copy test fixtures
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)

# Copy DLLs to tests directory after build
if(WIN32)
    add_custom_command(TARGET familyvault_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            $<TARGET_FILE_DIR:familyvault_tests>/
        COMMENT "Copying familyvault.dll to tests directory"
    )
    
    # Copy vcpkg DLLs
    set(VCPKG_BIN_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
    foreach(DLL_NAME fmt sqlite3 spdlog libcrypto-3-x64 libssl-3-x64)
        if(EXISTS "${VCPKG_BIN_DIR}/${DLL_NAME}.dll")
            add_custom_command(TARGET familyvault_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_BIN_DIR}/${DLL_NAME}.dll"
                    $<TARGET_FILE_DIR:familyvault_tests>/
                COMMENT "Copying ${DLL_NAME}.dll to tests directory"
            )
        endif()
    endforeach()
    
    # Copy MSVCP140_ATOMIC_WAIT.dll (needed for std::mutex in C++20)
    if(EXISTS "C:/Windows/System32/MSVCP140_ATOMIC_WAIT.dll")
        add_custom_command(TARGET familyvault_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "C:/Windows/System32/MSVCP140_ATOMIC_WAIT.dll"
                $<TARGET_FILE_DIR:familyvault_tests>/
            COMMENT "Copying MSVCP140_ATOMIC_WAIT.dll to tests directory"
        )
    endif()
    
    # Copy text extraction DLLs when enabled
    if(ENABLE_TEXT_EXTRACTION)
        set(TEXT_EXTRACTION_DLLS
            poppler poppler-cpp pugixml zip freetype libpng16
            bz2 zlib1 brotlicommon brotlidec tiff openjp2 
            jpeg62 liblzma iconv-2 charset-1
        )
        foreach(DLL_NAME ${TEXT_EXTRACTION_DLLS})
            if(EXISTS "${VCPKG_BIN_DIR}/${DLL_NAME}.dll")
                add_custom_command(TARGET familyvault_tests POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${VCPKG_BIN_DIR}/${DLL_NAME}.dll"
                        $<TARGET_FILE_DIR:familyvault_tests>/
                    COMMENT "Copying ${DLL_NAME}.dll to tests directory"
                )
            endif()
        endforeach()
    endif()
endif()