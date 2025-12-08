# FamilyVault

**Приложение для поиска и доступа к файлам на всех устройствах семьи.**

Единый индекс локальных папок и облачных хранилищ с полнотекстовым поиском.

## 📊 Статус

| Этап | Описание | Статус |
|------|----------|--------|
| 0 | Настройка окружения | ✅ Готово |
| 1 | C++ Core (Database, Index, Search) | ✅ Готово |
| 2 | Flutter App + FFI интеграция | ✅ Готово |
| 3 | Извлечение текста (PDF, DOCX, TXT, HTML) | ✅ Готово |
| 4 | P2P синхронизация | 🔲 Планируется |
| 5 | Google Drive интеграция | 🔲 Планируется |

## 🧪 Тестовое покрытие

| Компонент | Тестов | Описание |
|-----------|--------|----------|
| C++ Core | 137 | ContentIndexer, FileScanner, SearchEngine, IndexManager, Database, TextExtractor, Tags, MimeType |
| Dart/Flutter | 101 | Models, Formatters, Providers |
| **Всего** | **238** | |

## 🎯 Возможности

### Реализовано ✅
- 🔍 **Полнотекстовый поиск** — FTS5 поиск по именам файлов и содержимому
- 📁 **Индексация папок** — сканирование и отслеживание локальных папок
- 📄 **Извлечение текста** — PDF, DOCX, ODT, TXT, HTML, Markdown, JSON, XML
- 🏷️ **Теги** — добавление и поиск по тегам
- 🔎 **Поиск дубликатов** — нахождение одинаковых файлов по контрольной сумме
- 🖼️ **Превью изображений** — отображение миниатюр в интерфейсе
- 📊 **EXIF данные** — извлечение метаданных из фотографий

### Планируется 🔲
- 🔄 **P2P синхронизация** — прямое соединение устройств в локальной сети
- ☁️ **Google Drive** — интеграция с облачным хранилищем
- 🔐 **Безопасность** — TLS 1.3 PSK для P2P, секреты в OS Secure Storage

## 🏗️ Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│   C++ Core                 Flutter App (все платформы)      │
│   ┌──────────────┐        ┌────────────────────────────┐    │
│   │ Database     │        │                            │    │
│   │ IndexManager │  FFI   │  🖥️ Windows    ✅          │    │
│   │ SearchEngine │◄──────►│  📱 Android    ✅          │    │
│   │ FileScanner  │        │  📱 iOS        🔲          │    │
│   │ TextExtract  │        │  🌐 Web        🔲          │    │
│   │ TagManager   │        │                            │    │
│   └──────────────┘        │  Единый UI на Dart!        │    │
│         │                 └────────────────────────────┘    │
│   C API (familyvault_c.h)                                   │
└─────────────────────────────────────────────────────────────┘
```

## 📋 Требования

### Сборка C++ Core

- CMake 3.21+
- C++20 компилятор (MSVC 2022, GCC 11+, Clang 14+)
- vcpkg (для управления зависимостями)
- Ninja (рекомендуется)

### Сборка Flutter App

- Flutter SDK 3.16.0+
- Dart SDK 3.2.0+
- Android SDK (для Android)
- Visual Studio 2022 с C++ workload (для Windows)

## 🚀 Быстрый старт

### 1. Клонирование репозитория

```bash
git clone https://github.com/DmitryKorepanov/FamilyVault.git
cd FamilyVault
```

### 2. Настройка vcpkg

```bash
# Установите vcpkg если ещё не установлен
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat  # Windows
./bootstrap-vcpkg.sh   # Linux/macOS

# Установите переменную окружения
set VCPKG_ROOT=C:\path\to\vcpkg  # Windows
export VCPKG_ROOT=/path/to/vcpkg  # Linux/macOS
```

### 3. Сборка C++ Core

```bash
# Конфигурация
cmake --preset windows-x64

# Сборка
cmake --build --preset windows-x64-release
```

### 4. Запуск Flutter приложения

```bash
cd app
flutter pub get
flutter run -d windows  # или android
```

### Автоматизированный запуск

```bash
# Полный цикл: сборка ядра + запуск приложения
dart run tool/run.dart
```

## 📁 Структура проекта

```
FamilyVault/
├── CMakeLists.txt          # Сборка C++ core
├── vcpkg.json              # C++ зависимости
├── core/                   # C++ библиотека
│   ├── include/familyvault/
│   │   ├── core.h
│   │   ├── export.h
│   │   └── familyvault_c.h # C API для FFI
│   └── src/
├── app/                    # Flutter приложение
│   ├── lib/
│   │   ├── core/           # FFI и сервисы
│   │   ├── features/       # Фичи приложения
│   │   └── shared/         # Общие компоненты
│   ├── windows/
│   └── android/
├── tests/                  # Тесты
│   ├── core/               # C++ unit тесты
│   └── app/                # Flutter тесты
└── tool/                   # Скрипты сборки (Dart)
```

## 🔧 Разработка

### Скрипты сборки

```bash
# Сборка C++ Core
dart run tool/build_core.dart

# Генерация FFI биндингов
dart run tool/generate_ffi.dart

# Полный цикл разработки
dart run tool/run.dart

# Очистка артефактов
dart run tool/clean.dart
```

### Запуск тестов

```bash
# C++ тесты (137 тестов)
cd build/windows-x64
ctest --output-on-failure

# Dart/Flutter тесты (101 тест)
cd app
flutter test
```

### Конфигурации сборки

| Preset | Платформа | Описание |
|--------|-----------|----------|
| `windows-x64` | Windows | Desktop x64 |
| `android-arm64` | Android | ARM64 (современные устройства) |

## 📚 Документация

- [SPECIFICATIONS.md](./SPECIFICATIONS.md) — технические спецификации (API, типы, модели)
- [.cursorrules](./.cursorrules) — кодовое соглашение

## 📄 Лицензия

MIT License — см. [LICENSE](./LICENSE)

