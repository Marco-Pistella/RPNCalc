@echo off
rem SPDX-License-Identifier: MIT
rem Copyright (c) 2026 Marco Pistella
rem ============================================================================================
rem  build.bat - builds RPNCalc.dll (plug-in for Far Manager 3) with the MSYS2 GCC toolchains,
rem              as small as possible. Run it from the Windows Command Prompt, in this folder.
rem
rem    build.bat            builds both versions
rem    build.bat x64        builds only the 64-bit version, into .\x64
rem    build.bat x86        builds only the 32-bit version, into .\X86
rem    build.bat noupx      (with or without x64/x86) skips the UPX packing step
rem    build.bat zip        (with or without the others) also creates RPNCalc-<version>.zip, with the
rem                         DLL folders that were built, RPNCalc.txt and LICENSE; needs tar.exe (Windows 10+)
rem
rem  Each output folder also receives RPNCalcEng.lng and RPNCalcEng.hlf, which Far looks for next
rem  to the DLL, so a folder is ready to be copied into Far's Plugins folder.
rem
rem  Toolchains (found automatically under the MSYS2 folder, usually C:\msys64):
rem    x64  mingw64 (msvcrt: smallest DLL), otherwise ucrt64
rem    x86  mingw32
rem    If a compiler is missing the script prints the pacman command that installs it.
rem
rem  Optional environment variables:
rem    MSYS2_ROOT   MSYS2 folder, when it is not C:\msys64 (or on another drive)
rem    ENV64        x64 environments to try, in order     (default: mingw64 ucrt64)
rem    ENV32        x86 environments to try, in order     (default: mingw32)
rem    FARSDK       folder that contains Far's plugin.hpp, or its Headers.c subfolder (default: .\PluginSDK)
rem
rem  plugin.hpp: taken from this folder, from FARSDK or from Far's PluginSDK folder; if it is not
rem  found it is downloaded once (curl, official FarManager repository) into this folder.
rem
rem  Version: the build stops if the version in rpncalc.rc (VER_MAJOR ... VER_STRING) differs from the
rem  PLUGIN_VER_* values at the top of rpncalc.cpp. Uses the find.exe of Windows (not the one of MSYS2).
rem
rem  UPX: when upx.exe is found (PATH or MSYS2) the DLLs are packed with it, which roughly halves
rem  their size. Use "noupx" for a plain DLL.
rem ============================================================================================
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"

rem ---- command line ----------------------------------------------------------------------------
set "DO64="
set "DO32="
set "USEUPX=1"
set "DOZIP="
:args
if "%~1"=="" goto args_done
if /i "%~1"=="x64"   set "DO64=1" & shift & goto args
if /i "%~1"=="x86"   set "DO32=1" & shift & goto args
if /i "%~1"=="noupx" set "USEUPX=0" & shift & goto args
if /i "%~1"=="zip"   set "DOZIP=1" & shift & goto args
goto usage
:args_done
if not defined DO64 if not defined DO32 set "DO64=1" & set "DO32=1"
if not defined ENV64 set "ENV64=mingw64 ucrt64"
if not defined ENV32 set "ENV32=mingw32"

rem ---- version: rpncalc.rc must agree with the PLUGIN_VER_* values of rpncalc.cpp ---------------------
set "VERBAD="
for %%V in (MAJOR MINOR PATCH BUILD) do call :getver %%V
if defined VERBAD goto fail
set "VERSTR=%VER_MAJOR%.%VER_MINOR%.%VER_PATCH%.%VER_BUILD%"
set "RS="
for /f "tokens=3" %%N in ('%SystemRoot%\System32\find.exe "#define VER_STRING " rpncalc.rc') do set "RS=%%~N"
if not "%RS%"=="%VERSTR%" echo ERROR: VER_STRING in rpncalc.rc is "%RS%", expected "%VERSTR%". & goto fail

