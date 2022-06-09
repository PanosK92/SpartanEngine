@echo off
cd /D "%~dp0"
build_scripts\win-bash\bash.exe build_scripts/generate_project_files.sh vs2022 d3d11
exit