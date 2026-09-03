@echo off
rem Builds the simulation core with MSVC on the Windows host.
rem
rem Invoked over SSH by scripts/win-build.sh, and runnable by hand:
rem     build-core.bat [Debug|Release] [clean]
rem
rem Each configuration builds into its OWN directory and publishes into its own
rem folder under publish\. Both used to share build-msvc\, so whichever was
rem built last owned lib\core.lib and nothing outside could tell which one that
rem was - while the graphics layer can only take Release (the editor ships with
rem /MD and _ITERATOR_DEBUG_LEVEL=0, and a Debug CRT build is LNK2038 for it).
rem
rem Messages are in English on purpose: this runs through cmd.exe over SSH,
rem where the console code page is whatever the session happens to inherit,
rem and Cyrillic would arrive as garbage on the VM side.

setlocal enableextensions

rem %~dp0 ends in a slash, so ..\ would show up in every path printed below.
for %%i in ("%~dp0..") do set "PROJECT_DIR=%%~fi"
set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Debug"
if /i not "%BUILD_TYPE%"=="Debug" if /i not "%BUILD_TYPE%"=="Release" (
    echo [core] unknown configuration "%BUILD_TYPE%" - expected Debug or Release
    exit /b 2
)
set "BUILD_DIR=%PROJECT_DIR%\build-msvc-%BUILD_TYPE%"
set "PUBLISH_DIR=%PROJECT_DIR%\publish\%BUILD_TYPE%"
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

rem --- Publish ---------------------------------------------------------------
rem
rem The build directory is a workshop: half-built objects, the wreckage of an
rem interrupted build, files nobody promised. The consumer takes from publish\
rem instead, and this script is its only writer - so the folder is wiped and
rem rewritten rather than updated in place, and a file that is there is a file
rem this build put there.
rem
rem core.lib is MERGED with enkiTS.lib. The core's own archive carries eight
rem unresolved enki:: symbols from the step engine, so it does not link alone;
rem shipping two files would push our choice of scheduler into the consumer's
rem build script, and CLAUDE.md 7 says the core is ONE library. No enkiTS
rem header is published: none of our public headers names it.

set "CORE_LIB=%BUILD_DIR%\lib\core.lib"
set "ENKI_LIB=%BUILD_DIR%\lib\enkiTS.lib"

if not exist "%CORE_LIB%" (
    echo [core] no library to publish: "%CORE_LIB%" is missing
    exit /b 1
)
if not exist "%ENKI_LIB%" (
    echo [core] enkiTS.lib is missing - a merged core.lib would not link
    exit /b 1
)

echo [core] publish: %PUBLISH_DIR%
if exist "%PUBLISH_DIR%" rmdir /s /q "%PUBLISH_DIR%"
mkdir "%PUBLISH_DIR%\lib"
if errorlevel 1 exit /b 1

lib.exe /NOLOGO /OUT:"%PUBLISH_DIR%\lib\core.lib" "%CORE_LIB%" "%ENKI_LIB%"
if errorlevel 1 (
    echo [core] merging core.lib with enkiTS.lib failed
    exit /b 1
)

rem The public headers, plus the generated version header - which lives in the
rem build tree, not the source tree, because the number belongs to a file
rem (VERSION) and a second tracked copy of it would rot.
xcopy /E /I /Y /Q "%PROJECT_DIR%\include" "%PUBLISH_DIR%\include" >nul
if errorlevel 1 exit /b 1
if exist "%PUBLISH_DIR%\include\core_common\version.h.in" del /q "%PUBLISH_DIR%\include\core_common\version.h.in"
copy /y "%BUILD_DIR%\generated\include\core_common\version.h" "%PUBLISH_DIR%\include\core_common\version.h" >nul
if errorlevel 1 (
    echo [core] the generated version header is missing from "%BUILD_DIR%"
    exit /b 1
)

rem VERSION travels WITH the library, not only in the source tree: that is what
rem lets the consumer fail loudly on a stale artifact instead of linking it.
copy /y "%PROJECT_DIR%\VERSION" "%PUBLISH_DIR%\VERSION" >nul
if errorlevel 1 exit /b 1

rem The layout report, produced BY THIS BUILD (tools/core_layout.cpp). It is
rem the only thing in publish\ that can catch a pair that is not a pair: the
rem headers say one size, the archive was compiled with another, and every
rem version string on both sides still agrees. Whoever links compares.
if exist "%BUILD_DIR%\bin\core_layout.exe" (
    "%BUILD_DIR%\bin\core_layout.exe" > "%PUBLISH_DIR%\LAYOUT.txt"
    if errorlevel 1 (
        echo [core] the layout report could not be produced
        exit /b 1
    )
) else (
    echo [core] core_layout.exe is missing from "%BUILD_DIR%" - the publish would be uncheckable
    exit /b 1
)

set /p PUBLISHED_VERSION=<"%PUBLISH_DIR%\VERSION"
echo [core] published %BUILD_TYPE% %PUBLISHED_VERSION%
echo [core] ok: %BUILD_DIR%
exit /b 0
