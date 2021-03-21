@echo off
cd /D "%~dp0"
Scripts\win-bash\bash.exe ./Scripts/generate_project_files.sh vs2019 vulkan
exit