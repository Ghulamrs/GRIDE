@echo off
rem The release build, Windows: every program compiled from a fresh git checkout, the installer last.
rem git is the source of record, so nothing of a working tree reaches a release; one run, one set of build times.
rem
rem   release.cmd [version]   -> %RELEASE_DIR%\<stamp>\RIDE-<ver>\dist\RIDE-<ver>.msi and RELEASE.txt beside it
rem
rem What is final is each repository's default branch on GitHub (main or master), never a side branch.
rem The three compilers are repositories of their own beside VM6747 since 2026-10-12, so each is taken
rem at its own head like the rest; there is no submodule pin left to agree with (D16).
rem RELEASE_DIR (default %USERPROFILE%\ride-release).
rem RELEASE_BRANCHES, for a rehearsal only: "repo:branch ..." by GitHub name (Compiler-Cpp-Optimize for
rem cpp11), e.g. "SIM6747:rename-sim6747 RIDE-4.7:rename-sim6747". A rehearsal is not a release: the seals
rem of a side branch are reported and do not stop it.
rem RELEASE_DRYRUN=1 stops after the checks that come before the build - the seals - and, with
rem RELEASE_BIN naming a bin\ already built, runs build.bat confirm over it. Neither is for a release.
setlocal enabledelayedexpansion
set "VER=%~1"
if "%VER%"=="" set "VER=5.17"
if "%RELEASE_DIR%"=="" set "RELEASE_DIR=%USERPROFILE%\ride-release"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%t"
set "W=%RELEASE_DIR%\%STAMP%"
rem https needs no key, and every repository is public.
set "GIT=git -c url.https://github.com/.insteadOf=git@github.com: -c advice.detachedHead=false -c core.autocrlf=false"

mkdir "%W%" || exit /b 1
echo RIDE %VER% release in %W%
echo [1/3] Fresh checkouts from github.com/Ghulamrs
call :clone RIDE-%VER% RIDE-4.7 || exit /b 1
call :clone TI\VM6747 VM6747 || exit /b 1
call :clone Compilers\Compiler-Ci Compiler-Ci || exit /b 1
call :clone Compilers\Compiler-Si Compiler-Si || exit /b 1
call :clone Compilers\C++Optimize Compiler-Cpp-Optimize || exit /b 1
call :clone TI\ASM6x ASM6x || exit /b 1
call :clone TI\LNK6x LNK6X || exit /b 1
call :clone Windows\LINK LINK || exit /b 1
call :clone Windows\MASM MASM || exit /b 1
call :clone Compilers\Converter-C2S Converter-C2S || exit /b 1
call :clone TI\SIM6747 SIM6747 || exit /b 1
call :clone TI\RTS6x RTS6x || exit /b 1
(echo RIDE %VER%, built %DATE% %TIME% on %COMPUTERNAME% ^(Windows^) from fresh checkouts& type "%W%\RELEASE.txt.repos") > "%W%\RELEASE.txt"
del "%W%\RELEASE.txt.repos"

rem The sources as sealed, before a line is compiled: a release of code that does not match its seal is refused (T1).
echo [1b] The seals - verify_seals.py, MASTER.SEAL down to every project
python -c "print(1)" >nul 2>&1
if errorlevel 1 (
  echo   WARNING: no Python on this machine, so the seals were NOT checked here - release.sh checks the same commits
) else (
  python "%W%\RIDE-%VER%\verify_seals.py" > "%W%\SEALS.txt" 2>&1
  if errorlevel 1 (
    type "%W%\SEALS.txt"
    if "%RELEASE_BRANCHES%"=="" (echo release.cmd: the sources do not match their seals - reseal, commit, push, run again & exit /b 1)
    echo   WARNING: a rehearsal from RELEASE_BRANCHES - the seals do not match, and this is not a release
  )
)

