// test_mime_type.cpp — тесты MimeTypeDetector

#include <gtest/gtest.h>
#include "familyvault/MimeTypeDetector.h"

using namespace FamilyVault;

TEST(MimeTypeDetectorTest, DetectByExtension_Images) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("jpg"), "image/jpeg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("jpeg"), "image/jpeg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("png"), "image/png");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("gif"), "image/gif");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("webp"), "image/webp");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("bmp"), "image/bmp");
}

TEST(MimeTypeDetectorTest, DetectByExtension_Videos) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("mp4"), "video/mp4");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("avi"), "video/x-msvideo");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("mkv"), "video/x-matroska");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("mov"), "video/quicktime");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("webm"), "video/webm");
}

TEST(MimeTypeDetectorTest, DetectByExtension_Audio) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("mp3"), "audio/mpeg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("wav"), "audio/wav");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("ogg"), "audio/ogg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("flac"), "audio/flac");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("m4a"), "audio/mp4");
}

TEST(MimeTypeDetectorTest, DetectByExtension_Documents) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("pdf"), "application/pdf");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("doc"), "application/msword");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("txt"), "text/plain");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("html"), "text/html");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("json"), "application/json");
}

TEST(MimeTypeDetectorTest, DetectByExtension_Archives) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("zip"), "application/zip");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("rar"), "application/vnd.rar");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("7z"), "application/x-7z-compressed");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("tar"), "application/x-tar");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("gz"), "application/gzip");
}

TEST(MimeTypeDetectorTest, DetectByExtension_CaseInsensitive) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("JPG"), "image/jpeg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("Png"), "image/png");
    EXPECT_EQ(MimeTypeDetector::detectByExtension("PDF"), "application/pdf");
}

TEST(MimeTypeDetectorTest, DetectByExtension_WithDot) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension(".jpg"), "image/jpeg");
    EXPECT_EQ(MimeTypeDetector::detectByExtension(".pdf"), "application/pdf");
}

TEST(MimeTypeDetectorTest, DetectByExtension_Unknown) {
    EXPECT_EQ(MimeTypeDetector::detectByExtension("xyz"), "application/octet-stream");
    EXPECT_EQ(MimeTypeDetector::detectByExtension(""), "application/octet-stream");
}

TEST(MimeTypeDetectorTest, ExtractExtension) {
    EXPECT_EQ(MimeTypeDetector::extractExtension("photo.jpg"), "jpg");
    EXPECT_EQ(MimeTypeDetector::extractExtension("document.PDF"), "pdf");
    EXPECT_EQ(MimeTypeDetector::extractExtension("archive.tar.gz"), "gz");
    EXPECT_EQ(MimeTypeDetector::extractExtension("noextension"), "");
    EXPECT_EQ(MimeTypeDetector::extractExtension(".hidden"), "hidden");
}

TEST(MimeTypeDetectorTest, MimeToContentType) {
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("image/jpeg"), ContentType::Image);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("image/png"), ContentType::Image);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("video/mp4"), ContentType::Video);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("audio/mpeg"), ContentType::Audio);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("application/pdf"), ContentType::Document);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("text/plain"), ContentType::Document);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("application/zip"), ContentType::Archive);
    EXPECT_EQ(MimeTypeDetector::mimeToContentType("application/octet-stream"), ContentType::Other);
}

