@echo off
rem Builds the Windows-side P0 tools with MSVC (no CMake yet; see roadmap decision D5).
rem   windows\build.cmd           build pen_diag.exe and unit_tests.exe into windows\out\
rem   windows\build.cmd test      build, then run the unit tests
setlocal

set "ROOT=%~dp0"
set "OUT=%ROOT%out"
if not exist "%OUT%" mkdir "%OUT%"

rem Note: no parenthesised blocks around paths here; %ProgramFiles(x86)% contains ")".
where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :no_msvc
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 goto :no_msvc
goto :have_cl
:no_msvc
echo MSVC not found. Install "Desktop development with C++" (Visual Studio or Build Tools).
exit /b 1
:have_cl

set "CFLAGS=/nologo /std:c++20 /W4 /WX /EHsc /O2 /MT /utf-8 /permissive- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00"
set "LIBS=user32.lib gdi32.lib advapi32.lib"
set "CORE=%ROOT%core\injection\pen_injector.cpp"
set "DIAG=%ROOT%tests\pen-diagnostics"

echo Building pen_diag.exe
cl %CFLAGS% /Fo"%OUT%\\" /Fe"%OUT%\pen_diag.exe" "%DIAG%\main.cpp" "%DIAG%\diag_target.cpp" "%DIAG%\frame_stats.cpp" "%DIAG%\patterns.cpp" "%DIAG%\replay.cpp" "%DIAG%\pen_space.cpp" "%CORE%" %LIBS% || exit /b 1

echo Building unit_tests.exe
cl %CFLAGS% /Fo"%OUT%\\" /Fe"%OUT%\unit_tests.exe" "%ROOT%tests\unit\test_main.cpp" "%DIAG%\frame_stats.cpp" "%DIAG%\patterns.cpp" "%DIAG%\replay.cpp" "%CORE%" %LIBS% || exit /b 1

if /i "%~1"=="test" (
  "%OUT%\unit_tests.exe" || exit /b 1
)
echo OK: %OUT%
