[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    # Reconfigure from scratch instead of reusing the CMake cache.
    [switch]$Fresh,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$repository = Split-Path $PSScriptRoot -Parent

. (Join-Path $PSScriptRoot "quiet.ps1")
$qtToolsRoot = Split-Path (Split-Path $qtRoot -Parent) -Parent

# CMake 4.0+ is required; the one bundled with Qt is older. Prefer the one on PATH, then a
# portable copy under %USERPROFILE%\Tools.
function Find-CMake {
    $candidates = @()
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    $candidates += Get-ChildItem -Path (Join-Path $env:USERPROFILE "Tools") -Filter "cmake.exe" `
        -Recurse -Depth 3 -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | ForEach-Object FullName
    foreach ($candidate in $candidates) {
        $version = (& $candidate --version | Select-Object -First 1) -replace '^cmake version ', ''
        if ([version]($version -replace '-.*$', '') -ge [version]"4.0") { return $candidate }
    }
    throw "CMake 4.0 or newer was not found on PATH or under $env:USERPROFILE\Tools."
}
$cmake = Find-CMake

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw "Visual Studio 2022 with the MSVC x64 toolchain was not found."
}
# VsDevCmd calls vswhere by name.
$env:PATH = "$(Split-Path $vswhere -Parent);$env:PATH"
Import-Module (Join-Path $visualStudio "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

# Only this process sees these tools; the global PATH stays untouched.
$env:QT_ROOT = $qtRoot
$env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = "1"
$env:PATH = @(
    (Split-Path $cmake -Parent),
    (Join-Path $qtToolsRoot "Tools\Ninja"),
    (Join-Path $qtToolsRoot "Tools\QtCreator\bin\clang\bin"),
    (Join-Path $qtRoot "bin"),
    $env:PATH
) -join ';'

Push-Location $repository
try {
    $preset = "windows-$($Configuration.ToLowerInvariant())"
    $configure = @("--preset", $preset)
    if ($Fresh) { $configure = @("--fresh") + $configure }
    & $cmake @configure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not $SkipTests -and $Configuration -eq "Debug") {
        $failed = Invoke-Quiet { ctest --preset windows-debug | Out-Host; $LASTEXITCODE }
        if ($failed -ne 0) { exit $failed }
    }
} finally {
    Pop-Location
}
