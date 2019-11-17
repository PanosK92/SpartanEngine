@echo off

@RD /S /Q "Binaries\intermediate"
del	/S /Q "Binaries\*.exp
del	/S /Q "Binaries\*.ilk
del	/S /Q "Binaries\*.lib
del	/S /Q "Binaries\*.pdb

exit /b