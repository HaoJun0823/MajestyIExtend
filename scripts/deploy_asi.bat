@echo off
REM ===========================================================================
REM  deploy_asi.bat —— 部署 MajestyI_StrFix.asi 到游戏 update 目录
REM
REM  用法：scripts\deploy_asi.bat
REM  注意：游戏运行中会占用文件，请先退出游戏。
REM ===========================================================================
setlocal

set "ROOT=%~dp0.."
set "GAME=I:\SteamLibrary\steamapps\common\Majesty HD"
set "ASI=%ROOT%\MajestyI_StrFix\Release\MajestyI_StrFix.asi"
set "DEST=%GAME%\update\MajestyI_StrFix.asi"

if not exist "%ASI%" (
    echo [ERR] 未找到 %ASI%
    echo       请先运行 scripts\build_asi.bat
    exit /b 1
)
if not exist "%GAME%\update" mkdir "%GAME%\update"

echo [1/2] copy ...
copy /y "%ASI%" "%DEST%" >nul
if errorlevel 1 (
    echo [ERR] 复制失败，游戏可能正在运行。
    exit /b 1
)

echo [2/2] verify ...
for %%A in ("%DEST%") do echo      size=%%~zA bytes

echo.
echo [OK] %DEST%
echo      运行游戏后查看: %GAME%\update\MajestyI_StrFix.log
endlocal
