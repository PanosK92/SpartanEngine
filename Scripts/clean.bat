@echo off

:: Set script directory as the working directory
cd /D "%~dp0"

@RD /S /Q "Binaries\Intermediate"
del	"..\Binaries\Release\Runtime.lib"
del	"..\Binaries\Release\Editor.lib"
del	"..\Binaries\Release\Editor.exp"
del	"..\Binaries\Release\Editor.pdb"
del	"..\Binaries\Debug\Runtime.lib"
del	"..\Binaries\Debug\Runtime.pdb"
del	"..\Binaries\Debug\Editor.lib"
del	"..\Binaries\Debug\Editor.exp"
del	"..\Binaries\Debug\Editor.pdb"
del	"..\Binaries\Debug\Editor.ilk"

exit /b