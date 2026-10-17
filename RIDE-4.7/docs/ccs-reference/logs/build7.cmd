@echo off
cd /d C:\ti\ccsref
set E=C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws7
for %%P in (K6747c K6747cpp) do for %%C in (Debug Release) do %E% -application com.ti.ccstudio.apps.buildProject -ccs.projects %%P -ccs.configuration %%C -ccs.buildType full > build7-%%P-%%C.txt 2>&1
