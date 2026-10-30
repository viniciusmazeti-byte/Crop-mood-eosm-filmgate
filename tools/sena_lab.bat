@echo off
rem Drag one or more exported .mov files (with their .KEL next to them) onto this file.
python "%~dp0sena_lab.py" %*
pause
