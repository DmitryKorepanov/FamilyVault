@echo off

rem Получаем путь к директории, где лежит батник
set "SCRIPT_DIR=%~dp0"

rem Переходим в корень проекта
pushd "%SCRIPT_DIR%"

rem Запускаем настройку окружения VS
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" || goto :do_exit

echo Environment setup complete!
echo Current directory: %CD%

rem Запускаем Cursor в текущей директории
rem Если команда 'Cursor' не найдена, убедитесь, что она добавлена в PATH при установке
call Cursor .

:do_exit
popd
if %ERRORLEVEL% neq 0 pause
exit /b %ERRORLEVEL%


