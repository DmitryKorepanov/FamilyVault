# Tests CMakeLists.txt

set(TEST_SOURCES
    test_main.cpp
    test_version.cpp
    test_database.cpp
    test_mime_type.cpp
    test_index_manager.cpp
    test_search_engine.cpp
    test_tags.cpp
    test_text_extractor.cpp
    test_content_indexer.cpp
)

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
include(GoogleTest)
gtest_discover_tests(familyvault_tests)

# Copy test fixtures
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)

# Copy familyvault.dll to tests directory after build
if(WIN32)
    add_custom_command(TARGET familyvault_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:familyvault>
            $<TARGET_FILE_DIR:familyvault_tests>/
        COMMENT "Copying familyvault.dll to tests directory"
    )
endif()