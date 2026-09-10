@echo off
REM ===========================================================================
REM  build_asi.bat —— 构建 MajestyI_StrFix.asi
REM
REM  用法：双击执行，或在项目根目录运行 scripts\build_asi.bat
REM  产物：MajestyI_StrFix\Release\MajestyI_StrFix.asi
REM
REM  环境：Visual Studio 2017 Professional（v141_xp 工具集 + Win SDK 7.1A）
REM  注意：源文件是 UTF-8 无 BOM，工程已通过 /utf-8 编译选项声明编码，
REM        否则 MSVC 会按 GBK 解析导致中文串报 C2001 "常量中有换行符"。
REM ===========================================================================
setlocal

set "ROOT=%~dp0.."
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
set "VCVARS=%VS%\VC\Auxiliary\Build\vcvarsall.bat"
set "MSB=%VS%\MSBuild\15.0\Bin\MSBuild.exe"

if not exist "%VCVARS%" ( echo [ERR] missing: %VCVARS% & exit /b 1 )
if not exist "%MSB%"    ( echo [ERR] missing: %MSB%    & exit /b 1 )

echo [1/3] init x86 env ...
call "%VCVARS%" x86 >nul
if errorlevel 1 ( echo [ERR] vcvarsall x86 failed & exit /b 1 )

echo [2/3] build Release^|Win32 ...
"%MSB%" "%ROOT%\MajestyI_StrFix\MajestyI_StrFix.vcxproj" /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
if errorlevel 1 ( echo [ERR] build failed & exit /b 1 )

set "DLL=%ROOT%\MajestyI_StrFix\Release\MajestyI_StrFix.dll"
set "ASI=%ROOT%\MajestyI_StrFix\Release\MajestyI_StrFix.asi"
if not exist "%DLL%" ( echo [ERR] no dll produced & exit /b 1 )

echo [3/3] produce .asi ...
copy /y "%DLL%" "%ASI%" >nul
if errorlevel 1 ( echo [ERR] copy to .asi failed & exit /b 1 )

echo.
echo [OK] %ASI%
endlocal
