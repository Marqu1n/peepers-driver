@echo off
REM Configuration script for Peepers Driver Webhook Server
REM Run as Administrator

echo ========================================
echo Peepers Driver Configuration Script
echo ========================================
echo.

REM Check if running as administrator
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script must be run as Administrator!
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

echo Running as Administrator - OK
echo.

:menu
echo Choose configuration method:
echo 1. Set Environment Variables (User-specific)
echo 2. Set Environment Variables (System-wide)
echo 3. Set Registry Values (System-wide)
echo 4. View Current Configuration
echo 5. Remove Configuration
echo 6. Exit
echo.
set /p choice="Enter your choice (1-6): "

if "%choice%"=="1" goto set_user_env
if "%choice%"=="2" goto set_system_env
if "%choice%"=="3" goto set_registry
if "%choice%"=="4" goto view_config
if "%choice%"=="5" goto remove_config
if "%choice%"=="6" goto exit
goto menu

:set_user_env
echo.
echo === Setting User Environment Variables ===
set /p api_url="Enter Remote API URL (e.g., http://your-api.com/api/process-data): "
set /p server_host="Enter Server Host (default: 0.0.0.0): "
set /p server_port="Enter Server Port (default: 8888): "

if "%server_host%"=="" set server_host=0.0.0.0
if "%server_port%"=="" set server_port=8888

setx PEEPERS_API_URL "%api_url%"
if not "%server_host%"=="0.0.0.0" setx PEEPERS_SERVER_HOST "%server_host%"
if not "%server_port%"=="8888" setx PEEPERS_SERVER_PORT "%server_port%"

echo.
echo User environment variables set successfully!
echo You may need to restart the application for changes to take effect.
echo.
pause
goto menu

:set_system_env
echo.
echo === Setting System Environment Variables ===
set /p api_url="Enter Remote API URL (e.g., http://your-api.com/api/process-data): "
set /p server_host="Enter Server Host (default: 0.0.0.0): "
set /p server_port="Enter Server Port (default: 8888): "

if "%server_host%"=="" set server_host=0.0.0.0
if "%server_port%"=="" set server_port=8888

setx PEEPERS_API_URL "%api_url%" /M
if not "%server_host%"=="0.0.0.0" setx PEEPERS_SERVER_HOST "%server_host%" /M
if not "%server_port%"=="8888" setx PEEPERS_SERVER_PORT "%server_port%" /M

echo.
echo System environment variables set successfully!
echo You may need to restart the application for changes to take effect.
echo.
pause
goto menu

:set_registry
echo.
echo === Setting Registry Values ===
set /p api_url="Enter Remote API URL (e.g., http://your-api.com/api/process-data): "
set /p server_host="Enter Server Host (default: 0.0.0.0): "
set /p server_port="Enter Server Port (default: 8888): "

if "%server_host%"=="" set server_host=0.0.0.0
if "%server_port%"=="" set server_port=8888

reg add "HKLM\SOFTWARE\PeepersDriver" /v RemoteApiUrl /t REG_SZ /d "%api_url%" /f
if not "%server_host%"=="0.0.0.0" reg add "HKLM\SOFTWARE\PeepersDriver" /v ServerHost /t REG_SZ /d "%server_host%" /f
if not "%server_port%"=="8888" reg add "HKLM\SOFTWARE\PeepersDriver" /v ServerPort /t REG_DWORD /d %server_port% /f

echo.
echo Registry values set successfully!
echo.
pause
goto menu

:view_config
echo.
echo === Current Configuration ===
echo.
echo Environment Variables:
echo PEEPERS_API_URL = %PEEPERS_API_URL%
echo PEEPERS_SERVER_HOST = %PEEPERS_SERVER_HOST%
echo PEEPERS_SERVER_PORT = %PEEPERS_SERVER_PORT%
echo.
echo Registry Values:
reg query "HKLM\SOFTWARE\PeepersDriver" 2>nul
if %errorLevel% neq 0 (
    echo Registry key not found or no values set.
)
echo.
pause
goto menu

:remove_config
echo.
echo === Remove Configuration ===
echo WARNING: This will remove all Peepers Driver configuration!
set /p confirm="Are you sure? (y/N): "
if /i not "%confirm%"=="y" goto menu

echo Removing environment variables...
reg delete "HKCU\Environment" /v PEEPERS_API_URL /f 2>nul
reg delete "HKCU\Environment" /v PEEPERS_SERVER_HOST /f 2>nul
reg delete "HKCU\Environment" /v PEEPERS_SERVER_PORT /f 2>nul
reg delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PEEPERS_API_URL /f 2>nul
reg delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PEEPERS_SERVER_HOST /f 2>nul
reg delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PEEPERS_SERVER_PORT /f 2>nul

echo Removing registry values...
reg delete "HKLM\SOFTWARE\PeepersDriver" /f 2>nul

echo.
echo Configuration removed successfully!
echo.
pause
goto menu

:exit
echo.
echo Goodbye!
exit /b 0