param(
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SelfContained
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$appProject = Join-Path $scriptRoot "PitlaneDanmaku.Windows\PitlaneDanmaku.Windows.csproj"
$installerProject = Join-Path $scriptRoot "PitlaneDanmaku.Installer\PitlaneDanmaku.Installer.csproj"
$payloadPath = Join-Path $scriptRoot "PitlaneDanmaku.Installer\Resources\payload.zip"
$publishMode = if ($SelfContained) { "self-contained" } else { "framework-dependent" }
$publishDir = Join-Path $repoRoot "BundleArtifacts\publish\PitlaneDanmaku.Windows-$RuntimeIdentifier-$publishMode"
$installerPublishDir = Join-Path $repoRoot "BundleArtifacts\installer-$publishMode"

[xml]$appProjectXml = Get-Content -Path $appProject
$version = $appProjectXml.Project.PropertyGroup.Version
if ([string]::IsNullOrWhiteSpace($version)) {
    throw "Unable to read app version from $appProject"
}

Remove-Item -LiteralPath $publishDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $installerPublishDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $publishDir | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $payloadPath) -Force | Out-Null

$selfContainedText = if ($SelfContained) { "true" } else { "false" }

& dotnet publish $appProject `
    -c Release `
    -r $RuntimeIdentifier `
    --self-contained $selfContainedText `
    -o $publishDir `
    /p:DebugType=none `
    /p:DebugSymbols=false `
    /p:UseAppHost=true
if ($LASTEXITCODE -ne 0) {
    throw "Application publish failed with exit code $LASTEXITCODE"
}

Remove-Item -LiteralPath $payloadPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $publishDir "*") -DestinationPath $payloadPath -Force

& dotnet publish $installerProject `
    -c Release `
    -r $RuntimeIdentifier `
    --self-contained $selfContainedText `
    -o $installerPublishDir `
    /p:PublishSingleFile=true `
    /p:DebugType=none `
    /p:DebugSymbols=false `
    /p:UseAppHost=true `
    /p:Version=$version
if ($LASTEXITCODE -ne 0) {
    throw "Installer publish failed with exit code $LASTEXITCODE"
}

$setupSource = Join-Path $installerPublishDir "PitlaneDanmaku.Windows.Setup.exe"
$setupSuffix = if ($SelfContained) { "self-contained" } else { "runtime-dependent" }
$setupTarget = Join-Path $repoRoot "BundleArtifacts\PitlaneDanmaku.Windows.Setup-$version-$RuntimeIdentifier-$setupSuffix.exe"
Copy-Item -LiteralPath $setupSource -Destination $setupTarget -Force

Write-Host "Installer created: $setupTarget"
if (-not $SelfContained) {
    Write-Host "This installer does not include .NET 8 Desktop Runtime. Windows will prompt users to install it when required."
}
