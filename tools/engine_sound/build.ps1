# builds the offline engine sound renderer against the live synthesizer source, or against another copy of it
# (e.g. one taken from git) so an older model can be rendered side by side
param
(
    [string]$Synth = "",
    [string]$Name = "engine_sound_render"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs)
{
    throw "no visual studio with the c++ toolset found"
}
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$bin = Join-Path $root "bin"
$obj = Join-Path $bin $Name
New-Item -ItemType Directory -Force $obj | Out-Null
if (-not $Synth)
{
    $Synth = Join-Path $root "..\..\source\car\CarEngineSoundSynthesis.cpp"
}
$Synth = Resolve-Path $Synth
$include = Resolve-Path (Join-Path $root "..\..\source\car")
$command = "`"$vcvars`" >nul && cl /nologo /std:c++20 /O2 /EHsc /W3 /MP /I`"$root`" /I`"$include`" /Fo`"$obj\\`" /Fe`"$bin\$Name.exe`" `"$root\render.cpp`" `"$Synth`""
cmd /c $command
if ($LASTEXITCODE -ne 0)
{
    throw "build failed"
}
