#!/bin/bash
# Автоматическая разработка с циклом code review
#
# Режимы:
#   ./autodev.sh "Задача"                    - сразу кодить
#   ./autodev.sh --plan "Большая фича"       - создать план
#   ./autodev.sh --run plan.md               - выполнить план
#   ./autodev.sh --run plan.md --from 3      - начать с итерации 3
#   ./autodev.sh --run plan.md --only 2      - выполнить только итерацию 2
#   ./autodev.sh --run plan.md --fresh       - новый CODER для каждой итерации фичи
#   ./autodev.sh --run plan.md --resume-chat <id> - продолжить с существующим CODER

set -e

# ═══════════════════════════════════════════════════════════
# Cleanup при прерывании
# ═══════════════════════════════════════════════════════════

# PID файл для отслеживания запущенных процессов (внутри WSL)
WSL_AGENT_PID_FILE=""
# Windows timeout PID для kill при cleanup
TIMEOUT_PID=""

cleanup() {
    echo ""
    echo -e "\033[1;33m⚠️  Прервано! Очистка...\033[0m"
    
    # Убиваем timeout процесс (Windows side)
    if [ -n "$TIMEOUT_PID" ]; then
        kill "$TIMEOUT_PID" 2>/dev/null || true
        echo "Killed timeout process: $TIMEOUT_PID"
    fi
    
    # Убиваем cursor-agent процесс внутри WSL (по PID файлу в WSL)
    if [ -n "$WSL_AGENT_PID_FILE" ]; then
        local agent_pid
        agent_pid=$(wsl -d Ubuntu -- cat "$WSL_AGENT_PID_FILE" 2>/dev/null || true)
        if [ -n "$agent_pid" ]; then
            wsl -d Ubuntu -- kill "$agent_pid" 2>/dev/null || true
            echo "Killed WSL agent process: $agent_pid"
        fi
        wsl -d Ubuntu -- rm -f "$WSL_AGENT_PID_FILE" 2>/dev/null || true
    fi
    
    # Fallback: убиваем cursor-agent только нашей сессии (по SESSION_ID в команде)
    # Это безопаснее чем pkill -f "cursor-agent" который убьёт все процессы
    if [ -n "$SESSION_ID" ]; then
        wsl -d Ubuntu -- pkill -f "cursor-agent.*$SESSION_ID" 2>/dev/null || true
    fi
    
    exit 130
}
trap cleanup SIGINT SIGTERM

# ═══════════════════════════════════════════════════════════
# Пути и конфигурация
# ═══════════════════════════════════════════════════════════

PROJECT_ROOT=$(pwd)
# Windows/Git Bash path → WSL path
# Git Bash: /c/FamilyVault → /mnt/c/FamilyVault
# Windows:  C:\FamilyVault → /mnt/c/FamilyVault
if [[ "$PROJECT_ROOT" =~ ^/([a-zA-Z])/ ]]; then
    # Git Bash style: /c/FamilyVault → /mnt/c/FamilyVault
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    WSL_PROJECT_ROOT="/mnt/${DRIVE_LETTER,,}${PROJECT_ROOT:2}"
elif [[ "$PROJECT_ROOT" =~ ^([A-Za-z]): ]]; then
    # Windows style: C:\FamilyVault → /mnt/c/FamilyVault
    DRIVE_LETTER="${BASH_REMATCH[1]}"
    WSL_PROJECT_ROOT="/mnt/${DRIVE_LETTER,,}${PROJECT_ROOT:2}"
    WSL_PROJECT_ROOT=$(echo "$WSL_PROJECT_ROOT" | tr '\\' '/')
else
    # Already Unix-style or unknown
    WSL_PROJECT_ROOT="$PROJECT_ROOT"
fi

# Timeout для cursor-agent (секунды)
AGENT_TIMEOUT="${AGENT_TIMEOUT:-300}"

# Парсинг аргументов
MODE="direct"  # direct | plan | run
PLAN_FILE=""
START_ITER=1
ONLY_ITER=""   # Выполнить только эту итерацию
FRESH_CODER=false  # Создавать нового CODER'а для каждой итерации фичи
RESUME_CHAT_ID=""  # ID чата для продолжения

while [[ $# -gt 0 ]]; do
    case $1 in
        --plan)
            MODE="plan"
            shift
            ;;
        --run)
            MODE="run"
            PLAN_FILE="$2"
            shift 2
            ;;
        --from)
            START_ITER="$2"
            shift 2
            ;;
        --only)
            ONLY_ITER="$2"
            START_ITER="$2"
            shift 2
            ;;
        --fresh)
            FRESH_CODER=true
            shift
            ;;
        --resume-chat)
            RESUME_CHAT_ID="$2"
            shift 2
            ;;
        *)
            if [ -z "$TASK" ]; then
                TASK="$1"
            else
                MAX_ITER="$1"
            fi
            shift
            ;;
    esac
done

# Проверка конфликта --only и --from
if [ -n "$ONLY_ITER" ] && [ "$START_ITER" != "$ONLY_ITER" ]; then
    echo "❌ Ошибка: --only $ONLY_ITER и --from $START_ITER конфликтуют" >&2
    echo "Используйте что-то одно." >&2
    exit 1
fi

# MAX_ITER будет вычислен после валидации плана
if [ -z "$MAX_ITER" ]; then
    MAX_ITER=0  # Временно, пересчитаем после валидации
fi
ITER=0

# Счётчик неудачных ревью подряд (защита от бесконечного цикла)
CONSECUTIVE_REVIEW_FAILURES=0
MAX_CONSECUTIVE_FAILURES="${MAX_CONSECUTIVE_FAILURES:-5}"

# Pattern для определения вопросов от CODER
QUESTION_PATTERN="## [Вв]опрос|### [Вв]опрос|⏸️|[Жж]ду ответ|waiting.*answer|QUESTIONS:"

# Лимит раундов вопросов
MAX_QUESTION_ROUNDS="${MAX_QUESTION_ROUNDS:-3}"

# Модели для разных ролей
# Доступные: gemini-3-pro, gpt-5.1, gpt-5.1-codex, sonnet-4.5, opus-4.5, auto
CODER_MODEL="${CODER_MODEL:-gemini-3-pro}"
CODE_REVIEWER_MODEL="${CODE_REVIEWER_MODEL:-gemini-3-pro}"
PLANNER_MODEL="${PLANNER_MODEL:-gemini-3-pro}"

# Chat IDs (инициализируем пустыми)
PLANNER_CHAT_ID=""
CODER_CHAT_ID=""

# Рабочая директория
WORKSPACE="agents/workspace"
mkdir -p "$WORKSPACE"

