@echo off
rem Builds the TargetExample executable used for testing the virtualizer engine.
rem Requires the vsdevenv to be present during call of this script,
rem may eventually update this to get the latest compiler itself using vswhere...

rem Setup build environment and switch to build directory
setlocal
set OriginalWorkingDirectory=%cd%
if not exist "%~dp0\Output" mkdir Output

rem Compile and link TargetExample
cd %~dp0\Output
cl /Od /Oi- /EHsc /fp:precise /GF /GS- /Gu /Gy /I .. /std:c++20 /Zi ^
/nologo /Tp ..\TargetExample.cc /utf-8 /Wall /link ^
/debug:full /dynamicbase /entry:EntryPoint /largeaddressaware ^
/machine:x64 /nodefaultlib /nxcompat /release /subsystem:windows ..\lvmlib64.lib

rem Cleanup some of the files generated during building end restore origional env
del /f /q .\TargetExample.obj
del /f /q .\vc140.pdb
cd %OriginalWorkingDirectory%
endlocal
