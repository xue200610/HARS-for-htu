# Build script for the HARS Qt server (qmake + MinGW)
# NOTE: ASCII-only on purpose -- Windows PowerShell 5.1 reads .ps1 as ANSI by default,
#       so non-ASCII text here would show up as garbage. Keep it English.

$ErrorActionPreference = 'Stop'

$QtRoot = 'D:\Mysoftware\Qt'
$Kit    = Join-Path $QtRoot '6.12.0\mingw_64'
$Mingw  = Join-Path $QtRoot 'Tools\mingw1310_64'

if (-not (Test-Path (Join-Path $Kit 'bin\qmake.exe'))) { Write-Host "qmake not found: $Kit" -ForegroundColor Red; exit 1 }

# Put Qt and MinGW on PATH so the compiler and Qt DLLs can be found
$env:PATH = "$(Join-Path $Kit 'bin');$(Join-Path $Mingw 'bin');$env:PATH"

$Root     = $PSScriptRoot
$BuildDir = Join-Path $Root 'build'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Auto-detect the .pro file. (The project has been renamed once already,
# so never hardcode the file name here -- otherwise a rename silently breaks the build.)
$pros = @(Get-ChildItem $Root -Filter *.pro -File -ErrorAction SilentlyContinue)
if ($pros.Count -eq 0) { Write-Host "no .pro file found in $Root" -ForegroundColor Red; exit 1 }
$ProFile = $pros[0]
Write-Host ("project: " + $ProFile.FullName) -ForegroundColor Cyan

Set-Location $BuildDir

Write-Host '[1/2] qmake ...' -ForegroundColor Cyan
& (Join-Path $Kit 'bin\qmake.exe') $ProFile.FullName
if ($LASTEXITCODE -ne 0) { Write-Host 'qmake failed' -ForegroundColor Red; exit $LASTEXITCODE }

Write-Host '[2/2] mingw32-make ...' -ForegroundColor Cyan
& (Join-Path $Mingw 'bin\mingw32-make.exe') -j8
if ($LASTEXITCODE -ne 0) { Write-Host 'build failed' -ForegroundColor Red; exit $LASTEXITCODE }

# Do not hardcode the exe name either -- report whatever qmake produced
$exe = Get-ChildItem $BuildDir -Filter *.exe -File -ErrorAction SilentlyContinue | Select-Object -First 1
if ($exe) {
    Write-Host ("OK -> " + $exe.FullName) -ForegroundColor Green
} else {
    Write-Host 'build finished but no .exe found in build dir' -ForegroundColor Yellow
    Get-ChildItem $BuildDir | Select-Object Name
}
