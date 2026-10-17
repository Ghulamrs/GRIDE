@echo off
rem A command prompt with RIDE's programs on PATH: the setup changes no PATH of its own.
set "PATH=%~dp0;%PATH%"
cd /d "%USERPROFILE%\Documents"
cmd /k echo RIDE's tools are on PATH here: c90 cpp11 shalimar masm asm6x lnk6x vm6747 sim6747 c2s
