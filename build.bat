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
rem    build.bat max        (with or without x64/x86) UPX also tries --ultra-brute: much slower, smallest DLL
rem    build.bat zip        (with or without the others) also creates RPNCalc-<version>.zip (for example RPNCalc-1.0.1.zip), with the
rem                         DLL folders that were built, RPNCalc.txt, CHANGELOG.TXT and LICENSE; needs tar.exe (Windows 10+)
rem
rem  Each output folder also receives RPNCalcEng.lng and RPNCalcEng.hlf, which Far looks for next
rem  to the DLL, so a folder is ready to be copied into Far's Plugins folder.
rem
rem  Toolchains (found automatically under the MSYS2 folder, usually C:\msys64):
rem    x64  mingw64 (msvcrt: smallest DLL), otherwise ucrt64
rem    x86  mingw32
rem    If the compiler of a DLL is missing the script lists the compilers it can install with pacman
rem    (mingw-w64-x86_64-gcc and mingw-w64-ucrt-x86_64-gcc for x64, mingw-w64-i686-gcc for x86) and
rem    asks which one to install, or none: with none the build of that DLL stops with an error.
rem    If MSYS2 is not found the script offers to download it and to install it unattended (on x64
rem    Windows that has curl.exe, when the MSYS2 folder is free; MSYS2 itself needs Windows 10 or
rem    later): the MSYS2 installer is downloaded into the temporary folder, run silently into
rem    MSYS2_ROOT (default C:\msys64), MSYS2 is updated with pacman and the build goes on. Nothing is
rem    downloaded or installed unless the answer is 1: with 0 the script only prints where to
rem    download MSYS2: https://www.msys2.org
rem
rem  Optional environment variables:
rem    MSYS2_ROOT   MSYS2 folder, when it is not C:\msys64 (or on another drive); it is also the folder
rem                 where MSYS2 is installed, if the script has to install it
rem    MSYS2URL     MSYS2 installer that is downloaded when MSYS2 has to be installed (default: the
rem                 "latest" installer of https://github.com/msys2/msys2-installer)
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
rem  UPX: when upx.exe is found (PATH or MSYS2) the DLLs are packed with it (--best --lzma), which
rem  roughly halves their size; the size before and after is shown. Use "noupx" for a plain DLL.
rem  With "max" UPX also tries --ultra-brute: many more variants, much slower, and the smallest result
rem  that it finds. "max" and "noupx" cannot be used together.
rem ============================================================================================
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"
if errorlevel 1 (
    echo ERROR: cannot change to the folder of build.bat.
    exit /b 1
)

rem Everything is taken from the folder of this script, and from nowhere else: the source files are named with
rem their full path.
set "HERE=%~dp0"
set "SRC=%HERE%rpncalc.cpp"
set "RCFILE=%HERE%rpncalc.rc"

rem ---- command line ----------------------------------------------------------------------------
set "DO64="
set "DO32="
set "USEUPX=1"
set "DOZIP="
set "UPXMAX="
:args
if "%~1"=="" goto args_done
if /i "%~1"=="x64"   set "DO64=1" & shift & goto args
if /i "%~1"=="x86"   set "DO32=1" & shift & goto args
if /i "%~1"=="noupx" set "USEUPX=0" & shift & goto args
if /i "%~1"=="max"   set "UPXMAX=1" & shift & goto args
if /i "%~1"=="zip"   set "DOZIP=1" & shift & goto args
goto usage
:args_done
if "%USEUPX%"=="0" if defined UPXMAX echo ERROR: "max" and "noupx" cannot be used together. & goto usage
if not defined DO64 if not defined DO32 set "DO64=1" & set "DO32=1"
if not defined ENV64 set "ENV64=mingw64 ucrt64"
if not defined ENV32 set "ENV32=mingw32"
if not defined MSYS2URL set "MSYS2URL=https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-x86_64-latest.exe"

rem ---- source files and license ------------------------------------------------------------------
if not exist "%SRC%" echo ERROR: source file "%SRC%" not found. & goto fail
if not exist "%RCFILE%" echo ERROR: resource file "%RCFILE%" not found. & goto fail
rem the license file is LICENSE (or LICENSE.TXT)
set "LICFILE="
if exist "LICENSE" set "LICFILE=LICENSE"
if not defined LICFILE if exist "LICENSE.TXT" set "LICFILE=LICENSE.TXT"
if defined DOZIP if not defined LICFILE echo ERROR: file LICENSE not found, it is needed by the zip option. & goto fail
if defined DOZIP if not exist "CHANGELOG.TXT" echo ERROR: file CHANGELOG.TXT not found, it is needed by the zip option. & goto fail
echo Source: %SRC%

rem ---- version: rpncalc.rc must agree with the PLUGIN_VER_* values of rpncalc.cpp ---------------------
set "VERBAD="
for %%V in (MAJOR MINOR PATCH BUILD) do call :getver %%V
if defined VERBAD goto fail
rem the version shown (VER_STRING, the name of the zip): major.minor.patch (the build number is shown only if it is not 0)
set "VERSTR=%VER_MAJOR%.%VER_MINOR%.%VER_PATCH%"
if not "%VER_BUILD%"=="0" set "VERSTR=%VERSTR%.%VER_BUILD%"
set "RS="
for /f "tokens=1-3" %%M in ('%SystemRoot%\System32\find.exe "#define VER_STRING " "%RCFILE%"') do if "%%M"=="#define" set "RS=%%~O"
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
rem MSYSWANT is the folder asked for with MSYS2_ROOT: if MSYS2 has to be installed, it goes there.
set "MSYSWANT=%MSYS2_ROOT%"
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto msys_ok
set "MSYS2_ROOT="
for %%D in (C D E F G) do if not defined MSYS2_ROOT if exist "%%D:\msys64\usr\bin\bash.exe" set "MSYS2_ROOT=%%D:\msys64"
if defined MSYS2_ROOT goto msys_ok
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MSYS2 64bit" /v InstallLocation 2^>nul ^| find "InstallLocation"') do set "MSYS2_ROOT=%%B"
if defined MSYS2_ROOT if exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto msys_ok
call :msysget
if errorlevel 1 goto fail
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
echo Usage: build.bat [x64] [x86] [noupx] [max] [zip]
echo   no option  builds both versions
echo   x64        builds only the 64-bit DLL into .\x64
echo   x86        builds only the 32-bit DLL into .\X86
echo   noupx      does not pack the DLLs with UPX ^(UPX is used when upx.exe is found^)
echo   max        UPX also tries --ultra-brute: the smallest DLL, but much slower ^(not with noupx^)
echo   zip        also creates RPNCalc-^<version^>.zip ^(needs tar.exe, Windows 10 or later^)
popd
exit /b 1

:fail
echo.
echo Build failed.
popd
exit /b 1

rem ---- :msysget  MSYS2 was not found: offers to download and install it, or says where to get it -------------
rem Returns 0 if MSYS2 has been installed (MSYS2_ROOT is set, and MSYSNEW is 1 so that :install knows it) or 1 if not,
rem with the message printed. Nothing is downloaded or installed unless the answer is 1. The offer is made only on
rem x64 Windows that has curl.exe, and when the folder is free: in every other case only the message is printed.
:msysget
set "MSYSARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "MSYSARCH=%PROCESSOR_ARCHITEW6432%"
if /i not "%MSYSARCH%"=="AMD64" goto msysget_manual
where curl.exe >nul 2>&1
if errorlevel 1 goto msysget_manual
set "MSYSDIR=%MSYSWANT%"
if not defined MSYSDIR set "MSYSDIR=C:\msys64"
if "%MSYSDIR:~-1%"=="\" set "MSYSDIR=%MSYSDIR:~0,-1%"
set "MSYSFWD=%MSYSDIR:\=/%"
set "MSYSSETUP=%TEMP%\build-msys2-setup.exe"
rem the folder must not exist, or must be empty: the installer would write into what is already there
if not exist "%MSYSDIR%\" goto msysget_free
dir /b "%MSYSDIR%" 2>nul | findstr "^" >nul
if not errorlevel 1 goto msysget_notempty
:msysget_free
echo.
echo MSYS2 was not found. It provides the compilers that build the DLL, and it can be installed now:
echo   download  %MSYS2URL%
echo             ^(the MSYS2 installer, about 85 MB: it is saved in the temporary folder and deleted afterwards^)
echo   install   silently into %MSYSDIR%, then MSYS2 is updated with pacman ^(a few minutes^)
echo MSYS2 needs 64-bit Windows 10 or later. Nothing is downloaded or installed unless you answer 1.
echo.
echo         1  download and install MSYS2 now
echo         0  none     do not install it: only show where to download MSYS2
echo.
call :ask 1
if errorlevel 1 goto msysget_go
goto msysget_manual
:msysget_go
if exist "%MSYSSETUP%" del /q "%MSYSSETUP%"
echo.
echo Downloading the MSYS2 installer ...
curl.exe -f -L -# --retry 2 -o "%MSYSSETUP%" "%MSYS2URL%"
if errorlevel 1 goto msysget_dlfail
if not exist "%MSYSSETUP%" goto msysget_dlfail
for %%F in ("%MSYSSETUP%") do set "MSYSSIZE=%%~zF"
if %MSYSSIZE% lss 10000000 goto msysget_dlfail
echo.
echo Installing MSYS2 into %MSYSDIR% ^(a few minutes^) ...
start "" /wait "%MSYSSETUP%" in --confirm-command --accept-messages --root "%MSYSFWD%"
set "MSYSRC=%errorlevel%"
del /q "%MSYSSETUP%" 2>nul
if not "%MSYSRC%"=="0" echo The MSYS2 installer returned the exit code %MSYSRC%.
if not exist "%MSYSDIR%\usr\bin\bash.exe" goto msysget_instfail
set "MSYS2_ROOT=%MSYSDIR%"
set "MSYSNEW=1"
echo.
echo MSYS2 is installed in %MSYS2_ROOT%. Updating it with pacman ^(a few minutes^) ...
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc " "
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman --noconfirm -Syuu"
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman --noconfirm -Syuu"
if errorlevel 1 echo Warning: pacman could not update MSYS2 ^(see its messages above^): the build goes on.
exit /b 0
:msysget_notempty
echo.
echo MSYS2 was not found, but the folder %MSYSDIR% exists and is not empty: MSYS2 is not installed there.
echo Empty it, or set MSYS2_ROOT to another folder, and run build.bat again.
goto msysget_manual
:msysget_dlfail
if exist "%MSYSSETUP%" del /q "%MSYSSETUP%"
echo.
echo ERROR: the MSYS2 installer could not be downloaded from %MSYS2URL%
goto msysget_manual
:msysget_instfail
echo.
echo ERROR: the MSYS2 installation did not complete: %MSYSDIR%\usr\bin\bash.exe was not created.
:msysget_manual
echo.
echo ERROR: MSYS2 was not found. Install it in C:\msys64 or set MSYS2_ROOT to its folder.
echo        Download it from https://www.msys2.org
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
call :install "%~2"
if errorlevel 1 exit /b 1
for %%E in (%~2) do call :probe %%E
if defined BIN goto have_cc
echo [%ARCH%] ERROR: still no g++.exe found in %MSYS2_ROOT%, so the %ARCH% DLL cannot be built.
exit /b 1
:have_cc
set "PATH=%BIN%;%PATH%"
if not exist obj mkdir obj
if not exist "%OUT%" mkdir "%OUT%"
echo.
echo [%ARCH%] using %ENVNAME% - %BIN%
"%BIN%\windres.exe" -O coff -I. -i "%RCFILE%" -o "obj\res_%ARCH%.o"
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
set "UPXOPT=--best --lzma"
if defined UPXMAX set "UPXOPT=--best --ultra-brute --lzma"
if defined UPXMAX echo [%ARCH%] packing with UPX %UPXOPT%, this can take a long time ...
for %%F in ("%OUT%\RPNCalc.dll") do set "SIZE0=%%~zF"
"%UPXEXE%" %UPXOPT% -q "%OUT%\RPNCalc.dll"
if not "%errorlevel%"=="0" echo [%ARCH%] warning: UPX failed, the DLL is left unpacked. & goto copy_files
for %%F in ("%OUT%\RPNCalc.dll") do echo [%ARCH%] UPX: %SIZE0% bytes before, %%~zF bytes after.
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

rem ---- :install  environments to try: no compiler was found, asks which one to install with pacman ---------
rem Returns 0 if a package was installed (the caller looks for the compiler again) or 1 if not, with the message printed.
:install
echo.
if defined MSYSNEW echo [%ARCH%] MSYS2 has just been installed and has no compiler yet ^(environments tried: %~1^).
if not defined MSYSNEW echo [%ARCH%] ERROR: no g++.exe found in %MSYS2_ROOT% ^(environments tried: %~1^).
set "NOPT=0"
for %%E in (%~1) do call :addopt %%E
if "%NOPT%"=="0" goto install_unknown
echo         0  none     install nothing: the %ARCH% DLL is not built
echo.
call :ask %NOPT%
set "PICK=%errorlevel%"
if "%PICK%"=="0" goto install_none
set "N=0"
set "SELPKG="
for %%E in (%~1) do call :selopt %%E
if not defined SELPKG goto install_none
echo.
echo [%ARCH%] installing %SELPKG% with pacman ...
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm %SELPKG%"
if not "%errorlevel%"=="0" goto install_failed
echo.
echo [%ARCH%] %SELPKG% installed.
exit /b 0
:install_unknown
echo [%ARCH%] ERROR: no compiler package is known for these environments, install a g++ for them from an MSYS2 shell.
exit /b 1
:install_none
echo [%ARCH%] ERROR: no compiler was installed, so the %ARCH% DLL cannot be built.
exit /b 1
:install_failed
echo.
echo [%ARCH%] ERROR: pacman could not install %SELPKG%, see its messages above.
echo [%ARCH%] If this MSYS2 is old, update it first: in an MSYS2 shell run  pacman -Syu  ^(again, until nothing is left
echo [%ARCH%] to update^), then run build.bat again. The %ARCH% DLL cannot be built until a compiler is installed.
exit /b 1

rem ---- :addopt  environment name: if a compiler package is known for it, shows it as the next numbered choice ---
:addopt
call :pkgfor %1
if not defined PKG exit /b 0
if "%NOPT%"=="0" echo [%ARCH%] A compiler can be installed now with pacman. Which one do you want?
set /a NOPT+=1
echo         %NOPT%  %PKGTXT%
exit /b 0

rem ---- :selopt  environment name: if it is the choice number PICK, its package goes into SELPKG --------------
:selopt
call :pkgfor %1
if not defined PKG exit /b 0
set /a N+=1
if "%N%"=="%PICK%" set "SELPKG=%PKG%"
exit /b 0

rem ---- :pkgfor  environment name: sets PKG (the g++ package of that environment) and PKGTXT (its menu line) ----
:pkgfor
set "PKG="
set "PKGTXT="
if /i "%~1"=="mingw64" set "PKG=mingw-w64-x86_64-gcc" & set "PKGTXT=mingw64  mingw-w64-x86_64-gcc       smallest DLL, msvcrt runtime. Deprecated by MSYS2"
if /i "%~1"=="ucrt64"  set "PKG=mingw-w64-ucrt-x86_64-gcc" & set "PKGTXT=ucrt64   mingw-w64-ucrt-x86_64-gcc  needs the UCRT: Windows 10, or updated 7/8.1"
if /i "%~1"=="mingw32" set "PKG=mingw-w64-i686-gcc" & set "PKGTXT=mingw32  mingw-w64-i686-gcc          32 bit. MSYS2 is phasing out the 32 bit packages"
exit /b 0

rem ---- :ask  highest choice number: asks for a number from 0 to it and returns it in the errorlevel -----------
rem Anything else is asked again, up to 3 times (also when the input is not a keyboard and ends); then the result is 0.
:ask
setlocal EnableDelayedExpansion
set "TRIES=0"
:ask_again
set "CH="
set /p "CH=Your choice (0-%~1): "
set "PICKED=-1"
for /l %%I in (0,1,%~1) do if "!CH!"=="%%I" set "PICKED=%%I"
if %PICKED% geq 0 exit /b %PICKED%
set /a TRIES+=1
if %TRIES% lss 3 echo Please type a number from 0 to %~1. & goto ask_again
echo No valid choice.
exit /b 0

rem ---- :cc  extra g++ flags: compiles and links the DLL -----------------------------------------------------
:cc
"%BIN%\g++.exe" -std=c++17 -Os -Wall -Wextra -fno-ident -ffunction-sections -fdata-sections -I. %SDKINC% %~1 "%SRC%" "obj\res_%ARCH%.o" -shared -static -static-libgcc -static-libstdc++ -s -Wl,--gc-sections -Wl,--kill-at -o "%OUT%\RPNCalc.dll"
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
for /f "tokens=1-3" %%M in ('%SystemRoot%\System32\find.exe "#define PLUGIN_VER_%1 " "%SRC%"') do if "%%M"=="#define" set "CV=%%O"
for /f "tokens=1-3" %%M in ('%SystemRoot%\System32\find.exe "#define VER_%1 " "%RCFILE%"') do if "%%M"=="#define" set "RV=%%O"
if not defined CV echo ERROR: PLUGIN_VER_%1 not found in rpncalc.cpp. & set "VERBAD=1"
if not defined RV echo ERROR: VER_%1 not found in rpncalc.rc. & set "VERBAD=1"
if defined CV if defined RV if not "%CV%"=="%RV%" echo ERROR: version %1 differs: rpncalc.cpp has %CV%, rpncalc.rc has %RV%. & set "VERBAD=1"
set "VER_%1=%CV%"
exit /b 0

rem ---- :mkzip  creates RPNCalc-<version>.zip with the folders that were built, RPNCalc.txt, CHANGELOG.TXT and LICENSE ---
:mkzip
setlocal
set "ZIPLIST=RPNCalc.txt CHANGELOG.TXT %LICFILE%"
if defined DO64 set "ZIPLIST=%ZIPLIST% x64"
if defined DO32 set "ZIPLIST=%ZIPLIST% X86"
set "ZIPFILE=RPNCalc-%VERSTR%.zip"
if exist "%ZIPFILE%" del /q "%ZIPFILE%"
tar.exe -a -c -f "%ZIPFILE%" %ZIPLIST%
if errorlevel 1 echo ERROR: tar.exe could not create %ZIPFILE%. & exit /b 1
for %%F in ("%ZIPFILE%") do echo %%~fF - %%~zF bytes
exit /b 0
