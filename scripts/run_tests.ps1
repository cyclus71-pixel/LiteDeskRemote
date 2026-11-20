# run_tests.ps1 — build + jalankan seluruh test (core + Windows integration).
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

# Build dulu (tanpa -RunTests agar tidak rekursif)
& (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw "build gagal" }

$ExeDir = Join-Path (Join-Path $Root "build") $Configuration
$Fail = $false

Write-Host ""
Write-Host "== ldcore_tests (unit test inti) ==" -ForegroundColor Green
& (Join-Path $ExeDir "ldcore_tests.exe")
if ($LASTEXITCODE -ne 0) { $Fail = $true }

Write-Host ""
Write-Host "== ldwin_tests (integrasi Windows: CNG, TLS+auth localhost, discovery) ==" -ForegroundColor Green
& (Join-Path $ExeDir "ldwin_tests.exe")
if ($LASTEXITCODE -ne 0) { $Fail = $true }

Write-Host ""
if ($Fail) {
    Write-Host "ADA TEST GAGAL" -ForegroundColor Red
    exit 1
}
Write-Host "SEMUA TEST LOLOS" -ForegroundColor Green
