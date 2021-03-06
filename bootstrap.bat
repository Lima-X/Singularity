@rem This file is a bootstarp file used for preparing all the dependecies.
@rem git, vcvars*, cmake and python (minimum version 2.7+/3.4+) are required, 
@rem they need to be registered in the PATH environment variable.
@rem Additionally due to xed you will also have to have vc142 installed
@echo off

rem Save current environment to restore later
setlocal
set OriginalWorkingDirectory=%cd%
cd %~dp0


rem Build xed, this has special semantics in the context of this specific script,
rem first we need to clone mbuild for xeds mfile, then we can let xed build.
rem After xed finished mbuild can be reomoved again and vcvars64 will be loaded
if not exist ".\mbuild" (
	git clone https://github.com/intelxed/mbuild
)
cd ".\xed"
python .\mfile.py --install-dir="." install
call vcvars64.bat

rem glfw has to be also build now, as this project is moving to gui for debugging
cd "..\glfw"
cmake . -DUSE_MSVC_RUNTIME_LIBRARY_DLL=OFF
msbuild ".\src\glfw.vcxproj" -p:Configuration=Release

rem The shitty hack has been fixed, this now forces the creation with MT
cd "..\fmt"
cmake . -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded"
msbuild ".\fmt.vcxproj" -p:Configuration=Release

rem Following spdlog can be build afterwards
cd "..\spdlog"
cmake . -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" -DSPDLOG_FMT_EXTERNAL=ON -Dfmt_DIR="..\fmt"
msbuild ".\spdlog.vcxproj" -p:Configuration=Release

rem Additionally the sdk for importing projects will be "build" (more like prepared)
cd ..
lib /nologo /machine:x64 /def:".\lvmlib64.def"
del /f /q ".\lvmlib64.exp"

rem Restore original processor environment and exit
cd %OriginalWorkingDirectory%
endlocal

rem Imgui needs not building, thats handle within the main app itself
