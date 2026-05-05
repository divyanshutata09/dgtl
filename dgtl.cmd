@echo off
REM DGTL Command Runner Launcher v1.0.0
REM Forward to PowerShell script to avoid WSL/GitBash routing issues natively on Windows.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0dgtl.ps1" %*
exit /b %ERRORLEVEL%

