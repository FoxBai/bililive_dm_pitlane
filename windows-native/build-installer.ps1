param(
    [string]$RuntimeIdentifier = "win-x64"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$appProject = Join-Path $scriptRoot "PitlaneDanmaku.Windows\PitlaneDanmaku.Windows.csproj"
$installerProject = Join-Path $scriptRoot "PitlaneDanmaku.Installer\PitlaneDanmaku.Installer.csproj"
$payloadPath = Join-Path $scriptRoot "PitlaneDanmaku.Installer\Resources\payload.zip"
$publishDir = Join-Path $repoRoot "BundleArtifacts\publish\PitlaneDanmaku.Windows-$RuntimeIdentifier"
$installerPublishDir = Join-Path $repoRoot "BundleArtifacts\installer"

[xml]$appProjectXml = Get-Content -Path $appProject
$version = $appProjectXml.Project.PropertyGroup.Version
if ([string]::IsNullOrWhiteSpace($version)) {
    throw "Unable to read app version from $appProject"
}

Remove-Item -LiteralPath $publishDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $installerPublishDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $publishDir | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $payloadPath) -Force | Out-Null

& dotnet publish $appProject `
    -c Release `
    -r $RuntimeIdentifier `
    --self-contained true `
    -o $publishDir `
    /p:DebugType=none `
    /p:DebugSymbols=false
if ($LASTEXITCODE -ne 0) {
    throw "Application publish failed with exit code $LASTEXITCODE"
}

Remove-Item -LiteralPath $payloadPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $publishDir "*") -DestinationPath $payloadPath -Force

& dotnet publish $installerProject `
    -c Release `
    -r $RuntimeIdentifier `
    --self-contained true `
    -o $installerPublishDir `
    /p:PublishSingleFile=true `
    /p:DebugType=none `
    /p:DebugSymbols=false `
    /p:Version=$version
if ($LASTEXITCODE -ne 0) {
    throw "Installer publish failed with exit code $LASTEXITCODE"
}

$setupSource = Join-Path $installerPublishDir "PitlaneDanmaku.Windows.Setup.exe"
$setupTarget = Join-Path $repoRoot "BundleArtifacts\PitlaneDanmaku.Windows.Setup-$version-$RuntimeIdentifier.exe"
Copy-Item -LiteralPath $setupSource -Destination $setupTarget -Force

Write-Host "Installer created: $setupTarget"
