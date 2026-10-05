@echo off
python "%~dp0tools\cloud_console.py" --latest "%~dp0build\game"
if errorlevel 1 pause
