# Stop the running server so the linker can overwrite the .exe.
#
# Why you may need this:
#   On Windows you cannot overwrite a running .exe. If the server is still running
#   (started by run.ps1, by hand, or from Qt Creator), the next build fails with:
#       collect2.exe: error: ld returned 1 exit status
#       [Makefile:...: ..\..\build\HARS.exe] Error 1
#   Run this script, then build again.
#
# NOTE: ASCII-only on purpose (Windows PowerShell 5.1 reads .ps1 as ANSI).

$ErrorActionPreference = 'Continue'

$port = if ($env:PORT) { $env:PORT } else { '8080' }

Write-Host "Killing whatever holds port $port ..."
$conns = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($conns) {
    $conns | Select-Object -Unique OwningProcess | ForEach-Object {
        $p = Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue
        Write-Host ("  stopping " + $p.ProcessName + " (PID " + $_.OwningProcess + ")")
        Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "  nothing is listening on that port"
}

# Also kill leftover server processes even if they are not holding the port
foreach ($name in @('HARS', 'qt-hospital-server')) {
    $left = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($left) {
        $left | ForEach-Object {
            Write-Host ("  stopping leftover " + $_.ProcessName + " (PID " + $_.Id + ")")
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Start-Sleep -Milliseconds 800

$still = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
if ($still) {
    Write-Host "port $port is STILL in use (try running as administrator)" -ForegroundColor Yellow
    exit 1
}
Write-Host "port $port is free -- you can build now." -ForegroundColor Green