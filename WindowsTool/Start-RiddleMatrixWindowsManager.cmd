@echo off
set "EXE=%~dp0RiddleMatrixWindowsManager.exe"
if exist "%EXE%" (
    start "" "%EXE%"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-RiddleMatrixWindowsManager.ps1"
)
