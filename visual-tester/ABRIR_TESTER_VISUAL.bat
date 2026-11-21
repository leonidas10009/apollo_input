@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

set "PY=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
if not exist "%PY%" set "PY=python"

echo Iniciando ApolloInput Nova Visual Tester...
echo Se abrira el navegador en http://127.0.0.1:8765/
echo Cierra esta ventana para detener el servidor.
echo.

"%PY%" server.py
if errorlevel 1 (
    echo.
    echo El servidor termino con error.
    pause
)
endlocal
