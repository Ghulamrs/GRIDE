@echo off
if not exist C:\ti\ccsref mkdir C:\ti\ccsref
cd /d C:\ti\ccsref
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws7 -application com.ti.ccstudio.apps.projectCreate -ccs.help > help7.txt 2>&1
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws7 -application com.ti.ccstudio.apps.projectBuild -ccs.help > helpb7.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws55 -application com.ti.ccstudio.apps.projectCreate -ccs.help > help55.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\ws55 -application com.ti.ccstudio.apps.projectBuild -ccs.help > helpb55.txt 2>&1
