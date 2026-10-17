@echo off
rem Builds WinConsole with MSVC, which is how it is built on the machine it is
rem meant for. There is no make on that box, and none is needed: a couple of
rem dozen translation units and one link.
rem
rem RIDEConsole.exe is this project's console editor on Windows.
rem On Windows RIDE.exe is the window - which is what somebody there runs -
rem and on a Mac or Linux, where there is no window, RIDE.exe is this one. - the same source as
rem ed1 on Linux and macOS, over the Windows half of the terminal, and named for
rem the machine it runs on so that the three variants can be told apart where
rem they are installed. See "The three variants" in the README.
rem
rem   build            builds RIDEConsole.exe
rem   build test       builds it, then builds and runs the unit tests
rem   build session    builds it, then drives the editor itself with keystrokes
rem   build check      both
rem
rem Run it from a Developer Command Prompt, or run it from anywhere and let it
rem find vcvars64 itself.
rem
rem The search is pinned to Visual Studio 2022 - the [17.0,18.0) below. A bare
rem "vswhere -latest" reaches past it to a newer Visual Studio if one is
rem installed, which is not the toolset this is built with.
rem
rem _CRT_SECURE_NO_WARNINGS is defined for the same reason cc1's own project
rem defines it: getenv and strerror are standard C++17, and MSVC's objection to
rem them is house policy rather than a defect to go and fix.
setlocal
rem The product's name, once, as product.props, the Makefile and make-setup.ps1 spell it.
set "PRODUCT=RIDE"
set "PRODUCT_LOWER=ride"

rem Before anything is built, because this one only looks at files. It needs no
rem compiler and no Visual Studio environment, and a check that rebuilds the
rem editor before answering is a check nobody runs.
if "%1"=="confirm" goto :confirm

if not "%VSCMD_ARG_TGT_ARCH%"=="x64" call :findvcvars
if errorlevel 1 goto :fail

if not exist obj mkdir obj

rem **Into bin\, which is where the compilers are.** This built the
rem console editor into the repository root until 2026-08-27, and an editor
rem there has nothing beside it: the solution puts c90.exe, shalimar.exe, c2s.exe
rem and shc's lib\ in bin\ (via /p:OutDir), and RIDE finds what it drives with
rem path::besideProgram before it looks at PATH. So a `build.bat` editor could
rem not compile anything without $CC1 being named, and said so in its own About
rem box - three times over, "not beside this program". The two ways of building
rem the editor here now write to the same directory, which is what make does on
rem Unix with BINDIR.
rem
rem The stale root copy goes with it. Two RIDEConsole.exe in one tree is a
rem tree where nobody can say which one they ran, and this one would be the
rem older every time from now on.
if "%BINDIR%"=="" set BINDIR=bin
if not exist "%BINDIR%" mkdir "%BINDIR%"
if exist %PRODUCT%Console.exe del %PRODUCT%Console.exe

cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /O2 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:%BINDIR%\%PRODUCT%Console.exe /Fo:obj\ ^
   src\main.cpp src\editor.cpp src\buffer.cpp src\compile.cpp src\convert.cpp ^
   src\indent.cpp src\menu.cpp src\tree.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp src\demangle_win.cpp ^
   src\path.cpp src\process.cpp src\debugger.cpp src\settings.cpp src\options.cpp src\about.cpp src\help.cpp ^
   src\ccs\ccsproject.cpp src\ccs\ccsoptions.cpp src\ccs\ccsxml.cpp src\ccs\ccsworkspace.cpp ^
   src\shalimar\channel.cpp src\shalimar\session.cpp ^
   src\terminal_common.cpp ^
   src\terminal_win.cpp
if errorlevel 1 goto :fail

if "%1"=="solution" goto :solution
if "%1"=="gui" goto :gui
if "%1"=="product" goto :product
if "%1"=="test" goto :unit
if "%1"=="check" goto :unit
if "%1"=="session" goto :session
goto :done

:gui
rem The window, which is C++/CLI and which the console build above never
rem compiles. Run from here for the same reason the solution is: msbuild
rem reaches PATH only after vcvars64.bat, which the top of this file has
rem already found, so nothing else has to know where Visual Studio is.
msbuild winforms\RIDEGui.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%CD%\bin\\" /v:minimal
if errorlevel 1 goto :fail
echo built %PRODUCT%.exe (the window)
exit /b 0

