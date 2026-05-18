param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$DistDir = "..\dist\PitlaneDanmakuLite-WinUI",
    [string]$LiteVcpkgRoot = "",
    [string]$MSBuildPath = "",
    [switch]$SkipBuild,
    [switch]$MakeZip
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $Root "..\..")
$SolutionPath = Join-Path $Root "PitlaneDanmaku.Lite.WinUI.sln"
$DistPath = Join-Path $Root $DistDir
$OutputPath = Join-Path $Root "x64\$Configuration\PitlaneDanmaku.Lite.WinUI"
$ExePath = Join-Path $OutputPath "PitlaneDanmaku.Lite.WinUI.exe"

function Repair-ProcessPath {
    $ProcessPath = [Environment]::GetEnvironmentVariable("Path", "Process")
    if ([string]::IsNullOrEmpty($ProcessPath)) {
        $ProcessPath = [Environment]::GetEnvironmentVariable("PATH", "Process")
    }
    if (-not [string]::IsNullOrEmpty($ProcessPath)) {
        [Environment]::SetEnvironmentVariable("PATH", $null, "Process")
        [Environment]::SetEnvironmentVariable("Path", $ProcessPath, "Process")
    }
}

function Resolve-MSBuild {
    if (-not [string]::IsNullOrWhiteSpace($MSBuildPath)) {
        if (Test-Path $MSBuildPath) {
            return (Resolve-Path $MSBuildPath).Path
        }
        throw "MSBuildPath does not exist: $MSBuildPath"
    }

    $Command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $Command) {
        return $Command.Source
    }

    $DefaultPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $DefaultPath) {
        return $DefaultPath
    }

    throw "MSBuild.exe was not found. Install Visual Studio 2022 C++ and WinUI tools."
}

Repair-ProcessPath

if ([string]::IsNullOrWhiteSpace($LiteVcpkgRoot)) {
    $LiteVcpkgRoot = Join-Path $Root "..\build-vcpkg\vcpkg_installed\x64-windows"
}

if (-not $SkipBuild) {
    $MSBuild = Resolve-MSBuild
    & $MSBuild $SolutionPath /restore /p:Configuration=$Configuration /p:Platform=x64 /p:LiteVcpkgRoot=$LiteVcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        throw "WinUI Lite build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path $ExePath)) {
    throw "Missing WinUI Lite executable: $ExePath"
}

if (Test-Path $DistPath) {
    Remove-Item -LiteralPath $DistPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $DistPath | Out-Null

$ExcludedExtensions = @(".pdb", ".exp", ".lib", ".ilk")
Get-ChildItem -LiteralPath $OutputPath -File |
    Where-Object { $ExcludedExtensions -notcontains $_.Extension.ToLowerInvariant() } |
    Copy-Item -Destination $DistPath -Force

$AssetsSource = Join-Path $RepoRoot "assets"
if (Test-Path $AssetsSource) {
    Copy-Item -LiteralPath $AssetsSource -Destination (Join-Path $DistPath "assets") -Recurse -Force
}

$Readme = @"
Pitlane Danmaku Lite WinUI/C++

Start:
  PitlaneDanmaku.Lite.WinUI.exe

Notes:
  This package is the WinUI/C++ shell and uses the real lite core in windows-lite-native/src.
  Real live-room receiving depends on the bundled zlib and brotli DLLs.
  If Windows App SDK Runtime is not installed, install the matching runtime first.

OBS browser source:
  http://127.0.0.1:17333/overlay

Settings file:
  %LOCALAPPDATA%\PitlaneDanmakuLite\settings.ini
"@

$Utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText((Join-Path $DistPath "README-winui-lite.txt"), $Readme, $Utf8NoBom)

if ($MakeZip) {
    $ZipPath = "$DistPath.zip"
    if (Test-Path $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    Compress-Archive -Path (Join-Path $DistPath "*") -DestinationPath $ZipPath -Force
    Write-Host "WinUI Lite ZIP created: $ZipPath"
}

Write-Host "WinUI Lite portable directory created: $DistPath"
