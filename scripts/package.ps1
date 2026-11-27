# Packs the Release build into what a release publishes, in out\package:
#   AnimeHub-<version>-win64.zip     the app folder, to run from anywhere;
#   AnimeHub-<version>-setup.exe     its installer for the current user (Inno Setup 6.6+);
#   AnimeHub-<version>-win64.ahpack  optional in-app updater package (-BuildUpdatePack);
#   update-win64.json                optional signed update manifest (when signing key is set);
#   SHA256SUMS.txt                   checksums of the generated release files.
# Run after .\scripts\build.ps1 -Configuration Release.
[CmdletBinding()]
param(
    # Starts the packaged app with a throwaway library, off the visible screen, and checks that
    # its window opens.
    [switch]$Smoke,
    # Inno Setup's command-line compiler; looked for on PATH and in its usual folders otherwise.
    [string]$Iscc,
    # Builds AnimeHub-<version>-win64.ahpack via scripts/update-tool.py pack-win64.
    [switch]$BuildUpdatePack,
    # Optional Ed25519 signing key file (or value) to also emit a signed update-win64.json.
    # When omitted, UPDATER_SIGNING_KEY from the environment is used if -BuildUpdatePack is set.
    [string]$SigningKey,
    # Base download URL embedded in update-win64.json (defaults to the GitHub Release tag URL).
    [string]$BaseUrl
)

$ErrorActionPreference = "Stop"
$repository = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repository "out\build\windows-release"
$out = Join-Path $repository "out\package"
$stage = Join-Path $out "AnimeHub"

function Copy-OptionalLicense {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$StageRoot
    )
    $licenseDir = Join-Path $RepoRoot "packaging\licenses\$Name"
    $destDir = Join-Path $StageRoot "licenses\$Name"
    if (Test-Path -LiteralPath $licenseDir -PathType Container) {
        Copy-Item -LiteralPath $licenseDir -Destination $destDir -Recurse -Force
        return
    }
    $candidates = @()
    $packagingMatches = Get-ChildItem -Path (Join-Path $RepoRoot "packaging\licenses") `
        -Filter "$Name*" -File -ErrorAction SilentlyContinue
    if ($packagingMatches) { $candidates += $packagingMatches }
    if ($candidates.Count -eq 0) {
        $thirdPartyDir = Join-Path $RepoRoot ("third_party\" + $Name.ToLowerInvariant())
        if (Test-Path -LiteralPath $thirdPartyDir -PathType Container) {
            $candidates += Get-ChildItem -Path $thirdPartyDir -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING)' }
        }
    }
    if ($candidates.Count -gt 0) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        foreach ($file in $candidates) {
            Copy-Item -LiteralPath $file.FullName -Destination $destDir -Force
        }
    }
}

$version = (Select-String -Path (Join-Path $repository "CMakeLists.txt") `
    -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
if (-not (Test-Path -LiteralPath (Join-Path $build "AnimeHub.exe"))) {
    throw "There is no Release build: run .\scripts\build.ps1 -Configuration Release first."
}

# Only what runs: the build folder also holds CMake's and the compiler's files.
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $build "AnimeHub.exe") -Destination $stage
$updaterExe = Join-Path $build "AnimeHubUpdater.exe"
if (Test-Path -LiteralPath $updaterExe) {
    Copy-Item -LiteralPath $updaterExe -Destination $stage
}
Copy-Item -Path (Join-Path $build "*.dll") -Destination $stage
foreach ($folder in "engines", "iconengines", "imageformats", "platforms", "qml", "sqldrivers",
                    "styles", "tls") {
    Copy-Item -LiteralPath (Join-Path $build $folder) -Destination $stage -Recurse
}
# Ensure engines\.pins stays in the staged folder so Inno Setup's NeedsMpvUpdate can detect
# whether mpv actually changed between versions.
$enginePins = Join-Path $build "engines\.pins"
if (Test-Path -LiteralPath $enginePins) {
    Copy-Item -LiteralPath $enginePins -Destination (Join-Path $stage "engines\.pins") -Force
}

