@echo off
setlocal
"%~dp0bin\node.exe" "%~dp0dist\src\index.js" %*
endlocal
