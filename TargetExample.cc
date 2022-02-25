// This file serves as a standalone test file for the obfuscation framework to work on.
// It is non optimized and self isolated, does not feature a runtime and is heavily restricted,
// in order to minimize the size of included code to the bare minimum to optimize testing. 
#include <lvmsdk.h>

#define IN
#define OUT
#define INOUT
#define OPT

extern int SwitchSelect;
void Call0();
void Call1();
void Call2();
void Call3();
void Call4();
void Call5();
void Call9();
void MsvcX64JumptableTest() {
	switch (SwitchSelect) {
	case 0: Call0(); break;
	case 1: Call1(); break;
	case 2: Call2(); break;
	case 3: Call3(); break;
	case 4: Call4(); break;
	case 5: Call5(); break;
	default:
		Call9();
	}
}

#pragma optimize("", off)
int TestFunction(
	IN int Argument
) {
	SingularityVirtualCodeBegin();
	auto Result = Argument % 2 ? Argument * 2 : 0;
	SingularityVirtualCodeEnd();
	return Result;
}
int TestFunction2() {
	SingularityVirtualCodeBegin();
	int Result = 0;
	for (int i = 0; i < 5; ++i)
		Result += i;
	SingularityVirtualCodeEnd();
	return Result;
}

int EntryPoint(
	IN unsigned long long RCX,
	IN unsigned long long RDX,
	IN unsigned long long R8,
	IN unsigned long long R9
) {
	auto ReturnValue = TestFunction(1);
	ReturnValue = TestFunction2();

	return ReturnValue;
}
#pragma optimize("", on)