# Логирование
SESSION_ID=$(date +%Y%m%d_%H%M%S)
LOG_DIR="$WORKSPACE/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/session_${SESSION_ID}.log"
HISTORY_FILE="$LOG_DIR/session_${SESSION_ID}_history.md"
# PID файл внутри WSL для правильного kill
WSL_AGENT_PID_FILE="/tmp/cursor_agent_${SESSION_ID}.pid"

# Инициализация лога
{
    echo "═══════════════════════════════════════════════════════════════"
    echo "SESSION: $SESSION_ID"
    echo "START: $(date)"
    echo "MODE: $MODE"
    echo "TASK: $TASK"
    echo "PLAN_FILE: $PLAN_FILE"
    echo "CODER_MODEL: $CODER_MODEL"
    echo "CODE_REVIEWER_MODEL: $CODE_REVIEWER_MODEL"
    echo "PLANNER_MODEL: $PLANNER_MODEL"
    echo "═══════════════════════════════════════════════════════════════"
} > "$LOG_FILE"

# Инициализация истории
{
    echo "# Session $SESSION_ID"
    echo ""
    echo "- **Дата:** $(date)"
    echo "- **Режим:** $MODE"
    echo "- **Задача:** $TASK"
    echo ""
} > "$HISTORY_FILE"

# Функция логирования
log_agent() {
    local role="$1"
    local model="$2"
    local prompt="$3"
    local response="$4"
    
    {
        echo ""
        echo "───────────────────────────────────────────────────────────────"
        echo "[$role] $(date '+%H:%M:%S') - $model"
        echo "───────────────────────────────────────────────────────────────"
        echo "PROMPT:"
        echo "$prompt" | head -20
        echo "..."
        echo ""
        echo "RESPONSE:"
        echo "$response"
    } >> "$LOG_FILE"
    
    {
        echo "## $role ($(date '+%H:%M:%S'))"
        echo ""
        echo "**Model:** $model"
        echo ""
        echo '```'
        echo "$response" | head -100
        echo '```'
        echo ""
    } >> "$HISTORY_FILE"
}

# Проверка аргументов (без PLAN_FILE - проверим после определения error)
if [ "$MODE" != "run" ] && [ -z "$TASK" ]; then
    echo "Использование:"
    echo ""
    echo "  Маленькие задачи (сразу кодить):"
    echo "    ./autodev.sh \"Задача\""
    echo ""
    echo "  Большие фичи (создать план):"
    echo "    ./autodev.sh --plan \"Большая фича\""
    echo ""
    echo "  Выполнить готовый план:"
    echo "    ./autodev.sh --run agents/workspace/plan.md"
    echo ""
    echo "  Продолжить с определённой итерации:"
    echo "    ./autodev.sh --run agents/workspace/plan.md --from 3"
    echo ""
    echo "  Выполнить только одну итерацию:"
    echo "    ./autodev.sh --run agents/workspace/plan.md --only 2"
    echo ""
    echo "  Новый CODER для каждой итерации фичи (чистый контекст):"
    echo "    ./autodev.sh --run agents/workspace/plan.md --fresh"
    echo ""
    echo "  Продолжить с существующим CODER (после перезапуска):"
    echo "    ./autodev.sh --run plan.md --resume-chat <chat-id>"
    echo ""
    echo "Результаты: $WORKSPACE/"
    echo "Chat ID для восстановления логируется в history.md"
    exit 1
fi

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

log() { echo -e "${BLUE}[$1]${NC} $2"; }
success() { echo -e "${GREEN}✅ $1${NC}"; }
error() { echo -e "${RED}❌ $1${NC}"; }
warn() { echo -e "${YELLOW}⚠️  $1${NC}"; }

# ═══════════════════════════════════════════════════════════
# Проверка зависимостей
# ═══════════════════════════════════════════════════════════

check_dependencies() {
    # WSL
    if ! command -v wsl &>/dev/null; then
        error "WSL не найден"
        exit 1
    fi
    
    # Проверка зависимостей
    local missing_deps=0
    
    if ! command -v git &>/dev/null; then
        error "git не найден"
        missing_deps=1
    fi
    
    if ! command -v cmake &>/dev/null; then
        warn "cmake не найден (нужен для C++ сборки)"
        # Не блокируем, т.к. может быть только Dart задача
    fi
    
    if ! command -v flutter &>/dev/null; then
        warn "flutter не найден (нужен для App сборки)"
    fi
    
    # cursor-agent в WSL (проверяем напрямую по пути, т.к. which может не работать)
    if ! wsl -d Ubuntu -- bash -c "test -x ~/.local/bin/cursor-agent" &>/dev/null; then
        error "cursor-agent не найден в WSL Ubuntu"
        echo "Установите: wsl -d Ubuntu -- bash -c \"curl https://cursor.com/install -fsSL | bash\""
        missing_deps=1
    fi
    
    if [ $missing_deps -eq 1 ]; then
        exit 1
    fi
}

check_dependencies

# Проверка PLAN_FILE
if [ "$MODE" = "run" ]; then
    if [ ! -f "$PLAN_FILE" ]; then
        error "Файл плана не найден: $PLAN_FILE"
        exit 1
    fi
    
    # Валидация формата плана
    PLAN_ITERATION_COUNT=$(grep -c "^### Iteration" "$PLAN_FILE" 2>/dev/null || echo 0)
    if [ "$PLAN_ITERATION_COUNT" -eq 0 ]; then
        error "План не содержит итераций!"
        echo "Ожидаемый формат: '### Iteration N: Название'"
        echo "Пример:"
        echo "  ### Iteration 1: Setup database schema"
        echo "  ### Iteration 2: Add API endpoints"
        exit 1
    fi
    log "PLAN" "Найдено итераций: $PLAN_ITERATION_COUNT"
    
    # Пересчитываем MAX_ITER если не задан явно
    if [ "$MAX_ITER" -eq 0 ]; then
        MAX_ITER=$((PLAN_ITERATION_COUNT * 4))
        log "PLAN" "MAX_ITER установлен: $MAX_ITER (${PLAN_ITERATION_COUNT} итераций × 4 попытки)"
    fi
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  AUTODEV: Автоматическая разработка"
echo "═══════════════════════════════════════════════════════════════"