if "%RELEASE_DRYRUN%"=="1" (
  if not "%RELEASE_BIN%"=="" (
    set "BINDIR=%RELEASE_BIN%"
    rem This script's own tree, whose build.bat is the one being rehearsed.
    pushd "%~dp0..\.."
    call build.bat confirm
    set "RC=!errorlevel!"
    popd
    if not "!RC!"=="0" (echo release.cmd: %RELEASE_BIN% is MISSING something RIDE drives - see above & exit /b 1)
  )
  echo release.cmd: a dry run - the checks passed, and nothing was built
  exit /b 0
)

echo [2/3] Every program by RIDE.sln, then the installer - its setup project, built by devenv after staging
set "R=%W%\RIDE-%VER%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem Through a file, as build.bat does: for /f cannot run a quoted path that has a space in it.
"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -property installationPath > "%TEMP%\ride-release-vspath.txt"
set "VSPATH="
set /p VSPATH=<"%TEMP%\ride-release-vspath.txt"
del "%TEMP%\ride-release-vspath.txt"
if "%VSPATH%"=="" (echo could not find Visual Studio 2022 & exit /b 1)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%R%"
msbuild RIDE.sln /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%R%\bin\\" /v:minimal /m
set "RC=%errorlevel%"
popd
if not "%RC%"=="0" (echo BUILD FAILED & exit /b 1)
rem Everything RIDE drives, the six RTS6x and Shalimar libraries among it, or no installer (S2).
pushd "%R%"
set "BINDIR=%R%\bin"
call build.bat confirm
set "RC=%errorlevel%"
popd
if not "%RC%"=="0" (echo release.cmd: %R%\bin is MISSING something RIDE drives - see above & exit /b 1)
call "%R%\packaging\windows\build-installer.bat" %VER% from-solution "%R%\bin"
if errorlevel 1 (echo INSTALLER FAILED & exit /b 1)
set "PRODUCT=%R%\dist\RIDE-%VER%.msi"
if not exist "%PRODUCT%" (echo release.cmd: no %PRODUCT% & exit /b 1)

echo [3/3] Done
echo   installer : %PRODUCT%
echo   record    : %W%\RELEASE.txt
for %%f in ("%PRODUCT%") do echo installer %%~f  %%~tf>> "%W%\RELEASE.txt"
rem What was packaged, by SHA-256, in a second table of MASTER.SEAL beside RELEASE.txt (D13).
python -c "print(1)" >nul 2>&1
if errorlevel 1 (echo   WARNING: no Python on this machine, so MASTER.SEAL's artefact table was NOT written & exit /b 0)
python "%R%\tools\master-seal" artefacts "%R%\bin" --release "RIDE %VER%, RIDE-%VER%.msi, built %DATE% %TIME% on %COMPUTERNAME%"
if errorlevel 1 (echo release.cmd: the artefact table was not written & exit /b 1)
copy /y "%R%\MASTER.SEAL" "%W%\MASTER.SEAL" >nul
echo   artefacts : %W%\MASTER.SEAL
exit /b 0

:clone
rem dir  repository - cloned at its default branch, the one GitHub calls HEAD, or at RELEASE_BRANCHES' branch for it
call :branchof %2
set "BOPT="
if not "%BRANCH%"=="" set "BOPT=--branch %BRANCH%"
%GIT% clone -q %BOPT% "https://github.com/Ghulamrs/%2.git" "%W%\%1" || exit /b 1
for /f %%b in ('git -C "%W%\%1" rev-parse --abbrev-ref HEAD') do set "BR=%%b"
for /f %%h in ('git -C "%W%\%1" rev-parse HEAD') do (echo   %1  %%h  !BR!& echo %1  %%h  !BR!  github.com/Ghulamrs/%2>> "%W%\RELEASE.txt.repos")
exit /b 0

:branchof
rem repository - BRANCH set to its branch in RELEASE_BRANCHES, or empty
set "BRANCH="
for %%p in (%RELEASE_BRANCHES%) do for /f "tokens=1,2 delims=:" %%a in ("%%p") do if /i "%%a"=="%1" set "BRANCH=%%b"
exit /b 0
