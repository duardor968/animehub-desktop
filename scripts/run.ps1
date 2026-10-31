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
. (Join-Path $PSScriptRoot "quiet.ps1")

$exitCode = Invoke-Quiet { & $Program @Arguments | Out-Host; $LASTEXITCODE }
exit $exitCode
