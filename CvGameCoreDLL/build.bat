@echo off
rem ============================================================
rem  Builds CvGameCoreDLL.dll for CIV4_performance_booster (original BtS 3.19 SDK).
rem  Usage:  build.bat [Release|Assert|Debug|Profile|Timing] [clean]
rem  Timing = Release + timing profiler, writes Logs\PerfProfile.log after every round.
rem  Double-click = Release build. Output goes to <Target>\CvGameCoreDLL.dll
rem  and the full log to build.log next to this file.
rem  The new DLL is also copied into ..\Assets (YOURMOD in Makefile.settings).
rem ============================================================
setlocal
cd /d "%~dp0"
if defined PERF_BUILD_INNER goto run
set PERF_BUILD_INNER=1
call "%~f0" %* > build.log 2>&1
set RESULT=%ERRORLEVEL%
type build.log
echo.
if "%RESULT%"=="0" (echo BUILD SUCCEEDED) else (echo BUILD FAILED - see build.log)
if not defined NOPAUSE pause
exit /b %RESULT%

:run
set TARGET=%~1
if "%TARGET%"=="" set TARGET=Release
if /i "%TARGET%"=="clean" set TARGET=Release& set DOCLEAN=1
if /i "%~2"=="clean" set DOCLEAN=1

rem --- find nmake.exe from any installed Visual Studio (2017 or newer) ---
set "NMAKE="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -find VC\Tools\MSVC\**\bin\Hostx86\x86\nmake.exe`) do set "NMAKE=%%i"
if not defined NMAKE for /f "delims=" %%i in ('where nmake.exe 2^>nul') do if not defined NMAKE set "NMAKE=%%i"
if not defined NMAKE (echo ERROR: nmake.exe not found. Install Visual Studio with "Desktop development with C++". & exit /b 1)
echo Using nmake: %NMAKE%
echo Target: %TARGET%

rem --- Microsoft build tools that are not part of this repository (see BUILD.md) ---
if not exist "C:\Program Files (x86)\Microsoft Visual C++ Toolkit 2003\bin\cl.exe" (echo ERROR: Visual C++ Toolkit 2003 not found in "C:\Program Files (x86)\Microsoft Visual C++ Toolkit 2003". See BUILD.md. & exit /b 1)
if not exist "..\Tools\WindowsSDK\include\Windows.h" (echo ERROR: Windows SDK headers not found in Tools\WindowsSDK. See BUILD.md. & exit /b 1)
if not exist "..\Tools\WindowsSDK\bin\rc.exe" (echo ERROR: rc.exe not found in Tools\WindowsSDK\bin. See BUILD.md. & exit /b 1)

rem --- clean environment so no modern compiler headers/libs leak in ---
rem rc.exe only sees include dirs via INCLUDE, so point it at the sal.h stub
set "INCLUDE=%~dp0..\Tools\compat"
set "LIB="
set "LIBPATH="
set "CL="
set "_CL_="
set "LINK="
set "PATH=C:\Program Files (x86)\Microsoft Visual C++ Toolkit 2003\bin;%SystemRoot%\system32;%SystemRoot%"
rem link.exe needs cvtres.exe to embed the resource file; the toolkit lacks it, .NET Framework has one
set "PATH=%PATH%;%SystemRoot%\Microsoft.NET\Framework\v4.0.30319"

rem Makefile never rebuilds the precompiled header when a header changes, so do a clean
rem build whenever any .h/.inl file is newer than the existing precompiled header.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "$p='temp_files\%TARGET%\CvGameCoreDLL.pch'; if(!(Test-Path $p)){exit 0}; $t=(Get-Item $p).LastWriteTime; if(Get-ChildItem *.h,*.inl | Where-Object {$_.LastWriteTime -gt $t}){exit 1}; exit 0"
if errorlevel 1 (echo Header files changed - doing a clean rebuild & set DOCLEAN=1)
if defined DOCLEAN "%NMAKE%" clean /NOLOGO
"%NMAKE%" source_list /NOLOGO || exit /b 1
"%NMAKE%" fastdep /NOLOGO || exit /b 1
"%NMAKE%" precompile /NOLOGO || exit /b 1
if /i "%TARGET%"=="Debug" goto nojom
if /i "%TARGET%"=="Profile" goto nojom
bin\jom.exe build /NOLOGO || exit /b 1
goto done
:nojom
"%NMAKE%" build /NOLOGO || exit /b 1
:done
if not exist "%TARGET%\CvGameCoreDLL.dll" (echo ERROR: %TARGET%\CvGameCoreDLL.dll was not produced & exit /b 1)
echo Built %CD%\%TARGET%\CvGameCoreDLL.dll
exit /b 0
