@echo off
cd /D "%~dp0"
call "Scripts\generate_project_files.bat" vs2019 d3d11
exit