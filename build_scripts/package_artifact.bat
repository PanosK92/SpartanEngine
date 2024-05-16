@echo off

:: package everything for the full release
build_scripts\7z.exe a build_%1.7z .\binaries\dxcompiler.dll .\binaries\fmod.dll .\binaries\data .\binaries\project .\binaries\spartan_%1.exe

:: package binaries only for a binaries-only release
build_scripts\7z.exe a binaries_only_%1.7z .\binaries\dxcompiler.dll .\binaries\fmod.dll .\binaries\data .\binaries\spartan_%1.exe
