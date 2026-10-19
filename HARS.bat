@echo off
rem ============================================================
rem  本文件是 GBK(936) 编码 + CRLF 换行，请勿改成 UTF-8 或 LF
rem
rem  HARS 医院挂号管理系统 —— 唯一入口（原来 4 个 bat 合并到这里）
rem    双击           -> 显示菜单
rem    命令行带编号    -> HARS.bat 1  /  HARS.bat 3  ...
rem ============================================================
chcp 936 >nul
title HARS 控制台
cd /d "%~dp0"

rem ============ 数据库配置 ============
rem  真实密码放在同目录的 db.env 里（该文件不进 Git）。
rem  第一次使用：把 db.env.example 复制成 db.env，填上你的 MySQL 密码。
if not exist "%~dp0db.env" (
  echo.
  echo [提示] 没有找到 db.env，无法连接数据库。
  echo        请把 db.env.example 复制成 db.env，填入你的 MySQL 密码后重试。
  echo.
  pause
  goto :eof
)
for /f "usebackq eol=# tokens=1,* delims==" %%a in ("%~dp0db.env") do set "%%a=%%b"
rem ====================================
set "PATH=D:\Mysoftware\Qt\6.12.0\mingw_64\bin;D:\Mysoftware\Qt\Tools\mingw1310_64\bin;%PATH%"
set "EXE=%~dp0build\HARS.exe"
set "PORT=8080"
set "ARGMODE="

if not "%~1"=="" (
  set "ARGMODE=1"
  set "CH=%~1"
  goto DISPATCH
)

:MENU
cls
echo.
echo ============================================================
echo    HARS  医院挂号管理系统   控制台
echo ============================================================
echo      1   启动服务（浏览器自动打开）
echo      2   停止服务 / 释放端口
echo      3   查看访问链接（本机 / 局域网）
echo      4   放行防火墙（让局域网其他设备能连，会弹 UAC）
echo      5   重新编译
echo      6   接口自检（需服务已启动）
echo      0   退出
echo ============================================================
echo.
set "CH="
set /p "CH=请输入编号后回车: "

:DISPATCH
if "%CH%"=="1" goto START
if "%CH%"=="2" goto STOP
if "%CH%"=="3" goto LINKS
if "%CH%"=="4" goto FIREWALL
if "%CH%"=="5" goto BUILD
if "%CH%"=="6" goto CHECK
if "%CH%"=="0" exit /b 0
goto MENU

:START
echo.
if not exist "%EXE%" (
  echo [提示] 还没编译，正在自动编译...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
  if errorlevel 1 (
    echo.
    echo [失败] 编译出错，把上面的错误发我
    call :WAIT
    goto DONE
  )
)
echo 正在启动服务，浏览器会自动打开 http://127.0.0.1:%PORT%
echo 停止服务：关掉本窗口，或按 Ctrl+C
echo.
start "" powershell -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Seconds 2; Start-Process 'http://127.0.0.1:%PORT%'"
"%EXE%"
echo.
echo 服务已停止。
call :WAIT
goto DONE

:STOP
echo.
echo 正在停止服务、释放端口 %PORT% ...
powershell -NoProfile -Command "$c=Get-NetTCPConnection -LocalPort %PORT% -State Listen -EA SilentlyContinue; if($c){ $c|Select-Object -Unique OwningProcess|ForEach-Object{ Stop-Process -Id $_.OwningProcess -Force -EA SilentlyContinue } }; foreach($n in 'HARS','qt-hospital-server'){ Get-Process -Name $n -EA SilentlyContinue|Stop-Process -Force -EA SilentlyContinue }; Write-Host ('  [ok] 端口 ' + %PORT% + ' 已释放，可以重新编译了')"
call :WAIT
goto DONE

:LINKS
echo.
echo ============================================================
echo    访问链接
echo ============================================================
echo    你自己用： http://127.0.0.1:%PORT%
echo.
echo    局域网访问（需连同一个 WiFi）：
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' -and $_.InterfaceAlias -notlike '*vEthernet*' } | ForEach-Object { Write-Host ('        http://' + $_.IPAddress + ':%PORT%') }"
echo.
echo    服务状态：
powershell -NoProfile -Command "$c=Get-NetTCPConnection -LocalPort %PORT% -State Listen -EA SilentlyContinue; if($c){ Write-Host '        运行中   OK' } else { Write-Host '        没在跑（先选 1 启动）' }"
echo    防火墙：
powershell -NoProfile -Command "$r=Get-NetFirewallRule -DisplayName 'HARS-%PORT%' -ErrorAction SilentlyContinue; if($r){ Write-Host '        已放行   OK' } else { Write-Host '        未放行（选 4 放行）' }"
echo    当前 WiFi：
powershell -NoProfile -Command "$s=(netsh wlan show interfaces | Select-String '^\s*SSID\s+:' | Select-Object -First 1); if($s){ Write-Host ('        ' + $s.Line.Trim()) }"
echo ============================================================
call :WAIT
goto DONE

:FIREWALL
echo.
echo 正在放行 TCP %PORT%（马上弹 UAC，请点「是」）...
powershell -NoProfile -Command "Start-Process -FilePath 'netsh' -ArgumentList 'advfirewall','firewall','add','rule','name=HARS-%PORT%','dir=in','action=allow','protocol=TCP','localport=%PORT%','profile=any' -Verb RunAs -Wait"
echo.
powershell -NoProfile -Command "$r=Get-NetFirewallRule -DisplayName 'HARS-%PORT%' -ErrorAction SilentlyContinue; if($r){ Write-Host '  [成功] 已放行，局域网已可访问' } else { Write-Host '  [未完成] 没有放行（可能点了取消）' }"
call :WAIT
goto DONE

:BUILD
echo.
echo 正在重新编译...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
call :WAIT
goto DONE

:CHECK
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check.ps1"
call :WAIT
goto DONE

:WAIT
if not defined ARGMODE pause
exit /b 0

:DONE
if defined ARGMODE exit /b 0
goto MENU