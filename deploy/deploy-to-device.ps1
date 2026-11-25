<#
.SYNOPSIS
  Deploys the Chips port (debug APK + YOUR OWN original game files) to an Android device over ADB.
  NOT run by the build; run it yourself on the device you choose.

.DESCRIPTION
  Steps: check adb/device -> install APK -> push CHIPS.DAT/CHIPS.EXE/*.WAV/*.MID to /data/local/tmp, then `run-as`
  copy into the app private dir files/gamedata (debuggable build) -> launch. Nothing is deleted on the device.
  The game files are never part of the APK and must not be published.

.EXAMPLE
  .\deploy-to-device.ps1 -WhatIf
  .\deploy-to-device.ps1 -Serial ABC123
  .\deploy-to-device.ps1 -Connect 192.168.0.50:5555 -SdcardInstead
#>
[CmdletBinding()]
param(
  [string]$Apk      = (Join-Path $PSScriptRoot '..\apk\out\Chips-debug.apk'),
  [string]$DataPath = (Join-Path $PSScriptRoot '..\orig'),
  [string]$Serial,
  [string]$Connect,
  [switch]$SdcardInstead,   # push to /sdcard/Chips/data instead of the private dir (needs storage permission in the app)
  [switch]$SkipInstall,
  [switch]$NoLaunch,
  [switch]$WhatIf
)
$ErrorActionPreference = 'Stop'
$Package = 'com.chips.port'
$Stage = '/data/local/tmp/chips_stage'
function Step($m) { Write-Host "`n== $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Invoke-Adb([string[]]$a, [switch]$Query) {
  $full = @(); if ($Serial) { $full += @('-s', $Serial) }; $full += $a
  if ($WhatIf -and -not $Query) { Write-Host "   [WhatIf] adb $($full -join ' ')"; return '' }
  $o = & adb.exe @full 2>&1; if ($LASTEXITCODE -ne 0) { Fail "adb $($full -join ' ') failed: $o" }; return $o
}
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) { Fail 'adb not found in PATH' }
if ($Connect) { Step "adb connect $Connect"; if (-not $WhatIf) { & adb connect $Connect } }
Step 'Device'; Invoke-Adb @('devices') -Query | Write-Host
if (-not (Test-Path $Apk)) { Fail "APK not found: $Apk (build with apk\build_all.sh)" }
$files = Get-ChildItem $DataPath -File | Where-Object { $_.Name -match '\.(DAT|EXE|WAV|MID)$' }
if (-not ($files | Where-Object { $_.Name -ieq 'CHIPS.DAT' }) -or -not ($files | Where-Object { $_.Name -ieq 'CHIPS.EXE' })) { Fail "CHIPS.DAT / CHIPS.EXE not found in $DataPath" }
if (-not $SkipInstall) { Step 'Install APK'; Invoke-Adb @('install', '-r', '-g', $Apk) | Write-Host }
if ($SdcardInstead) {
  Step 'Push game files to /sdcard/Chips/data'
  Invoke-Adb @('shell', 'mkdir', '-p', '/sdcard/Chips/data') | Out-Null
  foreach ($f in $files) { Invoke-Adb @('push', $f.FullName, "/sdcard/Chips/data/$($f.Name)") | Out-Null }
} else {
  Step 'Push game files into the app private dir (run-as)'
  Invoke-Adb @('shell', 'mkdir', '-p', $Stage) | Out-Null
  foreach ($f in $files) { Invoke-Adb @('push', $f.FullName, "$Stage/$($f.Name)") | Out-Null }
  Invoke-Adb @('shell', "run-as $Package mkdir -p files/gamedata") | Out-Null
  Invoke-Adb @('shell', "run-as $Package sh -c 'cp $Stage/* files/gamedata/'") | Out-Null
  Invoke-Adb @('shell', "rm -r $Stage") | Out-Null
}
if (-not $NoLaunch) { Step 'Launch'; Invoke-Adb @('shell', 'am', 'start', '-n', "$Package/.SetupActivity") | Write-Host }
Write-Host "`nDone." -ForegroundColor Green