case $MODE in
    direct)
        echo -e "Режим: ${CYAN}ПРЯМОЕ КОДИРОВАНИЕ${NC}"
        echo -e "Задача: ${CYAN}$TASK${NC}"
        ;;
    plan)
        echo -e "Режим: ${CYAN}ПЛАНИРОВАНИЕ${NC}"
        echo -e "Задача: ${CYAN}$TASK${NC}"
        ;;
    run)
        echo -e "Режим: ${CYAN}ВЫПОЛНЕНИЕ ПЛАНА${NC}"
        echo -e "План: ${CYAN}$PLAN_FILE${NC}"
        if [ -n "$ONLY_ITER" ]; then
            echo -e "Итерация: ${CYAN}только Iteration $ONLY_ITER${NC}"
        else
            echo -e "Начать с: ${CYAN}Iteration $START_ITER${NC}"
        fi
        if [ "$FRESH_CODER" = true ]; then
            echo -e "CODER: ${CYAN}новый для каждой итерации фичи${NC}"
        else
            echo -e "CODER: ${CYAN}один на все итерации${NC}"
        fi
        ;;
esac

echo -e "Лог: ${CYAN}$LOG_FILE${NC}"
echo -e "История: ${CYAN}$HISTORY_FILE${NC}"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# ═══════════════════════════════════════════════════════════
# Функции запуска агентов
# ═══════════════════════════════════════════════════════════

PROMPT_FILE="$LOG_DIR/prompt_${SESSION_ID}.txt"
PROMPT_WSL="$WSL_PROJECT_ROOT/$PROMPT_FILE"

# Retry настройки
MAX_RETRIES="${MAX_RETRIES:-3}"
RETRY_DELAY="${RETRY_DELAY:-5}"  # Базовая задержка в секундах

# Базовая функция запуска агента с проверками и retry
_run_agent() {
    local model="$1"
    local resume_id="$2"
    local prompt="$3"
    local output
    local exit_code
    local real_exit
    local attempt
    local agent_output_file
    
    echo "$prompt" > "$PROMPT_FILE"
    
    # Экранируем переменные для безопасности
    local safe_model
    safe_model=$(printf '%q' "$model")
    local safe_resume
    safe_resume=$(printf '%q' "$resume_id")
    # Экранируем пути для скрипта-обёртки
    local safe_project_root
    safe_project_root=$(printf '%q' "$WSL_PROJECT_ROOT")
    local safe_prompt_wsl
    safe_prompt_wsl=$(printf '%q' "$PROMPT_WSL")
    
    local session_injection=""
    if [ -n "$SESSION_ID" ]; then
        session_injection="SESSION_ID='$SESSION_ID' "
    fi
    
    # Создаём скрипт-обёртку для выполнения внутри WSL
    # Это решает проблемы с экранированием кавычек и аргументов при передаче через wsl.exe
    local runner_script="/tmp/agent_runner_${SESSION_ID}_${RANDOM}.sh"
    
    # Формируем команду запуска агента
    local agent_cmd="~/.local/bin/cursor-agent --model $safe_model"
    if [ -n "$resume_id" ] && [ "$resume_id" != "''" ]; then
        agent_cmd="$agent_cmd --resume $safe_resume"
    fi

    # Контент скрипта-обёртки (используем sed для удаления отступов)
    local runner_content
    runner_content=$(sed 's/^        //' <<EOF
        #!/bin/bash
        cd $safe_project_root || exit 1
        ${session_injection}

        # Читаем промпт из файла (безопасно, без экранирования в командной строке)
        PROMPT=\$(cat $safe_prompt_wsl)

        # Запускаем агента
        $agent_cmd chat "\$PROMPT" &
        AGENT_PID=\$!
        echo \$AGENT_PID > '$WSL_AGENT_PID_FILE'

        # Ждём завершения
        wait \$AGENT_PID
        EXIT_CODE=\$?
        rm -f '$WSL_AGENT_PID_FILE'
        echo "__EXIT_CODE__:\$EXIT_CODE"
EOF
)

    # Кодируем скрипт в base64, чтобы безопасно передать через командную строку wsl
    local runner_base64
    runner_base64=$(echo "$runner_content" | base64 -w 0)

    # Команда для выполнения: декодируем -> сохраняем -> запускаем -> удаляем
    local cmd="echo '$runner_base64' | base64 -d > '$runner_script' && chmod +x '$runner_script' && '$runner_script'; RM_EXIT=\$?; rm -f '$runner_script'; exit \$RM_EXIT"
    
    # Retry loop с exponential backoff + jitter
    for attempt in $(seq 1 $MAX_RETRIES); do
        # Запуск с timeout
        set +e
        agent_output_file=$(mktemp)
        timeout "$AGENT_TIMEOUT" wsl -d Ubuntu -- bash -c "$cmd" >"$agent_output_file" 2>&1 &
        TIMEOUT_PID=$!
        wait $TIMEOUT_PID
        exit_code=$?
        TIMEOUT_PID=""
        output=$(cat "$agent_output_file")
        rm -f "$agent_output_file"
        set -e
        
        # Jitter для retry (0-5 секунд)
        local jitter=$((RANDOM % 6))
        
        # Проверка timeout
        if [ $exit_code -eq 124 ]; then
            if [ $attempt -lt $MAX_RETRIES ]; then
                local delay=$((RETRY_DELAY * attempt + jitter))
                warn "cursor-agent timed out (attempt $attempt/$MAX_RETRIES). Retry in ${delay}s..."
                sleep $delay
                continue
            fi
            error "cursor-agent timed out after ${AGENT_TIMEOUT}s (all $MAX_RETRIES attempts failed)"
            echo "TIMEOUT: Agent execution timed out after $MAX_RETRIES attempts" >> "$LOG_FILE"
            exit 1
        fi
        
        # Извлекаем реальный exit code из вывода (POSIX-совместимый способ)
        real_exit=$(echo "$output" | sed -n 's/.*__EXIT_CODE__:\([0-9]*\).*/\1/p' | tail -1)
        output=$(echo "$output" | sed '/__EXIT_CODE__:/d')
        
        # Проверка ошибки (приоритет real_exit над exit_code)
        local failed=false
        if [ -n "$real_exit" ] && [ "$real_exit" -ne 0 ]; then
            failed=true
        elif [ -z "$real_exit" ] && [ $exit_code -ne 0 ]; then
            failed=true
        fi
        
        if [ "$failed" = true ]; then
            if [ $attempt -lt $MAX_RETRIES ]; then
                local delay=$((RETRY_DELAY * attempt + jitter))
                warn "cursor-agent failed (attempt $attempt/$MAX_RETRIES). Retry in ${delay}s..."
                sleep $delay
                continue
            fi
            error "cursor-agent failed (exit code: ${real_exit:-$exit_code}) after $MAX_RETRIES attempts"
            echo "ERROR: Agent execution failed after $MAX_RETRIES attempts: $output" >> "$LOG_FILE"
            exit 1
        fi
        
        # Проверка пустого ответа
        if [ -z "$output" ]; then
            if [ $attempt -lt $MAX_RETRIES ]; then
                local delay=$((RETRY_DELAY * attempt + jitter))
                warn "cursor-agent returned empty response (attempt $attempt/$MAX_RETRIES). Retry in ${delay}s..."
                sleep $delay
                continue
            fi
            error "cursor-agent returned empty response after $MAX_RETRIES attempts"
            echo "ERROR: Empty response from agent after $MAX_RETRIES attempts" >> "$LOG_FILE"
            exit 1
        fi
        
        # Успех — выходим из цикла
        break
    done
    
    echo "$output"
}

