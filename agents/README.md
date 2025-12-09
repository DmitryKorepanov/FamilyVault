# Multi-Agent Development

Автоматический цикл разработки с code review.

## Команды

### 1. Маленькие задачи — сразу кодить:
```bash
./agents/autodev.sh "Добавь валидацию email"
```

### 2. Большие фичи — создать план:
```bash
./agents/autodev.sh --plan "Интеграция с Google Drive по @tasks/GDRIVE.md"
```
→ Сохранит: `workspace/*-plan.md` и `*-decisions.md`
→ **PLAN REVIEWER** проверит план перед сохранением.

### 3. Выполнить готовый план:
```bash
./agents/autodev.sh --run agents/workspace/google-drive-plan.md
```

### 4. Продолжить с определённой итерации:
```bash
./agents/autodev.sh --run agents/workspace/plan.md --from 3
```

### 5. Выполнить только одну итерацию:
```bash
./agents/autodev.sh --run agents/workspace/plan.md --only 2
```

### 6. Продолжить после перезапуска:
```bash
# Chat ID логируется в history.md
./agents/autodev.sh --run plan.md --resume-chat abc-123-def
```

### 7. Чистый контекст для каждой итерации фичи:
```bash
./agents/autodev.sh --run agents/workspace/plan.md --fresh
```

## Что делает скрипт

1. **CODER** реализует итерацию
   - Если задаёт вопросы → пауза для ответов
2. **Сборка** — `cmake --build` + `ctest` автоматически
   - Если нет `build/` → пытается создать через `cmake -S . -B`
3. **Diff** — собирает `git diff` (большие сохраняются в файл)
4. **CODE_REVIEWER** проверяет код + результаты сборки
5. **CRITICAL/HIGH?** → CODER исправляет
6. **APPROVED?** → следующая итерация

## Модели

| Роль | По умолчанию | Контекст |
|------|--------------|----------|
| PLANNER | gemini-3-pro | Новый |
| PLAN_REVIEWER | gemini-3-pro | Чистый |
| CODER | gemini-3-pro | Сохраняет* |
| CODE_REVIEWER | gemini-3-pro | Чистый |

Доступные модели: `gemini-3-pro`, `gpt-5`, `sonnet-4`, `sonnet-4-thinking`

\* С `--fresh` — новый для каждой итерации фичи

## Переменные окружения

```bash
# Модели (доступные: gemini-3-pro, gpt-5, sonnet-4, sonnet-4-thinking)
CODER_MODEL=gemini-3-pro
CODE_REVIEWER_MODEL=gemini-3-pro
PLANNER_MODEL=gemini-3-pro

# Таймауты и retry
AGENT_TIMEOUT=300          # Таймаут для cursor-agent (секунды)
MAX_RETRIES=3              # Количество повторных попыток при ошибке
RETRY_DELAY=5              # Базовая задержка между попытками (exponential backoff + jitter)

# Защита от зацикливания
MAX_CONSECUTIVE_FAILURES=5 # Макс. неудачных ревью подряд
MAX_QUESTION_ROUNDS=3      # Макс. раундов вопросов от CODER

# Лимиты промптов (строки)
PLAN_LINES_LIMIT=200       # Макс. строк плана в промпте
SUMMARIES_LINES_LIMIT=100  # Макс. строк summaries в промпте
DIFF_LINES_LIMIT=500       # Макс. строк diff для reviewer
```

## Логирование

```
agents/workspace/logs/
├── session_20251209_143052.log         # Полный лог
├── session_20251209_143052_history.md  # История + Chat ID
├── diff_20251209_143052_1.patch        # Diff итерации 1
├── build_20251209_143052_1.log         # Лог сборки итерации 1
└── ...
```

**Chat ID** для восстановления логируется в `history.md`:
```
**CODER Chat ID:** `abc-123-def`
(для продолжения: --resume-chat abc-123-def)
```

## Требования

```bash
wsl --install Ubuntu
wsl -d Ubuntu -- bash -c "curl https://cursor.com/install -fsSL | bash"
```

## Сборка и тесты

Скрипт автоматически запускает:

**C++:**
- `cmake --build build/windows-x64 --config Release`
- `ctest --test-dir build/windows-x64 -C Release`

**Flutter/Dart:**
- `flutter analyze --no-fatal-infos`
- `flutter test`

Результаты включаются в промпт для CODE_REVIEWER.
