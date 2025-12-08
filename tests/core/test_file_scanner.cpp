// test_file_scanner.cpp — тесты FileScanner

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <gtest/gtest.h>
#include "familyvault/FileScanner.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════

class FileScannerTest : public ::testing::Test {
protected:
    std::string testFolderPath;
    std::unique_ptr<FileScanner> scanner;

    void SetUp() override {
        testFolderPath = "test_scanner_folder_" + std::to_string(std::rand());
        fs::create_directories(testFolderPath);
        scanner = std::make_unique<FileScanner>();
    }

    void TearDown() override {
        scanner.reset();
        if (fs::exists(testFolderPath)) {
            fs::remove_all(testFolderPath);
        }
    }

    void createTestFile(const std::string& relativePath, const std::string& content = "test") {
        std::string fullPath = testFolderPath + "/" + relativePath;
        fs::create_directories(fs::path(fullPath).parent_path());
        std::ofstream file(fullPath, std::ios::binary);
        file.write(content.data(), content.size());
    }

    void createHiddenFile(const std::string& relativePath, const std::string& content = "hidden") {
        std::string fullPath = testFolderPath + "/" + relativePath;
        fs::create_directories(fs::path(fullPath).parent_path());
        std::ofstream file(fullPath, std::ios::binary);
        file.write(content.data(), content.size());
        
#ifdef _WIN32
        SetFileAttributesA(fullPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    }
};

// ═══════════════════════════════════════════════════════════
// Basic Scanning Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, ScanEmptyDirectory) {
    std::vector<ScannedFile> files;
    
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_TRUE(files.empty());
}

TEST_F(FileScannerTest, ScanSingleFile) {
    createTestFile("test.txt", "Hello World");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].name, "test.txt");
    EXPECT_EQ(files[0].extension, "txt");
    EXPECT_EQ(files[0].size, 11);
}

TEST_F(FileScannerTest, ScanMultipleFiles) {
    createTestFile("file1.txt", "Content 1");
    createTestFile("file2.jpg", "JPEG");
    createTestFile("file3.pdf", "PDF");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_EQ(files.size(), 3);
}

TEST_F(FileScannerTest, ScanNestedDirectories) {
    createTestFile("level1/file1.txt");
    createTestFile("level1/level2/file2.txt");
    createTestFile("level1/level2/level3/file3.txt");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_EQ(files.size(), 3);
    
    // Check relative paths
    auto hasPath = [&files](const std::string& path) {
        for (const auto& f : files) {
            // Normalize path separators for comparison
            std::string normalizedPath = f.relativePath;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
            if (normalizedPath == path) return true;
        }
        return false;
    };
    
    EXPECT_TRUE(hasPath("level1/file1.txt"));
    EXPECT_TRUE(hasPath("level1/level2/file2.txt"));
    EXPECT_TRUE(hasPath("level1/level2/level3/file3.txt"));
}

TEST_F(FileScannerTest, ScanFileMetadata) {
    createTestFile("document.pdf", "%PDF-1.4 content");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].name, "document.pdf");
    EXPECT_EQ(files[0].extension, "pdf");
    EXPECT_EQ(files[0].contentType, ContentType::Document);
    EXPECT_GT(files[0].modifiedAt, 0);
    EXPECT_GT(files[0].size, 0);
}

// ═══════════════════════════════════════════════════════════
// Progress Callback Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, ProgressCallbackIsCalled) {
    createTestFile("file1.txt");
    createTestFile("file2.txt");
    createTestFile("file3.txt");
    
    int progressCalls = 0;
    ScanProgress lastProgress;
    
    scanner->scan(
        testFolderPath,
        [](const ScannedFile&) {},
        [&progressCalls, &lastProgress](const ScanProgress& p) {
            progressCalls++;
            lastProgress = p;
        }
    );
    
    EXPECT_GT(progressCalls, 0);
    EXPECT_EQ(lastProgress.totalFiles, 3);
}

TEST_F(FileScannerTest, ProgressShowsCurrentFile) {
    createTestFile("test_file.txt", "content");
    
    std::vector<std::string> currentFiles;
    
    scanner->scan(
        testFolderPath,
        [](const ScannedFile&) {},
        [&currentFiles](const ScanProgress& p) {
            if (!p.currentFile.empty()) {
                currentFiles.push_back(p.currentFile);
            }
        }
    );
    
    EXPECT_FALSE(currentFiles.empty());
}

// ═══════════════════════════════════════════════════════════
// Cancellation Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, CancellationStopsScan) {
    // Create many files
    for (int i = 0; i < 100; i++) {
        createTestFile("file" + std::to_string(i) + ".txt");
    }
    
    CancellationToken token;
    int filesScanned = 0;
    
    scanner->scan(
        testFolderPath,
        [&filesScanned, &token](const ScannedFile&) {
            filesScanned++;
            if (filesScanned >= 10) {
                token.cancel();
            }
        },
        nullptr,
        {},
        token
    );
    
    // Should have stopped before scanning all 100 files
    EXPECT_LT(filesScanned, 100);
}

TEST_F(FileScannerTest, CancellationTokenReset) {
    CancellationToken token;
    
    EXPECT_FALSE(token.isCancelled());
    
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
    
    token.reset();
    EXPECT_FALSE(token.isCancelled());
}

