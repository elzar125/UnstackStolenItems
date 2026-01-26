@echo off
echo Setting up SKSE Plugin Template...
echo.

REM Check if git is available
git --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git is not installed or not in PATH
    echo Please install Git and try again
    pause
    exit /b 1
)

REM Initialize and update submodules
echo Adding CommonLibVR submodule...
if not exist "extern" mkdir extern

REM Remove existing submodule if it exists (for clean setup)
if exist "extern\CommonLibVR" (
    echo Removing existing CommonLibVR submodule...
    rmdir /s /q "extern\CommonLibVR"
)

REM Add the CommonLibVR submodule
git submodule add -b ng https://github.com/alandtse/CommonLibVR.git extern/CommonLibVR 2>nul
if errorlevel 1 (
    echo Submodule already exists, updating instead...
)

echo Updating submodules...
git submodule update --init --recursive

REM Clean build cache if it exists
if exist "build" (
    echo Cleaning existing build cache...
    rmdir /s /q "build"
)

echo.
echo Setup complete! You can now:
echo 1. Open the project in Visual Studio 2022, VS Code, or CLion
echo 2. Configure your environment variables (SKYRIM_FOLDER or SKYRIM_MODS_FOLDER)
echo 3. Build the project
echo.
echo Press any key to continue...
pause >nul
