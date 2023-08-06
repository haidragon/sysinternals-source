copy %BASEDIR%\lib\i386\free\secsys .
rebase -b 8000000 -x dump -a secsys
copy *.sys ..\gui\.
copy *.sys ..\gui\release\.
copy *.sys ..\gui\debug\.

