# Packs the Release build into what a release publishes, in out\package:
#   AnimeHub-<version>-win64.zip   the app folder, to run from anywhere;
#   AnimeHub-<version>-setup.exe   its installer for the current user (Inno Setup 6);
#   SHA256SUMS.txt                 checksums of both.
# Run after .\scripts\build.ps1 -Configuration Release.
[CmdletBinding()]
param(
    # Starts the packaged app with a throwaway library, off the visible screen, and checks that
    # its window opens.
    [switch]$Smoke,
    # Inno Setup's command-line compiler; looked for on PATH and in its usual folders otherwise.
    [string]$Iscc
)

$ErrorActionPreference = "Stop"
$repository = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repository "out\build\windows-release"
$out = Join-Path $repository "out\package"
$stage = Join-Path $out "AnimeHub"

$version = (Select-String -Path (Join-Path $repository "CMakeLists.txt") `
    -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
if (-not (Test-Path -LiteralPath (Join-Path $build "AnimeHub.exe"))) {
    throw "There is no Release build: run .\scripts\build.ps1 -Configuration Release first."
}

# Only what runs: the build folder also holds CMake's and the compiler's files.
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $build "AnimeHub.exe") -Destination $stage
Copy-Item -Path (Join-Path $build "*.dll") -Destination $stage
foreach ($folder in "engines", "iconengines", "imageformats", "platforms", "qml", "sqldrivers",
                    "styles", "tls") {
    Copy-Item -LiteralPath (Join-Path $build $folder) -Destination $stage -Recurse
}
Copy-Item -LiteralPath (Join-Path $repository "LICENSE") -Destination (Join-Path $stage "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repository "packaging\THIRD-PARTY.txt") -Destination $stage
$qtLicenses = New-Item -ItemType Directory -Path (Join-Path $stage "licenses\Qt")
Copy-Item -LiteralPath (Join-Path $repository "packaging\licenses\LGPL-3.0.txt") -Destination $qtLicenses

if ($Smoke) {
    Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SmokeWindow {
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
}
"@
    $data = Join-Path $out "smoke-library"
    $log = Join-Path $out "smoke-stderr.log"
    $savedPath = $env:PATH
    try {
        # No Qt on PATH and an API that answers nothing: the folder must run on its own and
        # download nothing.
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $env:ANIMEHUB_API_URL = "http://127.0.0.1:9/api/v1/"
        $env:QT_FORCE_STDERR_LOGGING = "1"
        $app = Start-Process -FilePath (Join-Path $stage "AnimeHub.exe") `
            -ArgumentList "--data-dir", "`"$data`"" -PassThru -RedirectStandardError $log
    } finally {
        $env:PATH = $savedPath
        Remove-Item Env:ANIMEHUB_API_URL, Env:QT_FORCE_STDERR_LOGGING -ErrorAction SilentlyContinue
    }
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 200
        $app.Refresh()
    } until ($app.MainWindowHandle -ne 0 -or $app.HasExited -or (Get-Date) -gt $deadline)
    if ($app.HasExited -or $app.MainWindowHandle -eq 0) {
        Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Select-Object -Last 30
        if (-not $app.HasExited) { Stop-Process -Id $app.Id }
        throw "The packaged AnimeHub did not open its window."
    }
    [void][SmokeWindow]::MoveWindow($app.MainWindowHandle, -4000, 100, 1280, 800, $true)
    Start-Sleep -Seconds 3
    $app.Refresh()
    if ($app.HasExited) { throw "The packaged AnimeHub closed right after opening." }
    Stop-Process -Id $app.Id
    $app.WaitForExit(10000) | Out-Null
    Remove-Item -LiteralPath $data, $log -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "The packaged AnimeHub opened its window."
}

$zip = Join-Path $out "AnimeHub-$version-win64.zip"
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

if (-not $Iscc) {
    $Iscc = @(
        (Get-Command iscc -ErrorAction SilentlyContinue).Source,
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $Iscc) { throw "Inno Setup 6 was not found: install it or pass -Iscc." }
}
& $Iscc /Q "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutputDir=$out" `
    (Join-Path $repository "installer\AnimeHub.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }
$setup = Join-Path $out "AnimeHub-$version-setup.exe"

$sums = foreach ($file in $zip, $setup) {
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant(),
        (Split-Path $file -Leaf)
}
Set-Content -LiteralPath (Join-Path $out "SHA256SUMS.txt") -Value $sums -Encoding ascii
Get-Item $zip, $setup | ForEach-Object { "{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
