# Runs a built AnimeHub executable (the app or a test) the way a test run must: Qt's bin on the
# PATH of this process only, and the Windows error mode set so a missing DLL or a crash fails
# with an exit code instead of a box on the desktop. Arguments after the path go to the program.
#   .\scripts\run.ps1 out\build\windows-debug\AnimeHub.exe --data-dir <profile> --smoke-screenshot x.png
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Program,
    [Parameter(ValueFromRemainingArguments)][string[]]$Arguments
)

$ErrorActionPreference = "Stop"
$Program = (Get-Command $Program -CommandType Application -ErrorAction Stop).Source
$programDir = Split-Path -Parent $Program
$deployed = Test-Path -LiteralPath (Join-Path $programDir 'Qt6Core.dll') -PathType Leaf
. (Join-Path $PSScriptRoot "quiet.ps1") -BundledQt:$deployed
# A deployed Release carries its own Qt. Its platform plugins can differ from the SDK's.
$platform = $env:QT_QPA_PLATFORM
$platformPaths = $env:QT_QPA_PLATFORM_PLUGIN_PATH
for ($i = 0; $i -lt $Arguments.Count - 1; $i++) {
    if ($Arguments[$i] -in '-platform', '--platform') { $platform = $Arguments[$i + 1] }
    if ($Arguments[$i] -in '-platformpluginpath', '--platformpluginpath') { $platformPaths = $Arguments[$i + 1] }
}
if ($platform -match '^offscreen(:[^;]*)?$' -and $deployed) {
    $pluginDirs = @((Join-Path $programDir 'platforms')) + @($platformPaths -split ';' | Where-Object { $_ })
    foreach ($pluginRoot in ($env:QT_PLUGIN_PATH -split ';' | Where-Object { $_ })) {
        $pluginDirs += Join-Path $pluginRoot 'platforms'
    }
    if (-not ($pluginDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'qoffscreen.dll') -PathType Leaf })) {
        throw "This Release has no offscreen platform plugin. For --version use its default Windows platform. For a capture use --smoke-hidden-native on Windows, or the Debug build with the Qt SDK."
    }
}

$exitCode = Invoke-Quiet { & $Program @Arguments | Out-Host; $LASTEXITCODE }
exit $exitCode