:solution
rem **All four programs into one directory.** cc1, shc, this editor's console
rem half and the window, with both editors depending on both compilers so a
rem change to one and the change to the editor that goes with it are a single
rem build. This is what workspace.mk is on Unix.
rem
rem They land in bin\ together, which is the whole point: the editor
rem finds the compilers it drives beside itself before it looks at PATH, so a
rem build that scatters them is a build you cannot run from. Two of the four
rem always landed there; cc1 did not, because cc1.vcxproj set OutDir to its own
rem project directory, and the window did not, because it was not in the
rem solution at all. Both fixed at the source rather than by copying afterwards
rem - see the comments in cc1.vcxproj and shc.vcxproj.
rem
rem Run from here rather than by calling msbuild directly, because msbuild is
rem on PATH only after vcvars64.bat - which the top of this file has already
rem found. One place knows where Visual Studio is.
rem
rem RIDE.sln reaches ..\toolchain\Compilers\Compiler-Ci, ..\toolchain\Compilers\C++Optimize, ..\toolchain\Compilers\Compiler-Si,
rem ..\toolchain\TI\VM6747 and ..\toolchain\Compilers\Converter-C2S - the compilers c90, cpp11 and
rem shalimar, each its own repository since 2026-10-12, with vm6747 the emulator
rem that runs the fourth target - so all of them are laid out beside each other
rem on this machine, as tools/to-windows.sh lays them.
msbuild %PRODUCT%.sln /p:Configuration=Release /p:Platform=x64 "/p:OutDir=%CD%\bin\\" /v:minimal /m
if errorlevel 1 goto :fail
echo built the solution
goto :confirm

:confirm
rem What the editor drives, and the check that it is actually there.
rem
rem The editor links against none of it. It *runs* these, and finds them beside
rem itself, so "built" and "usable" are two different states and nothing
rem checked the second until now. A shc.exe standing without its runtime
rem compiles, writes correct assembly and then dies at the link - which no
rem build and no suite could see, because -S needs no runtime. It waited for
rem somebody to press Run on a Shalimar file, and then read as a broken
rem compiler rather than an incomplete directory.
rem
rem c2s.exe is named too. The Makefile's confirm has always listed it and this
rem did not, so the Windows check was one binary weaker than the Unix one - and
rem the converter is exactly the kind of thing that goes missing, being the one
rem of the four that nothing links and only the Language menu runs.
rem
rem Both runtime archives are named. Debug is the editor's default
rem configuration and links the other one, so checking a single archive would
rem confirm exactly the half that was not about to be used. And the C6000
rem runtime, a directory of assembly cpp11 writes for shalimar's post-build step:
rem the Makefile's DEPENDENCIES has named it since 3.5 and this did not, so a
rem box with both archives and no directory confirmed clean while a Shalimar
rem program for the emulator had nothing to run beside.
if "%BINDIR%"=="" set BINDIR=bin
set MISSING=0
for %%f in (c90.exe cpp11.exe vm6747.exe sim6747.exe asm6x.exe masm.exe link.exe lnk6x.exe shalimar.exe c2s.exe lib\shmrt-x86_64-windows.lib lib\shmrt-x86_64-windows-debug.lib lib\shmrt-tms6747\Runtime.s lib\shmrt-tms6747-debug\Debug.s lib\rts6x-tms6747\rts6x.lib lib\rts6x-tms6747\rts6xd.lib lib\rts6x-tms6747\printf6x.lib lib\rts6x-tms6747\printf6xd.lib lib\rts6x-tms6747\shmrt6x.lib lib\rts6x-tms6747\shmrt6xd.lib) do (
   if exist "%BINDIR%\%%f" (echo   ok       %%f) else (echo   MISSING  %%f& set MISSING=1)
)
if "%MISSING%"=="1" (
   echo.
   echo RIDE is in %BINDIR% without what it drives. Build the solution with
   echo "build.bat solution", or name them with %%CC1%%, %%CXX1%% and %%SHC%%.
   goto :fail
)
echo.
echo RIDE and everything it drives are in %BINDIR%
exit /b 0