run_planner() {
    _run_agent "$PLANNER_MODEL" "$PLANNER_CHAT_ID" "$1"
}

run_coder() {
    _run_agent "$CODER_MODEL" "$CODER_CHAT_ID" "$1"
}

run_code_reviewer() {
    _run_agent "$CODE_REVIEWER_MODEL" "" "$1"
}

# Лимиты на размер контента в промптах (строки)
PLAN_LINES_LIMIT="${PLAN_LINES_LIMIT:-200}"
SUMMARIES_LINES_LIMIT="${SUMMARIES_LINES_LIMIT:-100}"
DIFF_LINES_LIMIT="${DIFF_LINES_LIMIT:-500}"

# Функция для fresh coder (избегаем дублирования)
run_fresh_coder() {
    log "INIT" "Создаю нового CODER'а для следующей итерации..."
    create_coder_session
    local coder_role_content
    coder_role_content=$(cat agents/prompts/coder.md 2>/dev/null || echo "Ты — CODER.")
    
    # Собираем summaries предыдущих итераций (с лимитом)
    local prev_summaries=""
    if [ -f "$ITERATION_SUMMARIES" ]; then
        local summaries_content
        summaries_content=$(tail -$SUMMARIES_LINES_LIMIT "$ITERATION_SUMMARIES")
        prev_summaries="
## Что уже сделано (summaries, последние $SUMMARIES_LINES_LIMIT строк)
$summaries_content
"
    fi
    
    # План с лимитом
    local plan_content
    plan_content=$(head -$PLAN_LINES_LIMIT "$PLAN_FILE")
    local plan_total_lines
    plan_total_lines=$(wc -l < "$PLAN_FILE")
    local plan_note=""
    if [ "$plan_total_lines" -gt "$PLAN_LINES_LIMIT" ]; then
        plan_note="
(план обрезан: показано $PLAN_LINES_LIMIT из $plan_total_lines строк)"
    fi
    
    # Ищем файл решений (относительный путь → абсолютный)
    DECISIONS_FILE=$(grep -m1 "^Решения:" "$PLAN_FILE" | cut -d' ' -f2 || echo "")
    if [ -n "$DECISIONS_FILE" ] && [ ! -f "$DECISIONS_FILE" ]; then
        # Пробуем относительно директории плана
        PLAN_DIR=$(dirname "$PLAN_FILE")
        if [ -f "$PLAN_DIR/$DECISIONS_FILE" ]; then
            DECISIONS_FILE="$PLAN_DIR/$DECISIONS_FILE"
        elif [ -f "$WORKSPACE/$DECISIONS_FILE" ]; then
            DECISIONS_FILE="$WORKSPACE/$DECISIONS_FILE"
        fi
    fi

    local decisions_content=""
    if [ -f "$DECISIONS_FILE" ]; then
        decisions_content="## Решения\n$(cat "$DECISIONS_FILE")"
    fi
    
    local fresh_prompt="$coder_role_content

## План
$plan_content$plan_note
$decisions_content

$prev_summaries

Предыдущие итерации завершены.
Сейчас ты начинаешь новую итерацию.
Посмотри план и определи, какая итерация следующая (после выполненных).
Твоя задача — выполнить ТОЛЬКО эту следующую итерацию.

Посмотри изменённые файлы: git diff --name-only"
    CODER_RESPONSE=$(run_coder "$fresh_prompt")
    log_agent "CODER (fresh)" "$CODER_MODEL" "$fresh_prompt" "$CODER_RESPONSE"
    echo "$CODER_RESPONSE"
}

# Функция сохранения summary итерации
save_iteration_summary() {
    local iter_num="$1"
    local response="$2"
    
    {
        echo ""
        echo "### Iteration $iter_num completed"
        echo ""
        # Берём первые 30 строк как summary
        echo "$response" | head -30
        echo ""
        echo "---"
    } >> "$ITERATION_SUMMARIES"
}

run_plan_reviewer() {
    _run_agent "$CODE_REVIEWER_MODEL" "" "$1"
}

# ═══════════════════════════════════════════════════════════════
# РЕЖИМ: ПЛАНИРОВАНИЕ (--plan)
# ═══════════════════════════════════════════════════════════════

