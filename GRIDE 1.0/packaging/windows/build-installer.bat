@echo off
setlocal enabledelayedexpansion
rem ===========================================================================
rem  build-installer.bat - build RIDE end to end and produce its Windows setup.
rem
rem  Steps: compile every compiler project + the RIDE editor, (re)generate the
rem  HTML docs, stage the install tree, and build the Visual Studio setup project
rem  (packaging\windows\Installer.vdproj) into %OUT%\RIDE-<ver>.msi.
rem
rem  Usage:   build-installer.bat [5.17]
rem  Env overrides (all optional):
rem     CPP    the C++ compiler clone that carries include\ and lib\ headers
rem            (default: <repo>\..\toolchain\Compilers\C++Optimize, GRIDE1's shared copy)
rem     CC     the C compiler clone whose lib\ holds c90's headers
rem            (default: <CPP>\..\Compiler-Ci)
rem     OUT    output directory for the stage tree and the .msi
rem            (default: <repo>\dist)
rem
rem  Run it from anywhere; paths are resolved from the script's own location.
rem  Requires: Visual Studio 2022 with the Microsoft Visual Studio Installer Projects
rem  2022 extension (devenv builds a .vdproj; MSBuild cannot). Python is optional -
rem  if absent, the committed HTML docs are used.
rem ===========================================================================

rem  The product's name, once, as product.props, the Makefile and make-setup.ps1 spell it.
set "PRODUCT=RIDE"
set "VER=%~1"
rem "from-solution <OutDir>": every project of RIDE.sln already built into OutDir, by release.cmd's msbuild.
set "FROMSLN="
set "BINSRC="
if /i "%~2"=="from-solution" (set "FROMSLN=1" & set "BINSRC=%~f3")
if "%VER%"=="" set "VER=5.17"
rem  The 3.x releases are sealed and built from their own tree, not this one.
if not "%VER%"=="5.17" (echo build-installer.bat: this tree builds 5.17 only & exit /b 2)
set "NV=%VER:.=%"
set "HERE=%~dp0"
for %%I in ("%HERE%..\..") do set "ROOT=%%~fI"
if "%CPP%"=="" set "CPP=%ROOT%\..\toolchain\Compilers\C++Optimize"
if "%CC%"=="" set "CC=%CPP%\..\Compiler-Ci"
if "%OUT%"=="" set "OUT=%ROOT%\dist"
set "STAGE=%OUT%\stage%NV%"

echo ===========================================================================
echo  RIDE %VER% installer build
echo    repo    : %ROOT%
echo    headers : %CPP% (include), %CC% (lib)
echo    output  : %OUT%
echo ===========================================================================

if "%BINSRC%"=="" set "BINSRC=%ROOT%\bin"
if "%FROMSLN%"=="1" (echo [1/6] Built by RIDE.sln into %BINSRC% - not built again & pushd "%ROOT%" & goto :built)
echo [1/6] Building the compilers and the RIDE editor (build.bat solution) ...
pushd "%ROOT%"
call "%ROOT%\build.bat" solution
set "BUILDRC=%errorlevel%"
if not "%BUILDRC%"=="0" (echo   BUILD FAILED & popd & exit /b 1)
:built
rem  **The window must start before it is shipped.** It is mixed-mode, and a
rem  native global with a destructor corrupts its heap before main: it built,
rem  the suites passed, and the installed window died on 2026-09-18 and again
rem  on 2026-09-23. --version runs the same start-up and nothing else.
rem  start /wait: cmd does not wait for a windowed program, so its errorlevel was 0 whatever happened;
rem  and not 0 rather than errorlevel 1, the heap-corruption exit being negative. Both let 03-10-2026's through.
start "" /wait "%BINSRC%\%PRODUCT%.exe" --version
set START_RC=%errorlevel%
if not "%START_RC%"=="0" (echo   %PRODUCT%.exe DOES NOT START - exit %START_RC%, see %%TEMP%%\%PRODUCT%-fault.log & popd & exit /b 1)
popd

echo [2/6] Generating the HTML docs ...
call :genhtml

echo [3/6] Staging the install tree ...
if not exist "%OUT%" mkdir "%OUT%"
call "%HERE%stage.cmd" "%ROOT%" "%CPP%" "%STAGE%" "%CC%" "%BINSRC%"
if errorlevel 1 (echo   STAGE FAILED & exit /b 1)

echo [4/6] Bundling Express Help and the TI build path ...
copy /y "%HERE%EXPRESS-HELP-%VER%.md"   "%STAGE%\EXPRESS-HELP.md"   >nul
if exist "%HERE%EXPRESS-HELP-%VER%.html" copy /y "%HERE%EXPRESS-HELP-%VER%.html" "%STAGE%\EXPRESS-HELP.html" >nul
if not "%VER%"=="3.0" (
  if not exist "%STAGE%\bin\ti" mkdir "%STAGE%\bin\ti"
  copy /y "%HERE%ti-build.cmd" "%STAGE%\bin\ti\" >nul
  copy /y "%HERE%ti-link.cmd"  "%STAGE%\bin\ti\" >nul
  copy /y "%HERE%TI-BUILD.txt" "%STAGE%\bin\ti\" >nul
)

rem The release record, last: every program in bin by CRC-32 and size, which About compares its own with.
"%STAGE%\bin\RIDEConsole.exe" --release-record "%STAGE%\bin"
if errorlevel 1 (echo   RELEASE RECORD FAILED & exit /b 1)

echo [5/6] Building the installer (Visual Studio setup project) ...
rem The .vdproj lists every file and has no wildcard, so it is written from the stage now.
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%make-setup.ps1" "%STAGE%" %VER%
if errorlevel 1 (echo   make-setup.ps1 FAILED & exit /b 1)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -property productPath > "%TEMP%\ride-devenv.txt"
set "DEVENV="
set /p DEVENV=<"%TEMP%\ride-devenv.txt"
del "%TEMP%\ride-devenv.txt"
rem productPath is devenv.exe; devenv.com beside it writes the build to this console.
if not "%DEVENV%"=="" set "DEVENV=%DEVENV:~0,-4%.com"
if not exist "%DEVENV%" (echo   devenv.com of Visual Studio 2022 not found & exit /b 1)
if exist "%HERE%Release\%PRODUCT%-%VER%.msi" del "%HERE%Release\%PRODUCT%-%VER%.msi"
"%DEVENV%" "%HERE%Installer.sln" /build Release
if not exist "%HERE%Release\%PRODUCT%-%VER%.msi" (echo   SETUP PROJECT FAILED - is the Installer Projects 2022 extension installed? & exit /b 1)
copy /y "%HERE%Release\%PRODUCT%-%VER%.msi" "%OUT%\" >nul

echo [6/6] Done.  %OUT%\%PRODUCT%-%VER%.msi
exit /b 0

rem ---- HTML docs ------------------------------------------------------------
:genhtml
set "PY="
rem Run, not merely found: the Store's python.exe stub is found by where and runs nothing.
python -c "print(1)" >nul 2>&1 && set "PY=python"
if "%PY%"=="" py -c "print(1)" >nul 2>&1 && set "PY=py"
if "%PY%"=="" (echo    Python not found - using the committed HTML docs. & goto :eof)
set "MAN="
for %%f in ("%ROOT%\help\manual\*.md") do set "MAN=!MAN! "%%f""
%PY% "%HERE%docs2html.py" "%ROOT%\help\manual.html" "RIDE %VER% - The Complete Manual" "C, C++ and Shalimar - four targets - one editor" !MAN!
%PY% "%HERE%docs2html.py" "%ROOT%\help\guide.html" "RIDE %VER% - User Guide" "Using the editor" ^
  "%ROOT%\help\01-what-it-is.md" "%ROOT%\help\02-getting-started.md" "%ROOT%\help\03-the-screen.md" ^
  "%ROOT%\help\04-editing.md" "%ROOT%\help\05-finding.md" "%ROOT%\help\06-the-project.md" ^
  "%ROOT%\help\07-building.md" "%ROOT%\help\08-debugging.md" "%ROOT%\help\09-the-panel.md" ^
  "%ROOT%\help\10-keys.md" "%ROOT%\help\c.md" "%ROOT%\help\cpp.md" "%ROOT%\help\shalimar.md" ^
  "%ROOT%\help\mixing-c-and-shalimar.md" "%ROOT%\help\appendix-a-shalimar-language.md"
%PY% "%HERE%docs2html.py" "%HERE%EXPRESS-HELP-%VER%.html" "RIDE %VER% - Express Help" "Quick reference" "%HERE%EXPRESS-HELP-%VER%.md"
goto :eof
