[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt\6.11.0\msvc2022_64" }
$qtToolsRoot = Split-Path (Split-Path $qtRoot -Parent) -Parent
$portableCMakePath = Join-Path $env:USERPROFILE "Tools\cmake-4.4.1-windows-x86_64\bin"
$cmakePath = if (Test-Path -LiteralPath (Join-Path $portableCMakePath "cmake.exe")) {
    $portableCMakePath
} else {
    Join-Path $qtToolsRoot "Tools\CMake_64\bin"
}
$ninjaPath = Join-Path $qtToolsRoot "Tools\Ninja"
$clangToolsPath = Join-Path $qtToolsRoot "Tools\QtCreator\bin\clang\bin"
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $qtRoot)) {
    throw "Qt MSVC 6.11 was not found. Set QT_ROOT to the kit directory."
}

if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$visualStudioPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioPath) {
    throw "Visual Studio 2022 with the MSVC x64 toolchain was not found."
}

$devShellModule = Join-Path $visualStudioPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $visualStudioPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"

$env:QT_ROOT = $qtRoot
$env:PATH = "$cmakePath;$ninjaPath;$clangToolsPath;$(Join-Path $qtRoot 'bin');$env:PATH"

$preset = "windows-$($Configuration.ToLowerInvariant())"
cmake --fresh --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $SkipTests -and $Configuration -eq "Debug") {
    ctest --preset windows-debug
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
