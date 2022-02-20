# Singularity

## Prerequisite
To use and build this library you will have to have the following installed:
- vc143 (Packaged with Visual Studio or BuildTools)

To build the dependencies of this library you will also need:
- Dedicated vs19-vc142 Tools (either VisualStudio-2019 or BuildsTools-2019 to build xed)
- Python version 2.7 / 3.4 or higher (required by mbuild for xed)
- intelxed/mbuild (https://github.com/intelxed/mbuild)
- CMake (used by glfw, fmt, spdlog)

~~Additionally you will have to register your `python`, `git` and `vcvars*.bat` binaries in the `PATH` environment variable,\
if you want to manually build the dependencies.\
(these are required in bootstrap.bat, so its fine if they are local to the env of the prompt executing the script):~~

## How to build
To build the repository without building its dependencies simply run the self extracting dependecy package.\
You can then build the library either through Visual Studio or the commandline.\
The following configurations are possible and can be used: (TO BE IMPLEMENTED)
- `Executable` : A standalone executable implementing a GUI as well as a CLI ui/ux
- `DynamicLib` : Builds dynamic link library that can be imported by your application to interface with singularity
- `StaticLib`  : Same As Dynamic lib, just self contained as a static link library
- Can be build in both DEBUG and RELEASE versions (runtimes are always release)

To build the dependencies you need to recursively clone this repository, 
then run and hope that bootstrap succefully builds the dependencies.\
After that the library can be build as usual / build as described above.\
```
; Example build with dependencies
git clone --recurse-submodules https://github.com/Lima-X/Singularity
.\Singularity\bootstrap
vcvars64
msbuild .\Singularity\Singularity.sln -p:Configuration=Executable;Platform=x64Release

; Exmaple build using prebuild dependencies
sfx.exe
vcvars64
msbuild .\Singularity\Singularity.sln -p:Configuration=Executable;Platform=x64Release
```

## Features
- VisualSingualrity based on a DearImgui user interface, with tons of information on the way
    - Tons of High- and Lowlevel information about the loaded module
    - Timespooling, virtually slow down the processed of VisualSingularity (may not be efficient)
    - Multi- and Singlethreaded processing module to mutate and obfuscate binaries
- LibSingularity a raw C++20 module based library interface to control low level primatives
    - Highly extensible and easy to use (mostly) framework
    - Allows for multiple custom passes to be applied on a low level IR
    - Multithreading capable, actual multihreading support has to be provided by implementor
    - High memory efficiency, the framework tries to be low on memory allocations
    - MS-DIA driven symbol parser, used to more accurately lift the image (if pdb is available)
    - Recursive descent driven disassembler engine
    - Low level and highly universla cfg based IR with plenty of meta data

## Decompiler heuristics support list

- [x] `int 29h` : (RtlFailFast Exception), interpret as function exit point
- [ ] `jmp [/r]` and `jmp /r`: runtime dependent jump used for jumptables\
      This requires multiple heuristics to find and locate the upper and lower bounds of a jumptable
- [ ] `jmp remote` : tail function call, interpret as exit point
- [ ] `push ?; ret` : emulated call, interpret as call instead of ret (HIGH RISK FACTOR)\
      edit: will not be supported, simply doesnt add value to the framework as clean code is expected
- [x] `IDAD` : impossible disassembly detection\
      Detects if there are defects such as overlaying instructions within the code of a frame

## Limitations and requirements

Due to compilation being a lossy process and relying on debug symbols not being enough / a bad design choice,
several or rather all of the below restrictions / requirements must apply on any image passed to the framework.\
(This framework is not a deobfuscator, it expects cleancode and shits horrifying code out.)

- Functions must be selfcontained, the only entrypoint for a function that should be obfuscated is the head of the function.\
  Any function that has multiple entry points will be eliminated from the obfuscation pool in post, therefore leave a deadspace.
- All instructions decoded must have a unique area in the image that may not overlay with other instructions,\
  if IDAD is discoverd the containing function is purged from the obfuscation pool or the image may be declared invalid.
- SEH is currently not supported, any obfuscated frame will not propagate exceptions and is unable to catch them.
- Providing a pdb along side the framework can greatly impove code discovery, and result in a more obfuscated image.
- The image is expected to provide safeseh / exception data, this is used to improve code discovery and guarantee a correct frame size.
