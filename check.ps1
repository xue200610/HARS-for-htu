# HARS self-test: public endpoints, pages, login, protected endpoints, permission guards.
# NOTE: ASCII-only on purpose (Windows PowerShell 5.1 reads .ps1 as ANSI).

$port = if ($env:PORT) { $env:PORT } else { '8080' }
$base = "http://127.0.0.1:$port"
$pass = 0
$fail = 0

try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

function Get-Json($url, $token) {
    $hdrs = @{ 'Content-Type' = 'application/json' }
    if ($token) { $hdrs['Authorization'] = "Bearer $token" }
    Invoke-RestMethod -Uri $url -Headers $hdrs -TimeoutSec 10
}

function Expect($name, $cond, $detail) {
    if ($cond) { $script:pass++; Write-Host ("  [OK]   " + $name.PadRight(42) + $detail) -ForegroundColor Green }
    else       { $script:fail++; Write-Host ("  [FAIL] " + $name.PadRight(42) + $detail) -ForegroundColor Red }
}

function Code-Of($err) {
    try { return [int]$err.Exception.Response.StatusCode.value__ } catch { return 0 }
}

Write-Host ("Checking " + $base) -ForegroundColor Cyan
Write-Host ''

Write-Host '[1] public API'
foreach ($e in @('/api/health', '/api/departments', '/api/doctors',
                 '/api/schedules?days=7&onlyAvailable=1', '/api/schedules/dates')) {
    try {
        $r = Get-Json "$base$e"
        $n = if ($r.data -is [array]) { $r.data.Count } else { 1 }
        Expect $e ($r.ok -eq $true) ("data = " + $n)
    } catch { Expect $e $false $_.Exception.Message }
}

Write-Host '[2] web pages'
foreach ($p in @('/index.html', '/login.html', '/patient.html', '/doctor.html', '/admin.html')) {
    try {
        $w = Invoke-WebRequest -Uri "$base$p" -UseBasicParsing -TimeoutSec 10
        Expect $p ($w.StatusCode -eq 200) ("HTTP " + $w.StatusCode + ", " + $w.RawContentLength + " bytes")
    } catch { Expect $p $false $_.Exception.Message }
}

Write-Host '[3] login (password 123456)'
$tokens = @{}
foreach ($u in @(, @('admin', 'admin')) + @(, @('doctor1', 'doctor')) + @(, @('patient1', 'patient'))) {
    try {
        $body = @{ username = $u[0]; password = '123456'; role = $u[1] } | ConvertTo-Json -Compress
        $r = Invoke-RestMethod -Method Post -Uri "$base/api/auth/login" -ContentType 'application/json' -Body $body -TimeoutSec 10
        $tokens[$u[0]] = $r.data.token
        Expect ("login " + $u[0]) ($r.ok -eq $true) ($r.data.user.name + " / " + $r.data.user.role)
    } catch { Expect ("login " + $u[0]) $false $_.Exception.Message }
}

Write-Host '[4] protected API (as admin)'
foreach ($e in @('/api/appointments', '/api/patients', '/api/stats/overview',
                 '/api/stats/trend', '/api/stats/dept-distribution', '/api/stats/doctor-workload')) {
    try {
        $r = Get-Json "$base$e" $tokens['admin']
        $n = if ($r.data -is [array]) { $r.data.Count } else { 1 }
        Expect $e ($r.ok -eq $true) ("data = " + $n)
    } catch { Expect $e $false $_.Exception.Message }
}

Write-Host '[5] permission guards'
try { Get-Json "$base/api/stats/overview" $null | Out-Null; Expect 'no token -> 401' $false 'got 200' }
catch { $c = Code-Of $_; Expect 'no token -> 401' ($c -eq 401) ("HTTP " + $c) }

try { Get-Json "$base/api/patients" $tokens['patient1'] | Out-Null; Expect 'patient -> /api/patients = 403' $false 'got 200' }
catch { $c = Code-Of $_; Expect 'patient -> /api/patients = 403' ($c -eq 403) ("HTTP " + $c) }

try {
    $b = @{ username = 'admin'; password = 'wrong'; role = 'admin' } | ConvertTo-Json -Compress
    Invoke-RestMethod -Method Post -Uri "$base/api/auth/login" -ContentType 'application/json' -Body $b -TimeoutSec 10 | Out-Null
    Expect 'wrong password -> 401' $false 'got 200'
} catch { $c = Code-Of $_; Expect 'wrong password -> 401' ($c -eq 401) ("HTTP " + $c) }

try { Get-Json "$base/api/nope" | Out-Null; Expect 'unknown path -> 404' $false 'got 200' }
catch { $c = Code-Of $_; Expect 'unknown path -> 404' ($c -eq 404) ("HTTP " + $c) }

Write-Host ''
if ($fail -eq 0) {
    Write-Host ("ALL " + $pass + " CHECKS PASSED") -ForegroundColor Green
    exit 0
}
Write-Host ("" + $fail + " CHECK(S) FAILED, " + $pass + " passed") -ForegroundColor Red
exit 1