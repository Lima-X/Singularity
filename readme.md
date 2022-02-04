# Singularity

## Prerequisite
To use and build this library you will have to have the following installed:
- Python version 2.7 / 3.4 or higher
- Git
- msbuild (MicrosoftBuildsTools or VisualStudio)
- vc143 (and vc142 if you wanna build xed and possibly also boost if i will ever use that in here)
- CMake

Additionally you will have to register your `python`, `git` and `vcvars*.bat` binaries in the `PATH` environment variable,\
if you want to manually build the dependencies.\
(these are required in bootstrap.bat, so its fine if they are local to the env of the prompt executing the script):

## How to build
Clone the repository with all its dependencies, then run the bootstrap script.\
After that msbuild can be used to build the main project in either `Debug` or `Release`.\
```
git clone --recurse-submodules https://github.com/Lima-X/Singularity
.Singularity\bootstrap
vcvars64
msbuild .Singularity\Singularity.sln -p:Configuration=Release
```

## Decompiler heuristics support list

- [x] `int 29h` : (RtlFailFast Exception), interpret as function exit point
- [ ] `jmp [r/]` : runtime dependent jump used for jumptables
      This requires multiple heuristics to find and locate the upper and lower bounds of a jumptable
- [ ] `jmp remote` : tail function call, interpret as exit point
- [ ] `push ?; ret` : emulated call, interpret as call instead of ret (HIGH RISK FACTOR)
