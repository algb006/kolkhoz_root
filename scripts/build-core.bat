@echo off
rem Builds the simulation core with MSVC on the Windows host.
rem
rem Invoked over SSH by scripts/win-build.sh, and runnable by hand:
rem     build-core.bat [Debug|Release] [clean]
rem
rem Messages are in English on purpose: this runs through cmd.exe over SSH,
rem where the console code page is whatever the session happens to inherit,
rem and Cyrillic would arrive as garbage on the VM side.

setlocal enableextensions

set "PROJECT_DIR=%~dp0.."
set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Debug"
set "BUILD_DIR=%PROJECT_DIR%\build-msvc"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if /i "%~2"=="clean" (
    echo [core] wiping "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

rem --- MSVC environment ------------------------------------------------------

if not exist "%VSWHERE%" (
    echo [core] vswhere not found - Visual Studio Build Tools are not installed
    exit /b 1
)

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * ^
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
    -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo [core] no Visual Studio installation with the C++ tools
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 (
    echo [core] vcvarsall failed
    exit /b 1
)

rem --- CMake and Ninja -------------------------------------------------------
rem
rem Both ship with the C++ workload but are not put on PATH by vcvarsall, so
rem the bundled copies are used unless the host has its own.

set "CMAKE_EXE=cmake"
where cmake >nul 2>&1 || set "CMAKE_EXE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

where ninja >nul 2>&1 || set "PATH=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

rem --- Build -----------------------------------------------------------------
rem
rem cl.exe is named explicitly: with Ninja, CMake would otherwise pick up any
rem clang or gcc that happens to be on PATH from MSYS2.

echo [core] configure: %BUILD_TYPE%
"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 exit /b 1

echo [core] build
"%CMAKE_EXE%" --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo [core] ok: %BUILD_DIR%
exit /b 0
