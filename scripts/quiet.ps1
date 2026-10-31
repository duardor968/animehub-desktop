# Dot-sourced by build.ps1 and run.ps1. Children inherit the error mode set here, so a missing DLL,
# a crash or a failed assertion ends the program with an exit code and never opens a Windows box.
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt\6.11.0\msvc2022_64" }
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot "bin\qmake.exe"))) {
    throw "Qt 6.11 MSVC was not found. Set QT_ROOT to the kit directory."
}

Add-Type -Namespace AnimeHub -Name ErrorMode -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll")]
public static extern uint SetErrorMode(uint mode);
'@ -ErrorAction SilentlyContinue

function Invoke-Quiet([scriptblock]$Body) {
    # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX
    $previous = [AnimeHub.ErrorMode]::SetErrorMode(0x8003)
    $path = $env:PATH
    $env:PATH = "$(Join-Path $qtRoot 'bin');$path"
    # Qt's own message boxes for command-line errors and debug assertions.
    $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = "1"
    $env:QT_FORCE_STDERR_LOGGING = "1"
    try {
        & $Body
    } finally {
        $env:PATH = $path
        [void][AnimeHub.ErrorMode]::SetErrorMode($previous)
    }
}
