@echo off
rem Build the editor module (first run or after code changes) and launch the demo as a game window.
rem The engine is found automatically (see Tools\find_ue.ps1); set UE_ROOT to override.
setlocal
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\find_ue.ps1"`) do set UE_ROOT=%%P
if "%UE_ROOT%"=="" (
  echo Unreal Engine 5.8 not found. Set UE_ROOT to the engine folder and run again.
  pause
  exit /b 1
)
set PROJ=%~dp0AudioOcclusion.uproject
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" AudioOcclusionEditor Win64 Development -Project="%PROJ%" -WaitMutex
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -windowed -ResX=1600 -ResY=900
