@echo off
setlocal
rem =====================================================================
rem  Сборка Windows-релиза KuroMoe Veil: vcpkg -> cmake -> windeployqt
rem  -> zip с exe + Qt-плагины + libmpv-2.dll.
rem
rem  Запускать из КОРНЯ репозитория на Windows (MSVC + CMake + vcpkg +
rem  windeployqt). Всё остальное скрипт делает сам.
rem
rem  Переменные (опционально, по умолчанию):
rem    VCPKG_ROOT  - куда стоит vcpkg               (по умолчанию C:\dev\vcpkg)
rem    MPV_ROOT    - dev-сборка libmpv (shinchiro)  (по умолчанию C:\dev\libmpv)
rem =====================================================================

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\dev\vcpkg"
if "%MPV_ROOT%"==""   set "MPV_ROOT=C:\dev\libmpv"

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [ERROR] vcpkg не найден: %VCPKG_ROOT%
    echo         Задай VCPKG_ROOT (например C:\dev\vcpkg).
    exit /b 1
)
if not exist "%MPV_ROOT%\bin\libmpv-2.dll" (
    echo [ERROR] libmpv-2.dll не найден: %MPV_ROOT%\bin\libmpv-2.dll
    echo         Скачай dev-сборку shinchiro и задай MPV_ROOT.
    exit /b 1
)

echo === vcpkg: Qt-модули (манифест vcpkg.json) ===
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows
if errorlevel 1 exit /b 1

echo === CMake ===
cmake -S . -B build_rel ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DMPV_ROOT_HINT="%MPV_ROOT%" ^
    -DMPV_DLL="%MPV_ROOT%\bin\libmpv-2.dll"
if errorlevel 1 exit /b 1

echo === Сборка ===
cmake --build build_rel --config Release --target anime_client_cpp
if errorlevel 1 exit /b 1

set "EXE_DIR=build_rel\Release"
if not exist "%EXE_DIR%\anime_client_cpp.exe" (
    echo [ERROR] exe не найден: %EXE_DIR%\anime_client_cpp.exe
    exit /b 1
)

echo === windeployqt (Qt-плагины и DLL рядом с exe) ===
windeployqt --release --no-translations "%EXE_DIR%\anime_client_cpp.exe"
if errorlevel 1 exit /b 1

rem libmpv-2.dll POST_BUILD уже кладёт рядом с exe, но страхуемся:
if not exist "%EXE_DIR%\libmpv-2.dll" (
    copy /y "%MPV_ROOT%\bin\libmpv-2.dll" "%EXE_DIR%\" || exit /b 1
)

echo === Упаковка ===
set "DIST=dist\KuroMoeVeil"
if exist "%DIST%" rmdir /s /q "%DIST%"
xcopy /e /i /y "%EXE_DIR%" "%DIST%" >nul
if errorlevel 1 exit /b 1

rem Образец конфига без личных данных (прокси пустой):
copy /y packaging\config.ini.example "%DIST%\config.ini.example" >nul 2>&1

if not exist dist mkdir dist
powershell -NoProfile -Command ^
    "Compress-Archive -Path '%DIST%' -DestinationPath 'dist\kuromoe-veil-win64.zip' -Force"
if errorlevel 1 exit /b 1

echo.
echo Готово: dist\kuromoe-veil-win64.zip
echo Распаковать куда угодно и запустить KuroMoeVeil\anime_client_cpp.exe
endlocal