TEST_F(FileScannerTest, CancelAllStopsRunning) {
    // Create files
    for (int i = 0; i < 50; i++) {
        createTestFile("file" + std::to_string(i) + ".txt");
    }
    
    std::atomic<int> filesScanned{0};
    std::atomic<bool> scanStarted{false};
    
    // Start scan in background
    std::thread scanThread([this, &filesScanned, &scanStarted]() {
        scanner->scan(
            testFolderPath,
            [&filesScanned, &scanStarted](const ScannedFile&) {
                scanStarted = true;
                filesScanned++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        );
    });
    
    // Wait for scan to start
    while (!scanStarted) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Cancel all
    scanner->cancelAll();
    
    scanThread.join();
    
    // Should have stopped before scanning all files
    EXPECT_LT(filesScanned.load(), 50);
}

// ═══════════════════════════════════════════════════════════
// Options Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, ExcludePatterns) {
    createTestFile("keep.txt");
    createTestFile("skip.log");
    createTestFile("subdir/another.log");
    
    ScanOptions options;
    options.excludePatterns = {"*.log"};
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    }, nullptr, options);
    
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].name, "keep.txt");
}

TEST_F(FileScannerTest, IncludeExtensions) {
    createTestFile("doc.txt");
    createTestFile("image.jpg");
    createTestFile("video.mp4");
    
    ScanOptions options;
    options.includeExtensions = {"txt", "jpg"};
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    }, nullptr, options);
    
    EXPECT_EQ(files.size(), 2);
}

TEST_F(FileScannerTest, SkipHiddenFilesDefault) {
    createTestFile("visible.txt");
    createHiddenFile(".hidden");
    
    std::vector<ScannedFile> files;
    ScanOptions options;
    options.includeHidden = false;
    
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    }, nullptr, options);
    
    // Should only have visible file
    bool hasHidden = false;
    for (const auto& f : files) {
        if (f.name.find(".hidden") != std::string::npos || f.name[0] == '.') {
            hasHidden = true;
        }
    }
    EXPECT_FALSE(hasHidden);
}

TEST_F(FileScannerTest, IncludeHiddenFiles) {
    createTestFile("visible.txt");
    createTestFile(".hidden");
    
    ScanOptions options;
    options.includeHidden = true;
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    }, nullptr, options);
    
    EXPECT_GE(files.size(), 2);
}

// ═══════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, NonExistentPath) {
    std::vector<ScannedFile> files;
    
    // Should not throw, just return empty
    EXPECT_NO_THROW({
        scanner->scan("/nonexistent/path/12345", [&files](const ScannedFile& f) {
            files.push_back(f);
        });
    });
    
    EXPECT_TRUE(files.empty());
}

TEST_F(FileScannerTest, EmptyFileName) {
    // Create a valid file to ensure scanner works
    createTestFile("valid.txt");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    // All files should have non-empty names
    for (const auto& f : files) {
        EXPECT_FALSE(f.name.empty());
    }
}

TEST_F(FileScannerTest, DeepNesting) {
    // Create deeply nested structure
    std::string deepPath = "a/b/c/d/e/f/g/h/i/j/file.txt";
    createTestFile(deepPath);
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_EQ(files.size(), 1);
}

TEST_F(FileScannerTest, SpecialCharactersInFilename) {
    createTestFile("file with spaces.txt");
    createTestFile("file-with-dashes.txt");
    createTestFile("file_with_underscores.txt");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_EQ(files.size(), 3);
}

TEST_F(FileScannerTest, ZeroSizeFile) {
    createTestFile("empty.txt", "");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].size, 0);
}

TEST_F(FileScannerTest, LargeFileSize) {
    // Create 1MB file
    std::string content(1024 * 1024, 'X');
    createTestFile("large.bin", content);
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files[0].size, 1024 * 1024);
}

// ═══════════════════════════════════════════════════════════
// Content Type Detection Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, DetectsImageContentType) {
    createTestFile("photo.jpg", "\xFF\xD8\xFF");
    createTestFile("picture.png", "\x89PNG");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    for (const auto& f : files) {
        EXPECT_EQ(f.contentType, ContentType::Image);
    }
}

TEST_F(FileScannerTest, DetectsVideoContentType) {
    createTestFile("movie.mp4", "video");
    createTestFile("clip.avi", "video");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    for (const auto& f : files) {
        EXPECT_EQ(f.contentType, ContentType::Video);
    }
}

TEST_F(FileScannerTest, DetectsDocumentContentType) {
    createTestFile("doc.pdf", "%PDF");
    createTestFile("text.txt", "Hello");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    EXPECT_EQ(files.size(), 2);
    for (const auto& f : files) {
        EXPECT_EQ(f.contentType, ContentType::Document);
    }
}

TEST_F(FileScannerTest, DetectsAudioContentType) {
    createTestFile("music.mp3", "ID3");
    createTestFile("sound.wav", "RIFF");
    
    std::vector<ScannedFile> files;
    scanner->scan(testFolderPath, [&files](const ScannedFile& f) {
        files.push_back(f);
    });
    
    for (const auto& f : files) {
        EXPECT_EQ(f.contentType, ContentType::Audio);
    }
}

// ═══════════════════════════════════════════════════════════
// State Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileScannerTest, IsRunningDuringScan) {
    createTestFile("file.txt");
    
    std::atomic<bool> wasRunning{false};
    
    scanner->scan(testFolderPath, [this, &wasRunning](const ScannedFile&) {
        wasRunning = scanner->isRunning();
    });
    
    EXPECT_TRUE(wasRunning);
    EXPECT_FALSE(scanner->isRunning());
}

TEST_F(FileScannerTest, InitiallyNotRunning) {
    EXPECT_FALSE(scanner->isRunning());
}

