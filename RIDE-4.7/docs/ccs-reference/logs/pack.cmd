@echo off
cd /d C:\ti\ccsref
tar -czf ccsref.tgz --exclude=*.obj --exclude=*.out --exclude=*.map --exclude=*_linkInfo.xml --exclude=.metadata --exclude=*.d --exclude=*.pp --exclude=*.d_raw ws7/K6747c ws7/K6747cpp ws55/K6747c ws55/K6747cpp build7-*.txt build55-*.txt create7*.txt create55*.txt create7.cmd create55.cmd build7.cmd build55.cmd help7.txt helpb7.txt help55.txt helpb55.txt lo7.txt lo55.txt tpl7.txt tpl55.txt
