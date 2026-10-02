@echo off
rem ===========================================================================
rem  build.bat - one-click build for the FPS balloon training range
rem
rem  Usage:  build.bat          release build -> bin\BalloonRange.exe
rem          build.bat debug    debug build   -> bin\BalloonRange_d.exe
rem          build.bat clean    remove intermediates in bin\ and obj\
rem
rem  Zero third-party dependencies: only opengl32 / glu32 / user32 / gdi32 /
rem  shell32 / winmm, all shipped with Windows. winmm is the system multimedia
rem  library used for waveOut (the sound effects) - a Windows DLL, not a
rem  third-party package. The compiler is the cl.exe that comes with
rem  Visual Studio; the script locates it automatically (see below).
rem
rem  This file is deliberately pure ASCII with CRLF line endings. cmd.exe
rem  reads .bat files using the OEM code page, so non-ASCII comments here
rem  would be mis-decoded on a Chinese-locale machine, and LF endings break
rem  caret line continuation. Chinese belongs in the source and docs, not here.
rem
rem  On /utf-8: project sources are UTF-8 with BOM, which MSVC detects on its
rem  own. Passing /utf-8 as well pins the *execution* character set to UTF-8,
rem  so the narrow Chinese strings written into the selftest report stay UTF-8
rem  instead of being converted to code page 936.
rem ===========================================================================
setlocal

rem ---- Compiler location.
rem  VSROOT is a hint: if that exact folder exists it is used, which is faster
rem  and lets a machine with several Visual Studios pin one. When it does not
rem  exist, the script asks vswhere (shipped with every Visual Studio
rem  installer) for the newest installation that has the C++ build tools.
set "VSROOT=D:\My\MyProgramFile\Visual_Studio_2026\Community"
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VSROOT="
if not defined VSROOT set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSROOT if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0"

if /i "%~1"=="clean" goto :clean

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found.
    echo         Set VSROOT near the top of build.bat to the Visual Studio
    echo         folder that contains VC\Auxiliary\Build\vcvars64.bat
    exit /b 1
)

echo [1/4] Setting up the MSVC x64 environment ...
call "%VCVARS%" >nul 2>nul
if errorlevel 1 goto :fail

if not exist bin mkdir bin
if not exist obj mkdir obj

rem ---- Source list. Add a line here when adding a file.
set SRC=src\main.cpp src\core.cpp src\config.cpp src\png.cpp src\texture.cpp src\scene.cpp src\balloon.cpp src\player.cpp src\input.cpp src\effect.cpp src\score.cpp src\history.cpp src\hud.cpp src\app.cpp src\render.cpp src\audio.cpp src\selftest.cpp

rem  /Fd keeps the compiler's program database inside obj\ - without it cl.exe
rem  drops a vc140.pdb in the current directory, which is the project root.
set CFLAGS=/nologo /W3 /EHsc /MT /O2 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES /DNOMINMAX /DNDEBUG /Fdobj\vc140.pdb
set LFLAGS=/nologo /SUBSYSTEM:CONSOLE /INCREMENTAL:NO
set LIBS=opengl32.lib glu32.lib user32.lib gdi32.lib shell32.lib winmm.lib
set OUT=bin\BalloonRange.exe

if /i "%~1"=="debug" goto :dbg
goto :build

:dbg
set CFLAGS=/nologo /W3 /EHsc /MTd /Od /Zi /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES /DNOMINMAX /Fdobj\vc140.pdb
set LFLAGS=/nologo /SUBSYSTEM:CONSOLE /DEBUG
set OUT=bin\BalloonRange_d.exe

:build
echo [2/4] Removing stale object files ...
if exist obj\*.obj del /q obj\*.obj >nul 2>nul

rem  Number the sources instead of hardcoding the count - the hardcoded "16"
rem  was already wrong once (history.cpp made it 17) and nothing else noticed.
set /a SRCN=0
for %%f in (%SRC%) do set /a SRCN+=1

echo [3/4] Compiling and linking (%SRCN% source files) ...
cl %CFLAGS% /Foobj\ /Fe%OUT% %SRC% /link %LFLAGS% %LIBS%
if errorlevel 1 goto :fail
if not exist "%OUT%" goto :fail

echo [4/4] Build succeeded.
echo.
echo   Output : %OUT%
echo.
echo   Try    : %OUT% --selftest
echo            %OUT% --shot=shots\demo.png --n=8
echo            %OUT% --show
echo.
exit /b 0

:fail
echo.
echo [ERROR] Build failed. Read the compiler output above.
exit /b 1

:clean
echo Cleaning bin\ and obj\ ...
if exist bin\*.exe del /q bin\*.exe >nul 2>nul
if exist bin\*.pdb del /q bin\*.pdb >nul 2>nul
rem  link.exe also drops .ilk / .exp next to the output when it builds an
rem  incremental debug binary. They are not the program and nothing runs them;
rem  leaving them out of this list is how a 5.6 MB BalloonRange_d.ilk survived
rem  several clean runs and ended up in the delivered directory.
if exist bin\*.ilk del /q bin\*.ilk >nul 2>nul
if exist bin\*.exp del /q bin\*.exp >nul 2>nul
if exist obj\*.obj del /q obj\*.obj >nul 2>nul
if exist obj\*.pdb del /q obj\*.pdb >nul 2>nul
rem  bin\*.dat is deliberately left alone: history / records / params are the
rem  player's own data, not build output.
rem Builds from before /Fd was added dropped this one in the project root.
if exist vc140.pdb del /q vc140.pdb >nul 2>nul
echo Done.
exit /b 0
