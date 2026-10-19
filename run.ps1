# Build then run HARS
# NOTE: ASCII-only on purpose (see build.ps1)

$ErrorActionPreference = 'Stop'

$QtRoot = 'D:\Mysoftware\Qt'
$Kit    = Join-Path $QtRoot '6.12.0\mingw_64'
$Mingw  = Join-Path $QtRoot 'Tools\mingw1310_64'
$Root     = $PSScriptRoot
$BuildDir = Join-Path $Root 'build'

& (Join-Path $Root 'build.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The QMYSQL driver plugin needs the MySQL client library; copy them next to the exe
$dlls = @(
    'D:\Mysoftware\MySQL Server 8.0\lib\libmysql.dll',
    (Join-Path $Kit 'bin\libssl-3-x64.dll'),
    (Join-Path $Kit 'bin\libcrypto-3-x64.dll')
)
foreach ($d in $dlls) {
    if (Test-Path $d) { Copy-Item $d $BuildDir -Force }
    else { Write-Host "missing dll: $d" -ForegroundColor Yellow }
}

$env:PATH = "$(Join-Path $Kit 'bin');$(Join-Path $Mingw 'bin');$env:PATH"
$env:PORT = if ($env:PORT) { $env:PORT } else { '8080' }

Set-Location $Root
Write-Host ''
Write-Host 'Starting server (Ctrl+C to stop) ...' -ForegroundColor Cyan
$exe = Get-ChildItem $BuildDir -Filter *.exe -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { Write-Host 'no exe found, run build.ps1 first' -ForegroundColor Red; exit 1 }
& $exe.FullName