:product
rem The product, as against the build: one directory holding what you would
rem actually run, away from the project space it was compiled in. Both Windows
rem variants land here side by side - the console one and the window - and
rem is copied when msbuild has made it and passed over when it has not.
if "%BINDIR%"=="" set BINDIR=bin
set PRODUCT_DIR=%USERPROFILE%\%PRODUCT_LOWER%
rem Emptied first, the same as the Makefile's rule and for the same reason: a
rem binary that was renamed leaves its old self here, and a directory holding
rem two names is one where nobody can say which was run.
rem
rem The two directories this fills, and not %PRODUCT_DIR% itself - see the Makefile,
rem where PRODUCT_DIR is the caller's to set and a wholesale delete is a foot-gun.
rem Here it is fixed, but the two rules are kept the same shape on purpose.
if exist "%PRODUCT_DIR%\bin" rmdir /s /q "%PRODUCT_DIR%\bin"
if exist "%PRODUCT_DIR%\projects" rmdir /s /q "%PRODUCT_DIR%\projects"
if exist "%PRODUCT_DIR%\programs" rmdir /s /q "%PRODUCT_DIR%\programs"
if not exist "%PRODUCT_DIR%\bin" mkdir "%PRODUCT_DIR%\bin"
copy /y "%BINDIR%\%PRODUCT%Console.exe" "%PRODUCT_DIR%\bin\" >nul
if exist "%BINDIR%\%PRODUCT%.exe" copy /y "%BINDIR%\%PRODUCT%.exe" "%PRODUCT_DIR%\bin\" >nul

rem And what the editor drives, from wherever the solution built it. This used
rem to ship the editor alone, so the cc1.exe sitting in that bin\ was whatever
rem somebody had copied there by hand - on this machine a build from four days
rem earlier that nothing refreshed. An editor without its compilers is not a
rem product; it is half of one that fails at the first Ctrl-B.
rem
rem shc's runtime goes too, and into bin\lib\ rather than anywhere tidier,
rem because that is where shc looks: beside its own binary.
if "%BINDIR%"=="" set BINDIR=bin
if exist "%BINDIR%\c90.exe" copy /y "%BINDIR%\c90.exe" "%PRODUCT_DIR%\bin\" >nul
if exist "%BINDIR%\cpp11.exe" copy /y "%BINDIR%\cpp11.exe" "%PRODUCT_DIR%\bin\" >nul
if exist "%BINDIR%\vm6747.exe" copy /y "%BINDIR%\vm6747.exe" "%PRODUCT_DIR%\bin\" >nul
rem The headers go with the compilers, one directory above bin\ - because
rem bin\lib\ is shc's. include\ is cpp11's, its C++ headers and the C ones
rem they wrap in one directory; lib\ is c90's. Each looks there for its own
rem before the paths compiled into it, which name the checkout, and the
rem settings.json beside them tells the editor the same.
if "%CXX1_DIR%"=="" set CXX1_DIR=..\toolchain\Compilers\C++Optimize
if "%CC1_DIR%"=="" set CC1_DIR=..\toolchain\Compilers\Compiler-Ci
if exist "%PRODUCT_DIR%\include" rmdir /s /q "%PRODUCT_DIR%\include"
if exist "%PRODUCT_DIR%\lib" rmdir /s /q "%PRODUCT_DIR%\lib"
if exist "%CXX1_DIR%\include" xcopy /e /i /q "%CXX1_DIR%\include" "%PRODUCT_DIR%\include" >nul
if exist "%CXX1_DIR%\lib\*.h" copy /y "%CXX1_DIR%\lib\*.h" "%PRODUCT_DIR%\include\" >nul
if exist "%CC1_DIR%\lib" xcopy /e /i /q "%CC1_DIR%\lib" "%PRODUCT_DIR%\lib" >nul
rem TI's option definitions, which a CCS project's unstored options are read from (src\ccs\ccsoptions.h).
if not exist "%PRODUCT_DIR%\lib\ccs" mkdir "%PRODUCT_DIR%\lib\ccs"
copy /y docs\ccs-reference\ti-option-definitions\*.tsv "%PRODUCT_DIR%\lib\ccs\" >nul
(
   echo {
   echo   "include": "include",
   echo   "lib": "lib",
   echo   "vcvars": "",
   echo   "compiler": "auto",
   echo   "indent": 4,
   echo   "tabs": false,
   echo   "font": "",
   echo   "includes": [],
   echo   "libraries": []
   echo }
) > "%PRODUCT_DIR%\settings.json"
if exist "%BINDIR%\shalimar.exe" copy /y "%BINDIR%\shalimar.exe" "%PRODUCT_DIR%\bin\" >nul
if exist "%BINDIR%\%PRODUCT%.exe" copy /y "%BINDIR%\%PRODUCT%.exe" "%PRODUCT_DIR%\bin\" >nul
if exist "%BINDIR%\lib\*.lib" (
   if not exist "%PRODUCT_DIR%\bin\lib" mkdir "%PRODUCT_DIR%\bin\lib"
   copy /y "%BINDIR%\lib\*.lib" "%PRODUCT_DIR%\bin\lib\" >nul
)
rem And the C6000 runtime directory, which the emulator takes beside a
rem Shalimar program; the editor looks for it in lib/ beside itself.
if exist "%BINDIR%\lib\shmrt-tms6747" xcopy /e /i /q "%BINDIR%\lib\shmrt-tms6747" "%PRODUCT_DIR%\bin\lib\shmrt-tms6747" >nul
if exist "%BINDIR%\lib\shmrt-tms6747-debug" xcopy /e /i /q "%BINDIR%\lib\shmrt-tms6747-debug" "%PRODUCT_DIR%\bin\lib\shmrt-tms6747-debug" >nul
if exist "%BINDIR%\lib\rts6x-tms6747" xcopy /e /i /q "%BINDIR%\lib\rts6x-tms6747" "%PRODUCT_DIR%\bin\lib\rts6x-tms6747" >nul
copy /y README.md "%PRODUCT_DIR%\" >nul
rem The samples: projects\ (each in its folder, the CCS ones in ccs\) and programs\
rem (single files, with the headers they include). There is no examples\ since 03-10-2026.
xcopy /e /i /q projects "%PRODUCT_DIR%\projects" >nul
xcopy /e /i /q programs "%PRODUCT_DIR%\programs" >nul
echo RIDE is in %PRODUCT_DIR%
goto :done


