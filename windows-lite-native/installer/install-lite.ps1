$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms

$AppName = "Pitlane Danmaku Lite"
$DefaultInstallDir = Join-Path $env:LOCALAPPDATA "Programs\PitlaneDanmakuLite"

$FolderDialog = New-Object System.Windows.Forms.FolderBrowserDialog
$FolderDialog.Description = "Choose install folder for $AppName"
$FolderDialog.SelectedPath = $DefaultInstallDir
$FolderDialog.ShowNewFolderButton = $true
if ($FolderDialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
    exit 1
}

$InstallDir = $FolderDialog.SelectedPath
$CreateDesktopShortcut = [System.Windows.Forms.MessageBox]::Show(
    "Create a desktop shortcut?",
    $AppName,
    [System.Windows.Forms.MessageBoxButtons]::YesNo,
    [System.Windows.Forms.MessageBoxIcon]::Question) -eq [System.Windows.Forms.DialogResult]::Yes

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Expand-Archive -LiteralPath (Join-Path $PSScriptRoot "payload.zip") -DestinationPath $InstallDir -Force

$ExePath = Join-Path $InstallDir "pitlane_lite.exe"
$StartMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Pitlane Danmaku Lite"
$DesktopShortcut = Join-Path ([Environment]::GetFolderPath("DesktopDirectory")) "Pitlane Danmaku Lite.lnk"
$StartShortcut = Join-Path $StartMenuDir "Pitlane Danmaku Lite.lnk"
$UninstallScript = Join-Path $InstallDir "Uninstall.ps1"
$UninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PitlaneDanmakuLite"

$Shell = New-Object -ComObject WScript.Shell
New-Item -ItemType Directory -Force -Path $StartMenuDir | Out-Null
$Shortcut = $Shell.CreateShortcut($StartShortcut)
$Shortcut.TargetPath = $ExePath
$Shortcut.WorkingDirectory = $InstallDir
$Shortcut.Save()

if ($CreateDesktopShortcut) {
    $Shortcut = $Shell.CreateShortcut($DesktopShortcut)
    $Shortcut.TargetPath = $ExePath
    $Shortcut.WorkingDirectory = $InstallDir
    $Shortcut.Save()
}
elseif (Test-Path $DesktopShortcut) {
    Remove-Item -LiteralPath $DesktopShortcut -Force
}

$EscapedInstallDir = $InstallDir.Replace("'", "''")
$UninstallContent = @"
param([switch]`$Silent)
`$ErrorActionPreference = "Stop"
`$InstallDir = '$EscapedInstallDir'
`$StartMenuDir = Join-Path `$env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Pitlane Danmaku Lite'
`$DesktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Pitlane Danmaku Lite.lnk'
`$UninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PitlaneDanmakuLite'
Remove-Item -LiteralPath `$DesktopShortcut -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath `$StartMenuDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath `$UninstallKey -Recurse -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 200
Remove-Item -LiteralPath `$InstallDir -Recurse -Force -ErrorAction SilentlyContinue
if (-not `$Silent) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show('Pitlane Danmaku Lite was uninstalled.', 'Pitlane Danmaku Lite') | Out-Null
}
"@
Set-Content -LiteralPath $UninstallScript -Value $UninstallContent -Encoding UTF8

New-Item -Path $UninstallKey -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value $AppName -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value "0.1.1-lite" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "FoxBai" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $InstallDir -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value $ExePath -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$UninstallScript`"" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "QuietUninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$UninstallScript`" -Silent" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "NoModify" -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $UninstallKey -Name "NoRepair" -Value 1 -PropertyType DWord -Force | Out-Null

[System.Windows.Forms.MessageBox]::Show("$AppName was installed.", $AppName) | Out-Null
