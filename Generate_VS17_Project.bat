@echo off
cd Runtime
xcopy "Assets" "..\Binaries\Release\Assets\Standard Assets\" /E /I
premake5 vs2017
pause