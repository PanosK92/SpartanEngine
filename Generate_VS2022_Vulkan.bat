@echo off
pushd %~dp0
Scripts\win-bash\bash.exe ./Scripts/generate_project_files.sh vs2022 vulkan
popd