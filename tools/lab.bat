@echo off
rem Drag one or more exported .mov files (with their .KEL next to them) onto this file.
rem ffmpeg.exe and ffprobe.exe can sit in this same folder instead of being on the PATH.
cd /d "%~dp0"
python "%~dp0lab.py" %*
pause
