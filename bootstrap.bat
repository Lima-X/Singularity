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
git clone https://github.com/intelxed/mbuild
python .\xed\mfile.py --install-dir=".\xed\out" install
rd /s /q obj
rd /s /q mbuild
call vcvars64.bat

rem If fmt's cmake is unmodified it will be patched and a guard file for checking will be created.
rem This is a pretty shitty hack to make fmt correctly build with the proper runtime for this project,
rem but manually letting the user edit the project also sucks, so what gives.
rem Then build fmt's static library
if not exist ".\fmt\SINGULARITYCMAKE" (
	echo set_property(TARGET fmt PROPERTY ^
	MSVC_RUNTIME_LIBRARY "MultiThreaded")>>".\fmt\CMakeLists.txt"
	copy /y nul ".\fmt\SINGULARITYCMAKE">nul
)
cd ".\fmt" && cmake .
msbuild ".\fmt.vcxproj" -p:Configuration=Release

rem Following spdlog can be build afterwards
cd "..\spdlog" && cmake .
msbuild ".\spdlog.vcxproj" -p:Configuration=Release

rem Additionally the sdk for importing projects will be "build" (more like prepared)
cd ..
lib /nologo /machine:x64 /def:".\lvmlib64.def"
del /f /q ".\lvmlib64.exp"

rem Restore original processor environment and exit
cd %OriginalWorkingDirectory%
endlocal
