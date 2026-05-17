param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-vs",
    [string]$DistDir = "dist\PitlaneDanmakuLite",
    [switch]$SkipBuild,
    [switch]$MakeInstaller
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $Root "..")
$BuildPath = Join-Path $Root $BuildDir
$DistPath = Join-Path $Root $DistDir
$ExePath = Join-Path $BuildPath "pitlane_lite.exe"
$InstallerDir = Join-Path $Root "installer"
$InstallerOutput = Join-Path $InstallerDir "output"

function New-IExpressInstaller {
    param(
        [string]$OutputDir
    )

    $IExpress = Get-Command iexpress.exe -ErrorAction SilentlyContinue
    if ($null -eq $IExpress) {
        throw "Neither ISCC.exe nor iexpress.exe was found. Cannot create installer."
    }

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $PayloadZip = Join-Path $OutputDir "payload.zip"
    $SedPath = Join-Path $OutputDir "pitlane-lite-iexpress.sed"
    $SetupPath = Join-Path $OutputDir "PitlaneDanmakuLite-Lite-Setup.exe"

    if (Test-Path $PayloadZip) {
        Remove-Item -LiteralPath $PayloadZip -Force
    }
    Compress-Archive -Path (Join-Path $DistPath "*") -DestinationPath $PayloadZip -Force

    $InstallScript = Join-Path $InstallerDir "install-lite.ps1"
    if (-not (Test-Path $InstallScript)) {
        throw "Missing installer helper: $InstallScript"
    }

    $Sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=1
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=Pitlane Danmaku Lite setup finished.
TargetName=$SetupPath
FriendlyName=Pitlane Danmaku Lite
AppLaunched=powershell.exe -NoProfile -ExecutionPolicy Bypass -File install-lite.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0="install-lite.ps1"
FILE1="payload.zip"
[SourceFiles]
SourceFiles0=$InstallerDir
SourceFiles1=$OutputDir
[SourceFiles0]
%FILE0%=
[SourceFiles1]
%FILE1%=
"@
    Set-Content -LiteralPath $SedPath -Value $Sed -Encoding ASCII
    & $IExpress.Source /N /Q $SedPath

    if (-not (Test-Path $SetupPath)) {
        throw "IExpress did not create $SetupPath"
    }

    Write-Host "Lite installer created: $SetupPath"
}

function New-SevenZipInstaller {
    param(
        [string]$OutputDir
    )

    $SevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($null -eq $SevenZip) {
        return $false
    }

    $SevenZipDir = Split-Path -Parent $SevenZip.Source
    $SfxModule = Join-Path $SevenZipDir "7z.sfx"
    if (-not (Test-Path $SfxModule)) {
        return $false
    }

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $PayloadZip = Join-Path $OutputDir "payload.zip"
    $ArchivePath = Join-Path $OutputDir "pitlane-lite-setup.7z"
    $ConfigPath = Join-Path $OutputDir "pitlane-lite-sfx-config.txt"
    $SetupPath = Join-Path $OutputDir "PitlaneDanmakuLite-Lite-Setup.exe"
    $InstallScript = Join-Path $InstallerDir "install-lite.ps1"
    $PackedInstallScript = Join-Path $OutputDir "install-lite.ps1"

    if (Test-Path $PayloadZip) {
        Remove-Item -LiteralPath $PayloadZip -Force
    }
    if (Test-Path $ArchivePath) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }
    if (Test-Path $SetupPath) {
        Remove-Item -LiteralPath $SetupPath -Force
    }

    Compress-Archive -Path (Join-Path $DistPath "*") -DestinationPath $PayloadZip -Force
    Copy-Item -LiteralPath $InstallScript -Destination $PackedInstallScript -Force

    Push-Location $OutputDir
    try {
        & $SevenZip.Source a -t7z (Split-Path -Leaf $ArchivePath) "install-lite.ps1" "payload.zip" | Out-Null
    }
    finally {
        Pop-Location
    }

    $Config = @"
;!@Install@!UTF-8!
Title="Pitlane Danmaku Lite Setup"
RunProgram="powershell.exe -NoProfile -ExecutionPolicy Bypass -File install-lite.ps1"
;!@InstallEnd@!
"@
    $Utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($ConfigPath, $Config, $Utf8NoBom)

    $Bytes = New-Object System.Collections.Generic.List[byte]
    $Bytes.AddRange([System.IO.File]::ReadAllBytes($SfxModule))
    $Bytes.AddRange([System.IO.File]::ReadAllBytes($ConfigPath))
    $Bytes.AddRange([System.IO.File]::ReadAllBytes($ArchivePath))
    [System.IO.File]::WriteAllBytes($SetupPath, $Bytes.ToArray())

    if (-not (Test-Path $SetupPath)) {
        throw "7-Zip SFX did not create $SetupPath"
    }

    Write-Host "Lite installer created: $SetupPath"
    return $true
}

if (-not $SkipBuild) {
    cmake -S $Root -B $BuildPath -G Ninja -DCMAKE_BUILD_TYPE=$Configuration
    cmake --build $BuildPath --config $Configuration
}

if (-not (Test-Path $ExePath)) {
    throw "Missing $ExePath. Build the lite app first."
}

if (Test-Path $DistPath) {
    Remove-Item -LiteralPath $DistPath -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $DistPath | Out-Null
Copy-Item -LiteralPath $ExePath -Destination (Join-Path $DistPath "pitlane_lite.exe") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "assets") -Destination (Join-Path $DistPath "assets") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $Root "README.md") -Destination (Join-Path $DistPath "README-lite.md") -Force

$LicensePath = Join-Path $RepoRoot "LICENSE.txt"
if (Test-Path $LicensePath) {
    Copy-Item -LiteralPath $LicensePath -Destination (Join-Path $DistPath "LICENSE.txt") -Force
}

Write-Host "Lite portable directory created: $DistPath"

if ($MakeInstaller) {
    New-Item -ItemType Directory -Force -Path $InstallerOutput | Out-Null
    $Iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($null -ne $Iscc) {
        & $Iscc.Source (Join-Path $Root "installer\PitlaneDanmakuLite.iss")
    }
    elseif (New-SevenZipInstaller -OutputDir $InstallerOutput) {
        return
    }
    else {
        New-IExpressInstaller -OutputDir $InstallerOutput
    }
}
