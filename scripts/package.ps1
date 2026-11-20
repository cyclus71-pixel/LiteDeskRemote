# package.ps1 — kemas hasil build menjadi portable zip + (opsional) installer Inno Setup.
# Pemakaian:
#   .\scripts\package.ps1                 # build Release + portable zip
#   .\scripts\package.ps1 -Installer      # + installer (butuh Inno Setup/ISCC.exe)
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Installer
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"
$ExeDir = Join-Path $BuildDir $Configuration
$Version = "0.1.0"
$Stage = Join-Path $Root "dist\LiteDeskRemote-Portable-$Version"

if (-not (Test-Path (Join-Path $ExeDir "ldhost.exe"))) {
    Write-Host "Belum ada build - menjalankan build.ps1 dulu..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}

# 1. Staging
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

Copy-Item (Join-Path $ExeDir "ldhost.exe")   $Stage
Copy-Item (Join-Path $ExeDir "ldviewer.exe") $Stage
Copy-Item (Join-Path $Root "README.md")      $Stage
Copy-Item (Join-Path $Root "LICENSE")        $Stage
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "docs") | Out-Null
Copy-Item (Join-Path $Root "docs\*.md") (Join-Path $Stage "docs")

$ReadmePortable = @'
LITEDESK REMOTE - PORTABLE (FASE 1)
===================================

ldhost.exe  : jalankan di komputer yang akan DIKENDALIKAN
ldviewer.exe: jalankan di komputer PENGENDALI

Aplikasi portable: tidak perlu instalasi; data tersimpan di
%LOCALAPPDATA%\LiteDeskRemote (config, PIN, log).

Pertama kali: buka My Device (host) untuk membuat PIN, lalu
nyalakan Remote Access di Dashboard. Viewer menghubungkan via
Device ID / IP (lihat docs\GUIDE_LAN.md).

Catatan FASE 1: koneksi, autentikasi, permission, discovery LAN,
log - sudah aktif. Streaming layar hadir di FASE 2.
'@
Set-Content -Path (Join-Path $Stage "PORTABLE-README.txt") -Value $ReadmePortable -Encoding UTF8

# 2. Zip
$Zip = Join-Path $Root "dist\LiteDeskRemote-Portable-$Version.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip
Write-Host "Portable zip: $Zip" -ForegroundColor Green

# 3. Installer (opsional)
if ($Installer) {
    $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if (-not $iscc) {
        $isccPath = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
        if (Test-Path $isccPath) { $iscc = Get-Item $isccPath }
    }
    if ($iscc) {
        & $iscc.Source "/DAppVersion=$Version" (Join-Path $Root "installer\litedesk.iss")
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Installer: $(Join-Path $Root 'installer\Output\LiteDeskRemote-Setup-0.1.0.exe')" -ForegroundColor Green
        }
    } else {
        Write-Host "Inno Setup tidak ditemukan; installer dilewati (portable zip tetap dibuat)." -ForegroundColor Yellow
    }
}
