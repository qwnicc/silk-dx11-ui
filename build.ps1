# Configures and builds Silk with the CMake bundled in Visual Studio.
# Usage: ./build.ps1 [-Config Release|Debug]
param([string]$Config = "Release")


$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $cmake) { $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }

if (-not (Test-Path "$PSScriptRoot\build\CMakeCache.txt")) {
    & $cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -A x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $cmake --build "$PSScriptRoot\build" --config $Config --parallel
exit $LASTEXITCODE
