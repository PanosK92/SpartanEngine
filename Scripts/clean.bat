@echo off

@RD /S /Q "Binaries\Intermediate"
del	"Binaries\Release\Runtime.lib"
del	"Binaries\Release\Editor.lib"
del	"Binaries\Release\Editor.exp"

del	"Binaries\Debug\Editor.exp"
del	"Binaries\Debug\Editor.ilk"
del	"Binaries\Debug\Editor.lib"
del	"Binaries\Debug\Editor.pdb"
del	"Binaries\Debug\Runtime.lib"

exit /b