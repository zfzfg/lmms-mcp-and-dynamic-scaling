@echo off
setlocal
set SCRIPT_DIR=%~dp0
node "%SCRIPT_DIR%..\dist\src\index.js" %*
endlocal
