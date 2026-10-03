@echo off
echo ====================================
echo P2P File Transfer System - Launcher
echo ====================================
echo.

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python is not installed or not in PATH
    echo Please install Python 3.8 or higher
    pause
    exit /b 1
)

REM Check if virtual environment exists
if not exist "venv\Scripts\activate.bat" (
    echo Creating virtual environment...
    python -m venv venv
    echo Installing dependencies...
    call venv\Scripts\activate.bat
    pip install -r requirements.txt
) else (
    echo Activating virtual environment...
    call venv\Scripts\activate.bat
)

echo.
echo Starting P2P File Transfer System...
echo.
echo Step 1: Starting TCP Server (C++)...
start "P2P TCP Server" cmd /k "build\p2p_server.exe"
timeout /t 3 >nul

echo Step 2: Starting Web Server (Python)...
echo.
echo Web Interface will be available at: http://localhost:5050
echo.
python web_server.py

pause
