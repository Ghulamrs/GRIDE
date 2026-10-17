@echo off
rem Remove everything RIDE and the nine projects it drives build on Windows, and
rem nothing they are made of. The Unix half of this is clean.sh; this is not a translation of it
rem because the two machines do not leave the same things behind.
rem
rem   clean.cmd        clean
rem   clean.cmd -n     say what would go, remove nothing
rem
rem Run it from the RIDE directory. The projects are expected beside it -
rem Compiler-Ci\, C++Optimize\, Compiler-Si\, VM6747\, Converter-C2S\,
rem ASM6x\, MASM\, LINK\ and LNK6x\ - which is what RIDE.sln's ..\ paths require.
rem
rem **Every path here is named, never globbed.** clean.sh can ask git whether a
rem path is source; there is no git on this machine, so the safety has to come
rem from the list itself. `Compiler-C\lib` is sixteen tracked header files and
rem `Compiler-S\lib` is built runtime archives - the same name one directory
rem apart, meaning opposite things - so nothing here removes a directory called
rem lib, and the MSVC build does not produce one at that level in any case.
rem
rem What MSVC leaves that the Makefiles do not: a per-project intermediate
rem directory named after the project (RIDEConsole\, cc1\, shc\, c2s\), the
rem x64\ output tree, and .obj/.pdb/.ilk beside whatever was built by hand.
setlocal EnableDelayedExpansion

set DRY=0
if "%1"=="-n" set DRY=1

set ROOT=%~dp0..
for %%I in ("%ROOT%") do set ROOT=%%~fI

set COUNT=0

echo cleaning under %ROOT%
if "%DRY%"=="1" echo (dry run - nothing will be removed)
echo.

rem ---- the shared object root the Makefiles default to ------------------------
call :dropdir "%ROOT%\build"

rem ---- RIDE ---------------------------------------------------------------
set HERE=%~dp0
if "%HERE:~-1%"=="\" set HERE=%HERE:~0,-1%
for %%I in ("%HERE%") do set RIDENAME=%%~nxI
echo %RIDENAME%:
call :dropdir "%HERE%\x64"
call :dropdir "%HERE%\obj"
call :dropdir "%HERE%\bin"
call :dropdir "%HERE%\dist"
call :dropdir "%HERE%\RIDEConsole"
call :dropdir "%HERE%\winforms\x64"
call :dropdir "%HERE%\winforms\RIDEGui"
call :dropfile "%HERE%\RIDEConsole.exe"
call :dropfile "%HERE%\RIDE.exe"
call :dropfile "%HERE%\test.exe"
call :dropfile "%HERE%\session.exe"
call :dropglob "%HERE%" *.obj
call :dropglob "%HERE%" *.ilk
call :dropglob "%HERE%" *.pdb
echo.

rem ---- the projects RIDE drives, as workspace.mk and RIDE.sln name them -------
rem Each: the x64\ and obj\ trees MSVC leaves, bin\, its program, and what its suites wrote. build\ only
rem where the project holds no source of that name - c90's and cpp11's build is a tracked file.
call :project "Compilers\Compiler-Ci" c90.exe keepbuild
call :project "Compilers\C++Optimize" cpp11.exe keepbuild
call :project "Compilers\Compiler-Si" shalimar.exe
call :project "TI\VM6747" vm6747.exe
call :project "Compilers\Converter-C2S" c2s.exe
call :project "TI\ASM6x" asm6x.exe
call :project "Windows\MASM" masm.exe
call :project "Windows\LINK" link.exe
call :project "TI\LNK6x" lnk6x.exe

if "%DRY%"=="1" (echo %COUNT% would be removed.) else (echo %COUNT% removed.)
exit /b 0

rem ---------------------------------------------------------------------------
:project
set P=%ROOT%\%~1
if not exist "%P%\" (echo %~1: NOT FOUND - not beside RIDE here & echo. & exit /b 0)
echo %~1:
call :dropdir "%P%\x64"
call :dropdir "%P%\obj"
call :dropdir "%P%\bin"
call :dropdir "%P%\msvc\x64"
call :dropdir "%P%\ide\x64"
if not "%~3"=="keepbuild" call :dropdir "%P%\build"
call :dropfile "%P%\%~2"
call :dropouts "%P%"
echo.
exit /b 0

:dropdir
if not exist %1\ exit /b 0
set /a COUNT+=1
if "%DRY%"=="1" (echo   would go %~1) else (rd /s /q %1 & echo   removed  %~1)
exit /b 0

:dropfile
if not exist %1 exit /b 0
set /a COUNT+=1
if "%DRY%"=="1" (echo   would go %~1) else (del /q %1 & echo   removed  %~1)
exit /b 0

rem A glob, but only over the one directory named and only for the extension
rem given - never recursive, because src\ lives below these and holds .cpp that
rem a careless /s would sit next to.
:dropglob
for %%F in ("%~1\%~2") do (
  set /a COUNT+=1
  if "%DRY%"=="1" (echo   would go %%~fF) else (del /q "%%~fF" & echo   removed  %%~fF)
)
exit /b 0

rem The suites' scratch. tests\out and tests\out-<target>, which the shell
rem suites make and which arrive here inside a relayed tarball.
:dropouts
rem cpp11's emit golden is a recorded before-and-after, kept through a clean on purpose.
for /d %%D in ("%~1\tests\out" "%~1\tests\out-*") do (
  if /i not "%%~nxD"=="out-emit.golden" (
    set /a COUNT+=1
    if "%DRY%"=="1" (echo   would go %%~fD) else (rd /s /q "%%~fD" & echo   removed  %%~fD)
  )
)
exit /b 0