:unit

rem The CRT's include directory before src: src\process.h has <process.h>'s name, and <thread>
rem asks for the CRT's with angle brackets - winforms\RIDEGui.vcxproj says the same.
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /I "%UniversalCRTSdkDir%Include\%UCRTVersion%\ucrt" /I src /I winforms /Fe:test.exe /Fo:obj\ ^
   tests\test.cpp src\compile.cpp src\convert.cpp src\indent.cpp src\syntax.cpp src\toolchain.cpp ^
   src\json.cpp src\project.cpp src\find.cpp src\buffer.cpp src\utf8.cpp src\workspace.cpp src\symbols.cpp ^
   src\demangle_win.cpp src\path.cpp src\process.cpp src\debugger.cpp ^
   src\settings.cpp src\options.cpp src\about.cpp src\help.cpp ^
   src\ccs\ccsproject.cpp src\ccs\ccsoptions.cpp src\ccs\ccsxml.cpp src\ccs\ccsworkspace.cpp ^
   src\shalimar\channel.cpp src\shalimar\session.cpp ^
   winforms\bridge.cpp
if errorlevel 1 goto :fail
rem %RIDE_TEST_FLAGS% is handed on: --require-tools turns a skip of RIDE's own into a failure (D14).
.\test.exe %RIDE_TEST_FLAGS%
if errorlevel 1 goto :fail
if not "%1"=="check" goto :done

:session
rem Its own object directory. src\shalimar\session.cpp and tests\session.cpp
rem both become session.obj under one /Fo, and the two builds would take it in
rem turns to overwrite each other's - which works, right up until the day
rem something links both.
if not exist obj\harness mkdir obj\harness
cl /nologo /std:c++14 /W4 /WX /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS ^
   /I src /Fe:session.exe /Fo:obj\harness\ tests\session.cpp src\path.cpp
if errorlevel 1 goto :fail
.\session.exe %BINDIR%\%PRODUCT%Console.exe %CC1%
if errorlevel 1 goto :fail

rem The window has to reach main. Its start-up is mixed-mode, and a native
rem global with a destructor anywhere in what it links kills it before main
rem with STATUS_HEAP_CORRUPTION - which no suite saw from 2026-09-18 to the
rem 19th, because none of them ran the window. --version exits before a form.
rem start /wait, because cmd does not wait for a windowed program and its errorlevel stays 0 whatever
rem happens; and a test for not 0, because the heap-corruption exit is negative and `errorlevel 1` misses it.
if not exist %BINDIR%\%PRODUCT%.exe goto :done
start "" /wait %BINDIR%\%PRODUCT%.exe --version
set START_RC=%errorlevel%
if not "%START_RC%"=="0" (echo   %PRODUCT%.exe DOES NOT START - exit %START_RC% & goto :fail)

:done
echo built %BINDIR%\%PRODUCT%Console.exe
exit /b 0

:findvcvars
rem vswhere's answer goes through a file rather than a for/f. A for/f with a
rem quoted program AND quoted arguments loses a quote pair to cmd's own parsing,
rem and the version range here has both.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" echo could not find vswhere.exe & exit /b 1
"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -property installationPath > "%TEMP%\ed1-vspath.txt"
set VSPATH=
set /p VSPATH=<"%TEMP%\ed1-vspath.txt"
del "%TEMP%\ed1-vspath.txt"
if "%VSPATH%"=="" echo could not find Visual Studio 2022 & exit /b 1
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" echo no vcvars64 under %VSPATH% & exit /b 1
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
