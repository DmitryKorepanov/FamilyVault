// test_version.cpp — тест версии библиотеки

#include <gtest/gtest.h>
#include "familyvault/familyvault_c.h"
#include "familyvault/core.h"

TEST(VersionTest, ReturnsCorrectVersion) {
    const char* version = fv_version();
    ASSERT_NE(version, nullptr);
    EXPECT_STREQ(version, "0.1.0");
}

TEST(VersionTest, VersionMatchesCoreHeader) {
    const char* c_version = fv_version();
    EXPECT_STREQ(c_version, FamilyVault::VERSION);
}

TEST(ErrorMessageTest, ReturnsCorrectMessages) {
    EXPECT_STREQ(fv_error_message(FV_OK), "Success");
    EXPECT_STREQ(fv_error_message(FV_ERROR_INVALID_ARGUMENT), "Invalid argument");
    EXPECT_STREQ(fv_error_message(FV_ERROR_NOT_FOUND), "Not found");
    EXPECT_STREQ(fv_error_message(FV_ERROR_DATABASE), "Database error");
    EXPECT_STREQ(fv_error_message(FV_ERROR_IO), "I/O error");
    EXPECT_STREQ(fv_error_message(FV_ERROR_INTERNAL), "Internal error");
}

TEST(FreeStringTest, HandlesNull) {
    // Не должен падать при передаче nullptr
    fv_free_string(nullptr);
}
