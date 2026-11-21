@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

where cl.exe >nul 2>nul
if %errorlevel%==0 goto :build_msvc

set "VSWHERE="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VSDIR="
if defined VSWHERE (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)

if defined VSDIR (
    if exist "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [i] Found MSVC: !VSDIR!
        call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
        goto :build_msvc
    )
)

where g++.exe >nul 2>nul
if %errorlevel%==0 goto :build_mingw

echo [x] No C++ compiler found.
echo     Install "Visual Studio Build Tools" (C++ workload) or MinGW-w64 / MSYS2.
exit /b 1

:build_msvc
echo [i] Building with MSVC...
rc /nologo /fo nPrompt.res nPrompt.rc
if errorlevel 1 goto :fail
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /W3 nPrompt.cpp nPrompt.res /link /SUBSYSTEM:WINDOWS /OUT:nPrompt.exe
if errorlevel 1 goto :fail
goto :done

:build_mingw
echo [i] Building with MinGW g++...
set "WINDRES="
where windres.exe >nul 2>nul
if %errorlevel%==0 set "WINDRES=windres.exe"
if not defined WINDRES (
    for /f "delims=" %%i in ('where g++.exe') do (
        if not defined WINDRES if exist "%%~dpiwindres.exe" set "WINDRES=%%~dpiwindres.exe"
    )
)
if defined WINDRES (
    "!WINDRES!" -i nPrompt.rc -O coff -o nPrompt.res
    if errorlevel 1 goto :fail
    set "RES=nPrompt.res"
) else (
    echo [w] windres not found - building without the embedded icon
    set "RES="
)
g++ -std=c++17 -O2 -municode -mwindows nPrompt.cpp !RES! -o nPrompt.exe ^
    -lgdiplus -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -lmsimg32
if errorlevel 1 goto :fail
goto :done

:done
echo.
echo [ok] Built: %CD%\nPrompt.exe
exit /b 0

:fail
echo.
echo [x] Build failed.
exit /b 1
