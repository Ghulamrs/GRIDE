@echo off
setlocal
rem  The product's name, once, as product.props and make-setup.ps1 spell it.
set "PRODUCT=RIDE"
set SRC=%~1
set CPP=%~2
set STAGE=%~3
rem The C compiler clone, for lib\ - c90's headers. Beside the C++ one unless named.
set CC=%~4
if "%CC%"=="" set "CC=%CPP%\..\Compiler-Ci"
rem The programs: %SRC%\bin unless a fifth argument names the solution's OutDir (the Installer project).
set "BIN=%~5"
if "%BIN%"=="" set "BIN=%SRC%\bin"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\bin" "%STAGE%\bin\lib"
for %%f in (%PRODUCT%.exe %PRODUCT%Console.exe c90.exe cpp11.exe shalimar.exe vm6747.exe sim6747.exe asm6x.exe masm.exe link.exe lnk6x.exe c2s.exe) do (
  if exist "%BIN%\%%f" copy /y "%BIN%\%%f" "%STAGE%\bin\" >nul
)
rem The command prompt with bin on PATH: the setup changes no PATH of its own.
copy /y "%SRC%\packaging\windows\ride-prompt.cmd" "%STAGE%\bin\" >nul
if exist "%BIN%\lib\*.lib" copy /y "%BIN%\lib\*.lib" "%STAGE%\bin\lib\" >nul
if exist "%BIN%\lib\shmrt-tms6747" xcopy /e /i /q "%BIN%\lib\shmrt-tms6747" "%STAGE%\bin\lib\shmrt-tms6747" >nul
if exist "%BIN%\lib\shmrt-tms6747-debug" xcopy /e /i /q "%BIN%\lib\shmrt-tms6747-debug" "%STAGE%\bin\lib\shmrt-tms6747-debug" >nul
if exist "%BIN%\lib\rts6x-tms6747" xcopy /e /i /q "%BIN%\lib\rts6x-tms6747" "%STAGE%\bin\lib\rts6x-tms6747" >nul
rem include\ is cpp11's - its C++ headers and the C ones they wrap, in one
rem directory; lib\ is c90's. Each compiler looks one directory above its
rem bin\ for its own, and settings.json beside them says so for the editor.
if exist "%CPP%\include" xcopy /e /i /q "%CPP%\include" "%STAGE%\include" >nul
if exist "%CPP%\lib\*.h" copy /y "%CPP%\lib\*.h" "%STAGE%\include\" >nul
if exist "%CC%\lib" xcopy /e /i /q "%CC%\lib" "%STAGE%\lib" >nul
rem The sample programs: the editor copies them into Documents\RIDE\programs on first use.
if exist "%SRC%\programs" xcopy /e /i /q "%SRC%\programs" "%STAGE%\programs" >nul
rem The assembler and the two linkers are the project's own, beside the
rem editor, named relative to this file so the installation can be put
rem anywhere; the editor makes each absolute against the file. They are the
rem tools by default; when one of them fails a build and the compilers found
rem no fault in the source, "askNative": true has the editor ask whether to
rem use the vendor's (ml64 and link.exe, TI's lnk6x) for that build - false
rem never asks, and the build fails as it failed. The user's design.
rem settings.json is a file of the repository, packaging\windows\settings.json, so
rem what is installed can be read and diffed before an installer ever runs.
copy /y "%SRC%\packaging\windows\settings.json" "%STAGE%\settings.json" >nul
rem the sample projects (a .pro with its files in its own directory, e.g.
rem projects\compilerpp) - shipped whole into {app}\projects; the editor copies
rem them into Documents\RIDE\projects on first use, where a build can write
if exist "%SRC%\projects" xcopy /e /i /q "%SRC%\projects" "%STAGE%\projects" >nul
if exist "%SRC%\help" xcopy /e /i /q "%SRC%\help" "%STAGE%\help" >nul
if exist "%SRC%\docs" xcopy /e /i /q "%SRC%\docs" "%STAGE%\docs" >nul
if exist "%SRC%\README.md" copy /y "%SRC%\README.md" "%STAGE%\" >nul
echo staged files:
dir /s /b "%STAGE%" | find /c /v ""
endlocal