rem ---- Far Manager SDK header (plugin.hpp) -----------------------------------------------------
if not defined FARSDK set "FARSDK=.\PluginSDK"
set "SDKINC="
if exist "plugin.hpp" goto sdk_ok
if defined FARSDK if exist "%FARSDK%\plugin.hpp" set SDKINC=-I"%FARSDK%"
if not defined SDKINC if defined FARSDK if exist "%FARSDK%\Headers.c\plugin.hpp" set SDKINC=-I"%FARSDK%\Headers.c"
if defined SDKINC goto sdk_ok
for %%D in ("%ProgramFiles%" "%ProgramW6432%" "%ProgramFiles(x86)%") do if not defined SDKINC if exist "%%~D\Far Manager\PluginSDK\Headers.c\plugin.hpp" set SDKINC=-I"%%~D\Far Manager\PluginSDK\Headers.c"
if defined SDKINC goto sdk_ok
echo plugin.hpp not found: downloading it from the official Far Manager repository ...
curl.exe -fsSL -o plugin.hpp https://raw.githubusercontent.com/FarGroup/FarManager/master/plugins/common/unicode/plugin.hpp
if errorlevel 1 del "plugin.hpp" 2>nul & goto sdk_missing
findstr /c:"FARMANAGERVERSION_MAJOR" plugin.hpp >nul
if errorlevel 1 del "plugin.hpp" 2>nul & goto sdk_missing
goto sdk_ok
:sdk_missing
echo.
echo ERROR: Far Manager's plugin.hpp was not found and could not be downloaded.
echo        Copy it next to this file ^(it is in Far's PluginSDK folder, or in plugins\common\unicode
echo        of the FarManager repository^), or set FARSDK to the folder that contains it.
goto fail
:sdk_ok

rem ---- MSYS2 folder ------------------------------------------------------------------------------
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto msys_ok
set "MSYS2_ROOT="
for %%D in (C D E F G) do if not defined MSYS2_ROOT if exist "%%D:\msys64\usr\bin\bash.exe" set "MSYS2_ROOT=%%D:\msys64"
if defined MSYS2_ROOT goto msys_ok
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MSYS2 64bit" /v InstallLocation 2^>nul ^| find "InstallLocation"') do set "MSYS2_ROOT=%%B"
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto msys_ok
echo.
echo ERROR: MSYS2 was not found. Install it in C:\msys64 or set MSYS2_ROOT to its folder.
goto fail
:msys_ok

rem ---- build -----------------------------------------------------------------------------------
set "FAILED="
if defined DO64 call :build x64 "%ENV64%" x64
if defined DO64 if errorlevel 1 set "FAILED=1"
if defined DO32 call :build x86 "%ENV32%" X86
if defined DO32 if errorlevel 1 set "FAILED=1"
if exist obj rd /s /q obj
if defined FAILED goto fail
if defined DOZIP call :mkzip
if defined DOZIP if errorlevel 1 goto fail
echo.
echo Done.
popd
exit /b 0

:usage
echo Usage: build.bat [x64] [x86] [noupx] [zip]
echo   no option  builds both versions
echo   x64        builds only the 64-bit DLL into .\x64
echo   x86        builds only the 32-bit DLL into .\X86
echo   noupx      does not pack the DLLs with UPX ^(UPX is used when upx.exe is found^)
echo   zip        also creates RPNCalc-^<version^>.zip ^(needs tar.exe, Windows 10 or later^)
popd
exit /b 1

:fail
echo.
echo Build failed.
popd
exit /b 1

rem ---- :build  arch, environments to try, output folder ------------------------------------------------
:build
setlocal
set "ARCH=%~1"
set "OUT=%~3"
set "BIN="
set "ENVNAME="
for %%E in (%~2) do call :probe %%E
if defined BIN goto have_cc
echo.
echo [%ARCH%] ERROR: no g++.exe found in %MSYS2_ROOT% ^(environments tried: %~2^).
echo [%ARCH%] Install the compiler from an MSYS2 shell with:
if /i "%ARCH%"=="x64" echo            pacman -S --needed mingw-w64-x86_64-gcc      ^(smallest DLL, msvcrt^)
if /i "%ARCH%"=="x64" echo            pacman -S --needed mingw-w64-ucrt-x86_64-gcc ^(alternative: needs the UCRT, Windows 10 or updated 7/8.1^)
if /i "%ARCH%"=="x86" echo            pacman -S --needed mingw-w64-i686-gcc
exit /b 1
:have_cc
set "PATH=%BIN%;%PATH%"
if not exist obj mkdir obj
if not exist "%OUT%" mkdir "%OUT%"
echo.
echo [%ARCH%] using %ENVNAME% - %BIN%
"%BIN%\windres.exe" -O coff -I. -i rpncalc.rc -o "obj\res_%ARCH%.o"
if errorlevel 1 exit /b 1
if exist "%OUT%\RPNCalc.dll" del /q "%OUT%\RPNCalc.dll"
call :cc ""
if not errorlevel 1 goto built
echo.
echo [%ARCH%] build failed: retrying with the standard libstdc++ exception helpers ...
call :cc "-DRPN_KEEP_FUNCTEXCEPT"
if errorlevel 1 exit /b 1
:built
call :chkexp
if errorlevel 1 exit /b 1
if "%USEUPX%"=="0" goto copy_files
set "UPXEXE="
for %%P in ("%BIN%" "%MSYS2_ROOT%\ucrt64\bin" "%MSYS2_ROOT%\mingw64\bin" "%MSYS2_ROOT%\mingw32\bin" "%MSYS2_ROOT%\usr\bin") do if not defined UPXEXE if exist "%%~P\upx.exe" set "UPXEXE=%%~P\upx.exe"
if not defined UPXEXE for %%U in (upx.exe) do if not "%%~$PATH:U"=="" set "UPXEXE=%%~$PATH:U"
if not defined UPXEXE goto no_upx
"%UPXEXE%" --best --lzma -q "%OUT%\RPNCalc.dll"
if errorlevel 1 echo [%ARCH%] warning: UPX failed, the DLL is left unpacked.
goto copy_files
:no_upx
set "UPXPKG=mingw-w64-ucrt-x86_64-upx"
if /i "%ENVNAME%"=="mingw64" set "UPXPKG=mingw-w64-x86_64-upx"
if /i "%ENVNAME%"=="mingw32" set "UPXPKG=mingw-w64-i686-upx"
echo [%ARCH%] note: UPX not found, the DLL is not packed ^(smaller with UPX: pacman -S --needed %UPXPKG%^).
:copy_files
copy /y "RPNCalcEng.lng" "%OUT%\" >nul
if errorlevel 1 exit /b 1
copy /y "RPNCalcEng.hlf" "%OUT%\" >nul
if errorlevel 1 exit /b 1
for %%F in ("%OUT%\RPNCalc.dll") do echo [%ARCH%] %%~fF - %%~zF bytes
exit /b 0

