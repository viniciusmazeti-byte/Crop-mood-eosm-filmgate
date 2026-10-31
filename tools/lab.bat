@echo off
setlocal
title Lab Bolex
cd /d "%~dp0"
rem Two ways:
rem   - drag one or more .mov (with the .KEL beside them) onto this file -> *_lab.mov beside them
rem   - put the .mov and .KEL in the "entrada" folder and double-click   -> results in "resultado"
rem Needs only Python; numpy and ffmpeg are fetched by lab.py on the first run.

set "PY="
py -3 --version >nul 2>&1 && set "PY=py -3"
if not defined PY (python --version >nul 2>&1 && set "PY=python")
if not defined PY (python3 --version >nul 2>&1 && set "PY=python3")

if not defined PY (
    echo Python nao encontrado - instalando pelo winget...
    winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements
    py -3 --version >nul 2>&1 && set "PY=py -3"
)
if not defined PY (
    echo.
    echo ERRO: nao consegui achar nem instalar o Python.
    echo Instale em https://www.python.org/downloads/ marcando "Add python.exe to PATH"
    echo e abra o lab.bat de novo.
    pause
    exit /b 1
)

%PY% "%~dp0lab.py" %*
echo.
pause
