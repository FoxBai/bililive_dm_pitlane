param(
    [string]$LiteVcpkgRoot = "",
    [string]$MSBuildPath = "",
    [switch]$SkipBuild,
    [switch]$SkipLaunch
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SolutionPath = Join-Path $Root "PitlaneDanmaku.Lite.WinUI.sln"
$SmokeSource = Resolve-Path (Join-Path $Root "..\tests\BilibiliClientParsingSmoke.cpp")
$SmokeOutDir = Join-Path $Root "obj\verify"
$SmokeExe = Join-Path $SmokeOutDir "BilibiliClientParsingSmoke.exe"

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

function Invoke-VcCommand {
    param([string]$CommandLine)

    $Vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $Vcvars)) {
        throw "vcvars64.bat was not found. Cannot run the C++ smoke test."
    }

    & cmd.exe /d /c ('call "' + $Vcvars + '" >nul && ' + $CommandLine)
    if ($LASTEXITCODE -ne 0) {
        throw "C++ command failed with exit code $LASTEXITCODE."
    }
}

Repair-ProcessPath

if ([string]::IsNullOrWhiteSpace($LiteVcpkgRoot)) {
    $LiteVcpkgRoot = Join-Path $Root "..\build-vcpkg\vcpkg_installed\x64-windows"
}

New-Item -ItemType Directory -Force -Path $SmokeOutDir | Out-Null
$CoreInclude = Resolve-Path (Join-Path $Root "..\src")
$ClCommand = 'cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /I "' + $CoreInclude.Path + '" /Fo:"' + $SmokeOutDir + '\\" /Fe:"' + $SmokeExe + '" "' + $SmokeSource.Path + '" winhttp.lib bcrypt.lib ws2_32.lib'
Invoke-VcCommand -CommandLine $ClCommand
& $SmokeExe
if ($LASTEXITCODE -ne 0) {
    throw "BilibiliClient parsing smoke test failed."
}

if (-not $SkipBuild) {
    $MSBuild = Resolve-MSBuild
    foreach ($Configuration in @("Debug", "Release")) {
        & $MSBuild $SolutionPath /restore /p:Configuration=$Configuration /p:Platform=x64 /p:LiteVcpkgRoot=$LiteVcpkgRoot
        if ($LASTEXITCODE -ne 0) {
            throw "WinUI Lite $Configuration build failed with exit code $LASTEXITCODE."
        }
    }
}

$ReleaseOut = Join-Path $Root "x64\Release\PitlaneDanmaku.Lite.WinUI"
$DebugOut = Join-Path $Root "x64\Debug\PitlaneDanmaku.Lite.WinUI"
$ReleaseExe = Join-Path $ReleaseOut "PitlaneDanmaku.Lite.WinUI.exe"

foreach ($Path in @(
    $ReleaseExe,
    (Join-Path $ReleaseOut "brotlicommon.dll"),
    (Join-Path $ReleaseOut "brotlidec.dll"),
    (Join-Path $ReleaseOut "z.dll"),
    (Join-Path $DebugOut "brotlicommon.dll"),
    (Join-Path $DebugOut "brotlidec.dll"),
    (Join-Path $DebugOut "zd.dll")
)) {
    if (-not (Test-Path $Path)) {
        throw "Missing verification file: $Path"
    }
}

if (-not $SkipLaunch) {
    $Process = Start-Process -FilePath $ReleaseExe -WorkingDirectory $ReleaseOut -PassThru
    try {
        Start-Sleep -Seconds 4
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "WinUI Lite exited early with code $($Process.ExitCode)."
        }
        if ([string]::IsNullOrWhiteSpace($Process.MainWindowTitle) -or $Process.MainWindowTitle -notlike "*Pitlane Danmaku Lite*") {
            throw "WinUI Lite started, but the expected window title was not detected."
        }
        Write-Host "WinUI Lite launch check passed: $($Process.MainWindowTitle)"
    }
    finally {
        if (-not $Process.HasExited) {
            $null = $Process.CloseMainWindow()
            if (-not $Process.WaitForExit(5000)) {
                $Process.Kill()
                $Process.WaitForExit()
            }
        }
    }
}

Write-Host "WinUI Lite local verification passed."