Copy-Item -LiteralPath (Join-Path $repository "LICENSE") -Destination (Join-Path $stage "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repository "packaging\THIRD-PARTY.txt") -Destination $stage
$qtLicenses = New-Item -ItemType Directory -Path (Join-Path $stage "licenses\Qt")
Copy-Item -LiteralPath (Join-Path $repository "packaging\licenses\LGPL-3.0.txt") -Destination $qtLicenses
Copy-OptionalLicense -Name "Monocypher" -RepoRoot $repository -StageRoot $stage
Copy-OptionalLicense -Name "Zstd" -RepoRoot $repository -StageRoot $stage

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

$artifacts = [System.Collections.Generic.List[string]]@()
$pack = $null

# Build the update pack BEFORE zip/ISCC so installed-manifest.json written into $stage is
# included in both the portable zip and setup.exe for instant mtime diffing on first update.
if ($BuildUpdatePack -or -not [string]::IsNullOrWhiteSpace($SigningKey)) {
    $updateTool = Join-Path $repository "scripts\update-tool.py"
    if (-not (Test-Path -LiteralPath $updateTool)) {
        throw "scripts\update-tool.py was not found; cannot build the update pack."
    }
    $python = @(
        (Get-Command python -ErrorAction SilentlyContinue).Source,
        (Get-Command python3 -ErrorAction SilentlyContinue).Source
    ) | Where-Object { $_ } | Select-Object -First 1
    if (-not $python) {
        throw "Python was not found on PATH; cannot run scripts\update-tool.py."
    }

    $pack = Join-Path $out "AnimeHub-$version-win64.ahpack"
    & $python $updateTool pack-win64 --version $version --source $stage --output $pack
    if ($LASTEXITCODE -ne 0) {
        throw "scripts\update-tool.py pack-win64 failed with exit code $LASTEXITCODE."
    }
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

$artifacts.Add($zip)
$artifacts.Add($setup)

if ($pack) {
    $artifacts.Add($pack)

    $savedKey = $env:UPDATER_SIGNING_KEY
    $hasKey = $false
    try {
        if (-not [string]::IsNullOrWhiteSpace($SigningKey)) {
            if (Test-Path -LiteralPath $SigningKey -PathType Leaf) {
                $env:UPDATER_SIGNING_KEY = (Get-Content -LiteralPath $SigningKey -Raw).Trim()
            } else {
                $env:UPDATER_SIGNING_KEY = $SigningKey.Trim()
            }
        }
        $hasKey = -not [string]::IsNullOrWhiteSpace($env:UPDATER_SIGNING_KEY)
        if ($hasKey) {
            $manifest = Join-Path $out "update-win64.json"
            $manifestBaseUrl = if ([string]::IsNullOrWhiteSpace($BaseUrl)) {
                "https://github.com/duardor968/animehub-desktop/releases/download/v$version"
            } else {
                $BaseUrl.TrimEnd('/')
            }
            & $python $updateTool sign-manifest `
                --platform win64 `
                --version $version `
                --pack $pack `
                --installer $setup `
                --base-url $manifestBaseUrl `
                --output $manifest
            if ($LASTEXITCODE -ne 0) {
                throw "scripts\update-tool.py sign-manifest failed with exit code $LASTEXITCODE."
            }
            $artifacts.Add($manifest)
        }
    } finally {
        if ($null -eq $savedKey) {
            Remove-Item Env:UPDATER_SIGNING_KEY -ErrorAction SilentlyContinue
        } else {
            $env:UPDATER_SIGNING_KEY = $savedKey
        }
    }
}

$sums = foreach ($file in $artifacts) {
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant(),
        (Split-Path $file -Leaf)
}
Set-Content -LiteralPath (Join-Path $out "SHA256SUMS.txt") -Value $sums -Encoding ascii
Get-Item $artifacts | ForEach-Object { "{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
