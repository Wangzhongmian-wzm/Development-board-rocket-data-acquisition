@echo off
chcp 65001 >nul
title 掌控板数据服务 - 关掉这个窗口服务就停了
echo.
echo   正在启动，启动后浏览器打开  http://localhost:8000
echo   这个窗口不要关，关掉服务就停了。
echo.
"C:\Program Files\Mind+\apps\deps\mind-link\node.exe" "%~dp0server.js"
echo.
echo   服务已停止。
pause
