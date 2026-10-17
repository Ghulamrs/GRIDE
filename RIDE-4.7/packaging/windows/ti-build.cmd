@echo off
rem ti-build.cmd <source.c|.cpp|.shl> - a REAL TMS320C674x build (.out + .hex).
rem
rem License-safe: this ships NO Texas Instruments binaries. It FINDS a TI Code
rem Generation Tools install already on the machine and uses it. Our compiler
rem (c90/cpp11/shalimar) emits the C6000 assembly; TI's cl6x/asm6x assemble it,
rem lnk6x links against TI's runtime, hex6x makes the Intel .hex. As against the
rem vm6747 emulator, which runs the .s directly and needs no TI install.
rem
rem TI CGT is found via, in order: %RIDE_TI_CGT% (a CGT root or its bin),
rem cl6x on PATH, or C:\ti\ccs*\tools\compiler\ti-cgt-c6000_*. Get it free from
rem TI ("C6000 Code Generation Tools") if none is present.
setlocal enabledelayedexpansion
if "%~1"=="" (echo usage: ti-build ^<source.c^|.cpp^|.shl^> & exit /b 2)
set SRC=%~1
set STEM=%~n1
set EXT=%~x1
set SELF=%~dp0
rem --- our compilers: beside this script's bin (installed: bin\ti\ -> bin\) ---
set OURBIN=%SELF%..
if not exist "%OURBIN%\c90.exe" set OURBIN=%SELF%..\bin
if not exist "%OURBIN%\c90.exe" set OURBIN=C:\Users\GRA\source\RIDE\bin

rem --- find TI CGT ---
set CGT=
if not "%RIDE_TI_CGT%"=="" (
  if exist "%RIDE_TI_CGT%\bin\cl6x.exe" set CGT=%RIDE_TI_CGT%
  if exist "%RIDE_TI_CGT%\cl6x.exe" set CGT=%RIDE_TI_CGT%\..
)
if "%CGT%"=="" for %%D in (C:\ti\ccsv7 C:\ti\ccsv8 C:\ti\ccs1200 C:\ti\ccs) do (
  for /d %%C in ("%%D\tools\compiler\ti-cgt-c6000_*") do if exist "%%C\bin\cl6x.exe" set CGT=%%C
)
if "%CGT%"=="" (
  echo TI CGT not found. Install the free "C6000 Code Generation Tools" from
  echo Texas Instruments, or set RIDE_TI_CGT to its folder. The vm6747
  echo emulator still runs tms6747 programs without it.
  exit /b 3
)
set CGTBIN=%CGT%\bin
echo using TI CGT: %CGT%

rem --- the EH runtime, built once from THIS machine's CGT into a user cache ---
set CACHE=%LOCALAPPDATA%\RIDE\tilib
if not exist "%CACHE%\rts6740_elf_eh.lib" (
  echo building the C6000 exception-handling runtime once ^(may take a minute^)...
  if not exist "%CACHE%" mkdir "%CACHE%"
  set SAVED=!PATH!
  set PATH=%CGTBIN%;%CGT%\..\..\..\utils\bin;C:\Program Files\Git\usr\bin;!PATH!
  pushd "%CGT%\lib"
  mklib.exe --pattern=rts6740_elf_eh.lib --index="%CGT%\lib\libc.a" --compiler_bin_dir="%CGTBIN%" --install_to="%CACHE%" --parallel=4 > "%CACHE%\mklib.log" 2>&1
  popd
  set PATH=!SAVED!
  if not exist "%CACHE%\rts6740_elf_eh.lib" (echo RUNTIME_BUILD_FAILED - see "%CACHE%\mklib.log" & exit /b 1)
)

set PATH=%CGTBIN%;%PATH%
set LNKCMD=%SELF%ti-link.cmd

rem --- 1. our compiler emits tms6747 assembly ---
set CC="%OURBIN%\c90.exe"
set LANG=c
if /I "%EXT%"==".cpp" (set LANG=cpp& set CC="%OURBIN%\cpp11.exe")
if /I "%EXT%"==".cc"  (set LANG=cpp& set CC="%OURBIN%\cpp11.exe")
if /I "%EXT%"==".shl" (set LANG=shl& set CC="%OURBIN%\shalimar.exe")
if "%LANG%"=="shl" (%CC% -S --target=tms6747 -nologo "%SRC%" -o "%STEM%.s") else (%CC% -S -arch tms6747 -nologo "%SRC%" -o "%STEM%.s")
if errorlevel 1 (echo COMPILE_FAILED & exit /b 1)

rem --- 2. cl6x assembles our .s ---
cl6x -mv6740 --abi=eabi -c "%STEM%.s" --output_file="%STEM%.obj" > "%STEM%.asm.log" 2>&1
if errorlevel 1 (echo ASM_FAILED & type "%STEM%.asm.log" & exit /b 1)

rem --- Shalimar: assemble its C6000 runtime beside the program ---
set RTOBJS=
if "%LANG%"=="shl" (
  if not exist rtobj mkdir rtobj
  for %%f in ("%OURBIN%\lib\shmrt-tms6747\*.s") do (
    cl6x -mv6740 --abi=eabi -c "%%f" --output_file="rtobj\%%~nf.obj" > "rtobj\%%~nf.log" 2>&1
    if errorlevel 1 (echo RT_ASM_FAILED %%f & exit /b 1)
    set RTOBJS=!RTOBJS! "rtobj\%%~nf.obj"
  )
)

rem --- 3. lnk6x links a real ELF .out ---
lnk6x -mv6740 --abi=eabi -i "%CACHE%" "%LNKCMD%" "%STEM%.obj" !RTOBJS! -l rts6740_elf_eh.lib -o "%STEM%.out" > "%STEM%.lnk" 2>&1
if errorlevel 1 (echo LINK_FAILED & type "%STEM%.lnk" & exit /b 1)

rem --- 4. hex6x makes an Intel .hex ---
hex6x --intel --romwidth=8 --memwidth=8 "%STEM%.out" -o "%STEM%.hex" > "%STEM%.hex.log" 2>&1
if errorlevel 1 (echo HEX_FAILED & type "%STEM%.hex.log" & exit /b 1)

echo TI_BUILD_OK
for %%A in ("%STEM%.out") do @echo   out: %%~nxA  %%~zA bytes
for %%A in ("%STEM%.hex") do @echo   hex: %%~nxA  %%~zA bytes
endlocal