rem ---- :probe  environment name: takes it as the compiler folder if it has g++.exe ---------------------
:probe
if defined BIN exit /b 0
if exist "%MSYS2_ROOT%\%~1\bin\g++.exe" set "BIN=%MSYS2_ROOT%\%~1\bin"
if defined BIN set "ENVNAME=%~1"
exit /b 0

rem ---- :cc  extra g++ flags: compiles and links the DLL -----------------------------------------------------
:cc
"%BIN%\g++.exe" -std=c++17 -Os -Wall -Wextra -fno-ident -ffunction-sections -fdata-sections -I. %SDKINC% %~1 rpncalc.cpp "obj\res_%ARCH%.o" -shared -static -static-libgcc -static-libstdc++ -s -Wl,--gc-sections -Wl,--kill-at -o "%OUT%\RPNCalc.dll"
exit /b %errorlevel%

rem ---- :chkexp  checks that the DLL exports the four plug-in entry points with undecorated names -----------
:chkexp
if not exist "%BIN%\objdump.exe" exit /b 0
for %%X in (GetGlobalInfoW SetStartupInfoW GetPluginInfoW OpenW) do "%BIN%\objdump.exe" -p "%OUT%\RPNCalc.dll" | findstr /r /c:"[^A-Za-z0-9_@]%%X$" >nul || (echo [%ARCH%] ERROR: export %%X not found in the DLL ^(check --kill-at^) & exit /b 1)
exit /b 0

rem ---- :getver  part (MAJOR, MINOR, PATCH, BUILD): reads it from rpncalc.cpp and rpncalc.rc, checks they agree ---
:getver
set "CV="
set "RV="
for /f "tokens=3" %%N in ('%SystemRoot%\System32\find.exe "#define PLUGIN_VER_%1 " rpncalc.cpp') do set "CV=%%N"
for /f "tokens=3" %%N in ('%SystemRoot%\System32\find.exe "#define VER_%1 " rpncalc.rc') do set "RV=%%N"
if not defined CV echo ERROR: PLUGIN_VER_%1 not found in rpncalc.cpp. & set "VERBAD=1"
if not defined RV echo ERROR: VER_%1 not found in rpncalc.rc. & set "VERBAD=1"
if defined CV if defined RV if not "%CV%"=="%RV%" echo ERROR: version %1 differs: rpncalc.cpp has %CV%, rpncalc.rc has %RV%. & set "VERBAD=1"
set "VER_%1=%CV%"
exit /b 0

rem ---- :mkzip  creates RPNCalc-<version>.zip with the folders that were built, RPNCalc.txt and LICENSE -----
:mkzip
setlocal
set "ZIPLIST=RPNCalc.txt LICENSE.TXT"
if defined DO64 set "ZIPLIST=%ZIPLIST% x64"
if defined DO32 set "ZIPLIST=%ZIPLIST% X86"
set "ZIPFILE=RPNCalc-%VERSTR%.zip"
if exist "%ZIPFILE%" del /q "%ZIPFILE%"
tar.exe -a -c -f "%ZIPFILE%" %ZIPLIST%
if errorlevel 1 echo ERROR: tar.exe could not create %ZIPFILE%. & exit /b 1
for %%F in ("%ZIPFILE%") do echo %%~fF - %%~zF bytes
exit /b 0
