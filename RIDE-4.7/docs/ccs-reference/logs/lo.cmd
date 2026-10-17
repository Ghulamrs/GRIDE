@echo off
cd /d C:\ti\ccsref
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\wsprobe7 -application com.ti.ccstudio.apps.createProject -ccs.name Probe -ccs.device TMS320C67XX.TMS320C6747 -ccs.cgtVersion 8.2.2 -ccs.outputFormat ELF -ccs.listBuildOptions > lo7.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\wsprobe55 -application com.ti.ccstudio.apps.projectCreate -ccs.name Probe -ccs.device TMS320C67XX.TMS320C6747 -ccs.cgtVersion 7.4.4 -ccs.outputFormat ELF -ccs.listBuildOptions > lo55.txt 2>&1
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\wsprobe7 -application com.ti.ccstudio.apps.createProject -ccs.name Probe -ccs.device TMS320C67XX.TMS320C6747 -ccs.template bogus > tpl7.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\ti\ccsref\wsprobe55 -application com.ti.ccstudio.apps.projectCreate -ccs.name Probe -ccs.device TMS320C67XX.TMS320C6747 -ccs.template bogus > tpl55.txt 2>&1