if [ "$MODE" = "plan" ]; then
    # Slug для имени файла
    # Если TASK содержит путь к файлу (начинается с @), используем имя файла как базу
    if [[ "$TASK" =~ @.*\.md ]]; then
        # Извлекаем имя файла без пути и расширения
        # Пример: "@tasks/1. GDRIVE.md" -> "gdrive"
        BASENAME=$(echo "$TASK" | grep -oE '@[^ ]+\.md' | sed 's|^@.*/||' | sed 's|\.md$||')
        # Очищаем от цифр в начале и лишних символов
        SLUG=$(echo "$BASENAME" | sed 's/^[0-9.]*//' | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g' | sed 's/^-//' | sed 's/-$//')
        
        # Если слаг получился пустым (например файл назывался "1.md"), фоллбэк на старый метод
        if [ -z "$SLUG" ]; then
             SLUG=$(echo "$TASK" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g' | sed 's/--*/-/g' | head -c 50)
        fi
    else
        # Старый метод для текстовых задач, но с ограничением слов
        # Берем первые 5 слов для слага
        SHORT_TASK=$(echo "$TASK" | awk '{print $1,$2,$3,$4,$5}')
        SLUG=$(echo "$SHORT_TASK" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g' | sed 's/--*/-/g' | head -c 50 | sed 's/-$//')
    fi
    
    PLAN_FILE="$WORKSPACE/${SLUG}-plan.md"
    DECISIONS_FILE="$WORKSPACE/${SLUG}-decisions.md"
    
    # Создаём чат для PLANNER (сохраняет контекст между шагами)
    log "INIT" "Создаю сессию для PLANNER..."
    PLANNER_CHAT_ID=$(wsl -d Ubuntu -- bash -c "cd '$WSL_PROJECT_ROOT' && ~/.local/bin/cursor-agent create-chat")
    echo -e "PLANNER Chat ID: ${CYAN}$PLANNER_CHAT_ID${NC}"
    
    # Загружаем промпт и структуру проекта
    PLANNER_ROLE=$(cat agents/prompts/planner.md 2>/dev/null || echo "Ты — PLANNER.")
    PROJECT_TREE=$(find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.dart" \) 2>/dev/null | grep -v build | head -50 || echo "")
    
    # ШАГ 1: Вопросы
    QUESTIONS_PROMPT="$PLANNER_ROLE

## Структура проекта
\`\`\`
$PROJECT_TREE
\`\`\`

## Задача
$TASK

Выполни Шаг 1: задай вопросы или напиши 'Вопросов нет'."

    log "PLANNER ($PLANNER_MODEL)" "Анализирую задачу..."
    QUESTIONS=$(run_planner "$QUESTIONS_PROMPT")
    log_agent "PLANNER" "$PLANNER_MODEL" "$QUESTIONS_PROMPT" "$QUESTIONS"
    echo "$QUESTIONS"
    
    # Сохраняем вопросы
    echo "# Вопросы планирования" > "$DECISIONS_FILE"
    echo "" >> "$DECISIONS_FILE"
    echo "Задача: $TASK" >> "$DECISIONS_FILE"
    echo "" >> "$DECISIONS_FILE"
    echo "$QUESTIONS" >> "$DECISIONS_FILE"
    
    ANSWERS_TMP=$(mktemp)
    
    # Check if Planner has no questions
    if echo "$QUESTIONS" | grep -iq "вопросов нет"; then
        echo ""
        echo -e "${GREEN}Planner не имеет вопросов. Автоматический переход к планированию...${NC}"
        echo "Вопросов нет." > "$ANSWERS_TMP"
    else
        echo ""
        echo "═══════════════════════════════════════════════════════════════"
        echo -e "${MAGENTA}Введите ответы (Ctrl+D для завершения, пусто = согласен):${NC}"
        echo "═══════════════════════════════════════════════════════════════"
        cat > "$ANSWERS_TMP"
    fi
    
    if [ ! -s "$ANSWERS_TMP" ]; then
        echo "Согласен с рекомендациями" > "$ANSWERS_TMP"
    fi
    
    ANSWERS=$(cat "$ANSWERS_TMP")
    
    # Сохраняем ответы
    echo "" >> "$DECISIONS_FILE"
    echo "---" >> "$DECISIONS_FILE"
    echo "## Принятые решения" >> "$DECISIONS_FILE"
    cat "$ANSWERS_TMP" >> "$DECISIONS_FILE"
    rm -f "$ANSWERS_TMP"
    
    # ШАГ 2: План (PLANNER помнит контекст через --resume)
    PLAN_PROMPT="Напоминаю задачу: $TASK

Ответы на вопросы: $ANSWERS

Теперь выполни Шаг 2: создай план итераций по формату из своей роли."

    log "PLANNER ($PLANNER_MODEL)" "Создаю план..."
    PLAN=$(run_planner "$PLAN_PROMPT")
    log_agent "PLANNER" "$PLANNER_MODEL" "$PLAN_PROMPT" "$PLAN"
    echo "$PLAN"
    
    # ═══════════════════════════════════════════════════════════
    # ЦИКЛ РЕВЬЮ ПЛАНА
    # ═══════════════════════════════════════════════════════════
    
    PLAN_REVIEWER_ROLE=$(cat agents/prompts/plan_reviewer.md 2>/dev/null || echo "Ты — PLAN REVIEWER.")
    PLAN_ATTEMPTS=0
    MAX_PLAN_ATTEMPTS=3
    
    while [ $PLAN_ATTEMPTS -lt $MAX_PLAN_ATTEMPTS ]; do
        PLAN_ATTEMPTS=$((PLAN_ATTEMPTS + 1))
        
        # Запускаем ревью
        log "PLAN_REVIEWER" "Проверяю план (попытка $PLAN_ATTEMPTS/$MAX_PLAN_ATTEMPTS)..."
        REVIEW_PROMPT="$PLAN_REVIEWER_ROLE

## Задача
$TASK

## Предложенный план
$PLAN

Проверь план на соответствие критериям. Ответь APPROVED или NEEDS_WORK."

        REVIEW=$(run_plan_reviewer "$REVIEW_PROMPT")
        log_agent "PLAN_REVIEWER" "$CODE_REVIEWER_MODEL" "$REVIEW_PROMPT" "$REVIEW"
        echo "$REVIEW"
        
        # Если APPROVED — выходим из цикла
        if echo "$REVIEW" | grep -qi "APPROVED"; then
            success "План одобрен!"
            break
        fi
        
        # Если NEEDS_WORK — просим Planner исправить
        warn "План требует доработки..."
        
        FIX_PROMPT="Plan Reviewer нашёл проблемы в твоём плане:

$REVIEW

Исправь план и выведи его целиком заново.
Не пиши вступлений, только исправленный план в Markdown."

        log "PLANNER" "Исправляю план..."
        PLAN=$(run_planner "$FIX_PROMPT")
        log_agent "PLANNER (fix)" "$PLANNER_MODEL" "$FIX_PROMPT" "$PLAN"
        echo "$PLAN"
    done
    
    if [ $PLAN_ATTEMPTS -ge $MAX_PLAN_ATTEMPTS ]; then
        warn "Превышен лимит попыток улучшения плана. Сохраняю как есть."
    fi

    # Сохраняем план
    {
        echo "# План: $TASK"
        echo ""
        echo "Дата: $(date)"
        echo "Решения: $DECISIONS_FILE"
        echo ""
        echo "$PLAN"
    } > "$PLAN_FILE"
    
    echo ""
    success "План сохранён: $PLAN_FILE"
    success "Решения сохранены: $DECISIONS_FILE"
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "Для запуска реализации:"
    echo ""
    echo -e "  ${CYAN}./agents/autodev.sh --run $PLAN_FILE${NC}"
    echo ""
    echo "Или с определённой итерации:"
    echo ""
    echo -e "  ${CYAN}./agents/autodev.sh --run $PLAN_FILE --from 2${NC}"
    echo "═══════════════════════════════════════════════════════════════"
    exit 0
fi

# ═══════════════════════════════════════════════════════════════
# РЕЖИМ: ВЫПОЛНЕНИЕ ПЛАНА (--run) или ПРЯМОЕ КОДИРОВАНИЕ
# ═══════════════════════════════════════════════════════════════

# Функция создания нового CODER'а
create_coder_session() {
    CODER_CHAT_ID=$(wsl -d Ubuntu -- bash -c "cd '$WSL_PROJECT_ROOT' && ~/.local/bin/cursor-agent create-chat")
    echo -e "CODER Chat ID: ${CYAN}$CODER_CHAT_ID${NC}"
    # Логируем для возможного восстановления
    echo "CODER_CHAT_ID: $CODER_CHAT_ID" >> "$LOG_FILE"
    echo "" >> "$HISTORY_FILE"
    echo "**CODER Chat ID:** \`$CODER_CHAT_ID\`" >> "$HISTORY_FILE"
    echo "(для продолжения: --resume-chat $CODER_CHAT_ID)" >> "$HISTORY_FILE"
    echo "" >> "$HISTORY_FILE"
}

# Загружаем роль CODER
CODER_ROLE=$(cat agents/prompts/coder.md 2>/dev/null || echo "Ты — CODER.")

# Создаём или восстанавливаем чат для CODER
if [ -n "$RESUME_CHAT_ID" ]; then
    log "INIT" "Восстанавливаю сессию CODER: $RESUME_CHAT_ID"
    CODER_CHAT_ID="$RESUME_CHAT_ID"
    echo -e "CODER Chat ID: ${CYAN}$CODER_CHAT_ID${NC} (resumed)"
else
    log "INIT" "Создаю сессию для CODER..."
    create_coder_session
fi

if [ "$MODE" = "run" ]; then
    # Ищем файл решений (относительный путь → абсолютный)
    DECISIONS_FILE=$(grep -m1 "^Решения:" "$PLAN_FILE" | cut -d' ' -f2 || echo "")
    if [ -n "$DECISIONS_FILE" ] && [ ! -f "$DECISIONS_FILE" ]; then
        # Пробуем относительно директории плана
        PLAN_DIR=$(dirname "$PLAN_FILE")
        if [ -f "$PLAN_DIR/$DECISIONS_FILE" ]; then
            DECISIONS_FILE="$PLAN_DIR/$DECISIONS_FILE"
        elif [ -f "$WORKSPACE/$DECISIONS_FILE" ]; then
            DECISIONS_FILE="$WORKSPACE/$DECISIONS_FILE"
        fi
    fi
    
    if [ -n "$ONLY_ITER" ]; then
        CODER_INIT_PROMPT="$CODER_ROLE

## План
$(cat "$PLAN_FILE")

$([ -f "$DECISIONS_FILE" ] && echo "## Решения" && cat "$DECISIONS_FILE")

---

Выполни **ТОЛЬКО Iteration $ONLY_ITER**. Не переходи к другим итерациям.
После завершения напиши 'Итерация $ONLY_ITER завершена'."
    else
        CODER_INIT_PROMPT="$CODER_ROLE

## План
$(cat "$PLAN_FILE")

$([ -f "$DECISIONS_FILE" ] && echo "## Решения" && cat "$DECISIONS_FILE")

---

Начни с **Iteration $START_ITER**.
После завершения каждой итерации сообщи какие файлы изменены."
    fi

else
    CODER_INIT_PROMPT="$CODER_ROLE

Задача: $TASK

Требования:
1. Следуй архитектуре проекта (C++ → FFI → Dart)
2. Пиши тесты
3. Код должен компилироваться

Если есть вопросы — задай их СЕЙЧАС."
fi

log "CODER ($CODER_MODEL)" "Начинаю реализацию..."
CODER_RESPONSE=$(run_coder "$CODER_INIT_PROMPT")
log_agent "CODER" "$CODER_MODEL" "$CODER_INIT_PROMPT" "$CODER_RESPONSE"
echo "$CODER_RESPONSE"

# Проверяем, задал ли CODER вопросы (структурированные маркеры)
QUESTION_ROUNDS=0

while echo "$CODER_RESPONSE" | grep -qE "$QUESTION_PATTERN"; do
    QUESTION_ROUNDS=$((QUESTION_ROUNDS + 1))
    
    if [ $QUESTION_ROUNDS -gt $MAX_QUESTION_ROUNDS ]; then
        warn "Достигнут лимит раундов вопросов ($MAX_QUESTION_ROUNDS). Продолжаем без ответов."
        CODER_RESPONSE=$(run_coder "Лимит вопросов достигнут. Действуй по своему усмотрению и продолжай реализацию.")
        log_agent "CODER" "$CODER_MODEL" "question limit reached" "$CODER_RESPONSE"
        echo "$CODER_RESPONSE"
        break
    fi
    
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo -e "${MAGENTA}CODER задал вопросы (раунд $QUESTION_ROUNDS/$MAX_QUESTION_ROUNDS). Введите ответы:${NC}"
    echo "═══════════════════════════════════════════════════════════════"
    read -r CODER_ANSWERS
    
    if [ -z "$CODER_ANSWERS" ]; then
        CODER_ANSWERS="Действуй по своему усмотрению"
    fi
    
    log "CODER ($CODER_MODEL)" "Отвечаю на вопросы..."
    CODER_RESPONSE=$(run_coder "Ответы на твои вопросы: $CODER_ANSWERS

Продолжай реализацию. Больше вопросов не задавай — сразу делай.")
    log_agent "CODER" "$CODER_MODEL" "answers: $CODER_ANSWERS" "$CODER_RESPONSE"
    echo "$CODER_RESPONSE"
done

CODER_FIX_PROMPT="Исправь замечания от ревьювера:

%REVIEW%

Исправь только CRITICAL и HIGH."

CODER_NEXT_PROMPT="Ревью пройдено. Переходи к следующей итерации.
Если все итерации завершены — напиши 'COMPLETED: ALL_ITERATIONS'."

# Переменная для хранения предыдущего ревью
PREV_REVIEW=""

# Файл для сохранения summaries итераций (для --fresh)
ITERATION_SUMMARIES="$WORKSPACE/iteration_summaries_${SESSION_ID}.md"

# Цикл ревью
while true; do
    if [ "$MAX_ITER" -gt 0 ] && [ $ITER -ge $MAX_ITER ]; then
        echo "END: $(date)" >> "$LOG_FILE"
        echo "STATUS: FAILED (iteration limit $MAX_ITER)" >> "$LOG_FILE"
        error "Лимит итераций ($MAX_ITER)"
        echo "Лог: $LOG_FILE"
        echo "История: $HISTORY_FILE"
        exit 1
    fi
    
    CYCLE_NUM=$((ITER + 1))
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    if [ "$MAX_ITER" -gt 0 ]; then
        log "REVIEW CYCLE" "$CYCLE_NUM / $MAX_ITER"
    else
        log "REVIEW CYCLE" "$CYCLE_NUM"
    fi
    echo "═══════════════════════════════════════════════════════════════"
    
    # Собираем diff для ревьювера
    log "REVIEW" "Собираю diff..."
    DIFF_FILE="$LOG_DIR/diff_${SESSION_ID}_${CYCLE_NUM}.patch"
    GIT_DIFF_STAT=$(git diff --stat 2>/dev/null || echo "No changes")
    git diff > "$DIFF_FILE" 2>/dev/null || echo "No diff" > "$DIFF_FILE"
    DIFF_LINES=$(wc -l < "$DIFF_FILE")
    
    # Проверка пустого diff
    if [ "$DIFF_LINES" -eq 0 ] || [ "$(cat "$DIFF_FILE")" = "No diff" ]; then
        rm -f "$DIFF_FILE"
        warn "Нет изменений! CODER ничего не сделал в этой итерации."
        log "CODER" "Напоминаю продолжить работу..."
        CODER_RESPONSE=$(run_coder "Ты не внёс никаких изменений. Продолжи реализацию текущей итерации. Используй инструменты для редактирования файлов.")
        log_agent "CODER (remind)" "$CODER_MODEL" "no changes reminder" "$CODER_RESPONSE"
        echo "$CODER_RESPONSE"
        continue
    fi
    
    ITER=$CYCLE_NUM
    
    # Если diff большой — включаем только начало + ссылку на файл
    if [ "$DIFF_LINES" -gt "$DIFF_LINES_LIMIT" ]; then
        GIT_DIFF=$(head -$DIFF_LINES_LIMIT "$DIFF_FILE")
        DIFF_NOTE="
⚠️ Diff обрезан (показано $DIFF_LINES_LIMIT из $DIFF_LINES строк). Полный diff: $DIFF_FILE"
    else
        GIT_DIFF=$(cat "$DIFF_FILE")
        DIFF_NOTE=""
    fi
    
    # Запускаем сборку и тесты
    log "BUILD" "Проверяю сборку..."
    BUILD_LOG="$LOG_DIR/build_${SESSION_ID}_${ITER}.log"
    BUILD_STATUS="SKIPPED"
    TEST_STATUS="SKIPPED"
    
    # C++ сборка (если есть CMakeLists.txt)
    if [ -f "CMakeLists.txt" ]; then
        if [ ! -d "build/windows-x64" ]; then
            warn "build/windows-x64 не найден — пытаюсь создать..."
            if cmake -S . -B build/windows-x64 -G "Ninja" > "$BUILD_LOG" 2>&1; then
                log "BUILD" "CMake configure OK"
            else
                warn "CMake configure FAILED — сборка не будет проверена"
                echo "CMake configure failed" >> "$BUILD_LOG"
            fi
        fi
        
        if [ -d "build/windows-x64" ]; then
            BUILD_STATUS="OK"
            if ! cmake --build build/windows-x64 --config Release >> "$BUILD_LOG" 2>&1; then
                BUILD_STATUS="FAILED"
                warn "Сборка упала! См. $BUILD_LOG"
            fi
        fi
    fi
    
    # C++ тесты
    if [ "$BUILD_STATUS" = "OK" ] && [ -d "build/windows-x64" ]; then
        TEST_STATUS="OK"
        log "TEST" "Запускаю C++ тесты..."
        if ! ctest --test-dir build/windows-x64 -C Release --output-on-failure >> "$BUILD_LOG" 2>&1; then
            TEST_STATUS="FAILED"
            warn "C++ тесты упали! См. $BUILD_LOG"
        fi
    fi
    
    # Flutter/Dart проверки
    FLUTTER_STATUS="SKIPPED"
    if [ -f "app/pubspec.yaml" ]; then
        log "FLUTTER" "Проверяю Dart код..."
        FLUTTER_STATUS="OK"
        
        # flutter analyze (игнорируем info)
        if ! (cd app && flutter analyze --no-fatal-infos) >> "$BUILD_LOG" 2>&1; then
            FLUTTER_STATUS="FAILED"
            warn "Flutter analyze упал! См. $BUILD_LOG"
        fi
        
        # flutter test (если есть тесты)
        if [ -d "app/test" ] && [ "$FLUTTER_STATUS" = "OK" ]; then
            log "FLUTTER" "Запускаю Dart тесты..."
            if ! (cd app && flutter test) >> "$BUILD_LOG" 2>&1; then
                FLUTTER_STATUS="FAILED"
                warn "Flutter тесты упали! См. $BUILD_LOG"
            fi
        fi
    fi
    
    # Формируем результат сборки для ревьювера
    if [ "$BUILD_STATUS" = "FAILED" ] || [ "$TEST_STATUS" = "FAILED" ] || [ "$FLUTTER_STATUS" = "FAILED" ]; then
        BUILD_RESULT="
## ❌ Сборка/Тесты

**C++ Build:** $BUILD_STATUS
**C++ Tests:** $TEST_STATUS
**Flutter:** $FLUTTER_STATUS

Последние 50 строк лога:
\`\`\`
$(tail -50 "$BUILD_LOG" 2>/dev/null || echo "No log")
\`\`\`
"
    elif [ "$BUILD_STATUS" = "SKIPPED" ] && [ "$FLUTTER_STATUS" = "SKIPPED" ]; then
        BUILD_RESULT="
## ⚠️ Сборка/Тесты: НЕ ПРОВЕРЕНЫ

CMakeLists.txt и pubspec.yaml не найдены.
"
    else
        BUILD_RESULT="
## ✅ Сборка/Тесты

**C++ Build:** $BUILD_STATUS
**C++ Tests:** $TEST_STATUS
**Flutter:** $FLUTTER_STATUS
"
    fi
    
    # Загружаем роль REVIEWER и список файлов
    REVIEWER_ROLE=$(cat agents/prompts/code_reviewer.md 2>/dev/null || echo "Ты — CODE REVIEWER.")
    CHANGED_FILES=$(git diff --name-only 2>/dev/null | head -10 || echo "")
    
    # Формируем секцию previous review если есть
    PREV_REVIEW_SECTION=""
    if [ -n "$PREV_REVIEW" ]; then
        PREV_REVIEW_SECTION="
## Previous Review (check if fixed)
\`\`\`
$(echo "$PREV_REVIEW" | head -80)
\`\`\`
"
    fi
    
    CODE_REVIEWER_PROMPT="$REVIEWER_ROLE
$BUILD_RESULT
$PREV_REVIEW_SECTION
## Изменённые файлы
$CHANGED_FILES

## Статистика
\`\`\`
$GIT_DIFF_STAT
\`\`\`

## Diff
$DIFF_NOTE
\`\`\`diff
$GIT_DIFF
\`\`\`

Проанализируй изменения. Формат ответа:
- CRITICAL: ...
- HIGH: ...
- WARNING: ...
- Статус: NEEDS_WORK или APPROVED"

    log "CODE_REVIEWER ($CODE_REVIEWER_MODEL)" "Проверяю код..."
    REVIEW=$(run_code_reviewer "$CODE_REVIEWER_PROMPT")
    log_agent "CODE_REVIEWER" "$CODE_REVIEWER_MODEL" "$CODE_REVIEWER_PROMPT" "$REVIEW"
    echo "$REVIEW"
    
    # Сохраняем для следующей итерации
    PREV_REVIEW="$REVIEW"
    
    # Валидация ответа REVIEWER
    if ! echo "$REVIEW" | grep -qiE "(APPROVED|NEEDS_WORK)"; then
        warn "CODE_REVIEWER не вернул валидный статус (APPROVED/NEEDS_WORK)"
        warn "Считаем как NEEDS_WORK"
    fi
    
    # Если сборка упала — всегда NEEDS_WORK
    if [ "$BUILD_STATUS" = "FAILED" ] || [ "$TEST_STATUS" = "FAILED" ] || [ "$FLUTTER_STATUS" = "FAILED" ]; then
         if echo "$REVIEW" | grep -qi "APPROVED"; then
            warn "CODE_REVIEWER одобрил, но сборка/тесты упали! Переопределяю на NEEDS_WORK."
            REVIEW="NEEDS_WORK

### 🚨 Critical Issues
#### CRITICAL-BUILD: Сборка или тесты упали
Смотри лог выше. Исправь ошибки компиляции или тестов.
"
         fi
    fi

    # Проверяем завершение ВСЕХ итераций (структурированный маркер)
    if echo "$CODER_RESPONSE" | grep -qiE "COMPLETED:\s*ALL_ITERATIONS|все итерации завершены|all iterations (complete|done)"; then
        if echo "$REVIEW" | grep -qi "APPROVED"; then
            echo ""
            echo "END: $(date)" >> "$LOG_FILE"
            echo "STATUS: SUCCESS" >> "$LOG_FILE"
            success "ВСЕ ИТЕРАЦИИ ЗАВЕРШЕНЫ!"
            echo "Лог: $LOG_FILE"
            echo "История: $HISTORY_FILE"
            exit 0
        fi
    fi
    
    # APPROVED без критичных — следующая итерация
    if echo "$REVIEW" | grep -qi "APPROVED"; then
        # Проверяем, нет ли случайно CRITICAL в тексте (например, "No CRITICAL issues")
        # Ищем заголовки разделов или маркеры
        if ! echo "$REVIEW" | grep -qE "###.*CRITICAL|####.*CRITICAL|\*\*CRITICAL\*\*"; then
            # Сбрасываем счётчик неудач при успехе
            CONSECUTIVE_REVIEW_FAILURES=0
            
            # Сохраняем summary завершённой итерации
            save_iteration_summary "$ITER" "$CODER_RESPONSE"
            # Очищаем prev review для новой итерации
            PREV_REVIEW=""
            
            # Если --only, завершаем после одной итерации
            if [ -n "$ONLY_ITER" ]; then
                echo "END: $(date)" >> "$LOG_FILE"
                echo "STATUS: SUCCESS" >> "$LOG_FILE"
                success "ИТЕРАЦИЯ $ONLY_ITER ЗАВЕРШЕНА!"
                echo "Лог: $LOG_FILE"
                echo "История: $HISTORY_FILE"
                exit 0
            fi
            
            if [ "$MODE" = "run" ]; then
                log "CODER" "Итерация одобрена → следующая..."
                
                # Новый CODER для новой итерации фичи?
                if [ "$FRESH_CODER" = true ]; then
                    run_fresh_coder
                else
                    CODER_RESPONSE=$(run_coder "$CODER_NEXT_PROMPT")
                    log_agent "CODER" "$CODER_MODEL" "$CODER_NEXT_PROMPT" "$CODER_RESPONSE"
                    echo "$CODER_RESPONSE"
                fi
            else
                echo "END: $(date)" >> "$LOG_FILE"
                echo "STATUS: SUCCESS" >> "$LOG_FILE"
                success "КОД ОДОБРЕН!"
                echo "Лог: $LOG_FILE"
                echo "История: $HISTORY_FILE"
                exit 0
            fi
            continue
        fi
    fi
    
    # Есть проблемы — исправляем
    if echo "$REVIEW" | grep -qE "###.*CRITICAL|####.*CRITICAL|\*\*CRITICAL\*\*|###.*HIGH|####.*HIGH|\*\*HIGH\*\*"; then
        # Защита от бесконечного цикла CODER↔REVIEWER
        CONSECUTIVE_REVIEW_FAILURES=$((CONSECUTIVE_REVIEW_FAILURES + 1))
        if [ $CONSECUTIVE_REVIEW_FAILURES -ge $MAX_CONSECUTIVE_FAILURES ]; then
            echo "END: $(date)" >> "$LOG_FILE"
            echo "STATUS: FAILED (consecutive review failures: $MAX_CONSECUTIVE_FAILURES)" >> "$LOG_FILE"
            error "Слишком много неудачных ревью подряд ($MAX_CONSECUTIVE_FAILURES)!"
            echo "CODER и CODE_REVIEWER не могут договориться. Требуется ручное вмешательство."
            echo "Последнее ревью:"
            echo "$REVIEW" | head -30
            echo ""
            echo "Лог: $LOG_FILE"
            echo "История: $HISTORY_FILE"
            exit 1
        fi
        
        log "CODER" "Исправляю замечания (попытка $CONSECUTIVE_REVIEW_FAILURES/$MAX_CONSECUTIVE_FAILURES)..."
        FIX_PROMPT="Исправь замечания от ревьювера:

$REVIEW

Исправь только CRITICAL и HIGH."
        CODER_RESPONSE=$(run_coder "$FIX_PROMPT")
        log_agent "CODER (fix)" "$CODER_MODEL" "$FIX_PROMPT" "$CODER_RESPONSE"
        echo "$CODER_RESPONSE"
    else
        if [ "$MODE" = "run" ]; then
            warn "Только WARNING → следующая итерация"
            
            # Новый CODER для новой итерации фичи?
            if [ "$FRESH_CODER" = true ]; then
                run_fresh_coder
            else
                CODER_RESPONSE=$(run_coder "$CODER_NEXT_PROMPT")
                log_agent "CODER" "$CODER_MODEL" "$CODER_NEXT_PROMPT" "$CODER_RESPONSE"
                echo "$CODER_RESPONSE"
            fi
        else
            echo "END: $(date)" >> "$LOG_FILE"
            echo "STATUS: SUCCESS (warnings)" >> "$LOG_FILE"
            success "Готово (есть WARNING)"
            echo "Лог: $LOG_FILE"
            echo "История: $HISTORY_FILE"
            exit 0
        fi
    fi
done

echo "END: $(date)" >> "$LOG_FILE"
echo "STATUS: FAILED (loop exit)" >> "$LOG_FILE"
error "Цикл завершился без явной причины"
echo "Лог: $LOG_FILE"
echo "История: $HISTORY_FILE"
exit 1
