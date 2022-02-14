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
.\Singularity\bootstrap
vcvars64
msbuild .\Singularity\Singularity.sln -p:Configuration=Release
```

## Decompiler heuristics support list

- [x] `int 29h` : (RtlFailFast Exception), interpret as function exit point
- [ ] `jmp [r/]` : runtime dependent jump used for jumptables\
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

