// This file serves as a standalone test file for the obfuscation framework to work on.
// It is non optimized and self isolated, does not feature a runtime and is heavioly restricted,
// in order to minimize the size of included code to the bare minimum to optimize testing. 
#include <lvmsdk.h>

#define IN
#define OUT
#define INOUT
#define OPT

int TestFunction(
    IN int Argument
) {
    return Argument * 2;
}

int EntryPoint(
    IN unsigned long long RCX,
    IN unsigned long long RDX,
    IN unsigned long long R8,
    IN unsigned long long R9
) {
    auto ReturnValue = TestFunction(1);
    
    return ReturnValue;
}
