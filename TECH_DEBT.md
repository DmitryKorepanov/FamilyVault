# Technical Debt Log

## Core C++

### [Low] Database Code Duplication
- **File**: `core/include/familyvault/Database.h`
- **Issue**: `struct StmtGuard` is defined locally inside every template method (`execute`, `query`, `queryOne`, etc.).
- **Fix**: Move `StmtGuard` to be a private nested class of `Database` to adhere to DRY principles.
- **Added**: 2025-12-11 (GDrive Iteration 1 Review)


