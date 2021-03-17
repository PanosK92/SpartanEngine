@echo off
cd /D "%~dp0"
call "Scripts\generate_project_files.bat" vs2019 vulkan
exit