@echo off
copy sources.nt5 sources
call build.exe -cZ
if exist obj%BUILD_ALT_DIR%\%_BUILDARCH%\ctrl2cap.sys copy obj%BUILD_ALT_DIR%\%_BUILDARCH%\ctrl2cap.sys ctrl2cap.amd.sys
rebase -b 10000 -x symbols.amd -a ctrl2cap.amd.sys
copy ctrl2cap.amd.sys ..\release\.
del ctrl2cap.amd.sys





