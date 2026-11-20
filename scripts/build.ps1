# build.ps1 — build LiteDesk Remote dengan Visual Studio 2022 / MSVC.
# Pemakaian:
#   .\scripts\build.ps1                    # build Release
#   .\scripts\build.ps1 -Configuration Debug
#   .\scripts\build.ps1 -RunTests          # build + jalankan semua test
#   .\scripts\build.ps1 -Clean
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$RunTests,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"

function Find-CMake {
    $cm = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cm) { return $cm.Source }
    # CMake biasanya terpasang bersama Visual Studio
    $vsCMake = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022"
    if (Test-Path $vsCMake) {
        $found = Get-ChildItem -Path $vsCMake -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "CMake tidak ditemukan. Instal CMake 3.20+ atau gunakan installer Visual Studio (komponen C++ CMake tools)."
}

function Find-VsGenerator {
    # Deteksi Visual Studio 2022 (Build Tools juga memenuhi syarat)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) { return "Visual Studio 17 2022" }
    }
    throw "Visual Studio 2022 dengan workload 'Desktop development with C++' tidak ditemukan."
}

Write-Host "== LiteDesk Remote build ($Configuration) ==" -ForegroundColor Green
$CMake = Find-CMake
$Gen = Find-VsGenerator
Write-Host "CMake   : $CMake"
Write-Host "Generator: $Gen"

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    & $CMake -S $Root -B $BuildDir -G $Gen -A x64 -DCMAKE_BUILD_TYPE=$Configuration
    if ($LASTEXITCODE -ne 0) { throw "CMake configure gagal" }
}

& $CMake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build gagal - periksa error di atas" }

$ExeDir = Join-Path $BuildDir $Configuration
Write-Host ""
Write-Host "Hasil build:" -ForegroundColor Green
Get-ChildItem $ExeDir -Filter "*.exe" | ForEach-Object { Write-Host ("  " + $_.FullName) }

if ($RunTests) {
    Write-Host ""
    Write-Host "== Menjalankan tests ==" -ForegroundColor Green
    & (Join-Path $ExeDir "ldcore_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "ldcore_tests gagal" }
    & (Join-Path $ExeDir "ldwin_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "ldwin_tests gagal" }
    Write-Host "Semua test lolos." -ForegroundColor Green
}
