// Software Development Kit - The public sof include file used to obfuscate software,
// this include provides all the application required interfaces to mark code
// and tell sof how to deal with it later during obfuscation passes
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

	// Virtual machine entry-exit fences, these calls indicate the beginning and ending of a virtualized code section.
	// All code enclosed by these calls is up to severe modifications and shall be self contained,
	// as it cannot be referenced by any external code, after being virtualized.
	// All enclosed code paths must match their calls to these functions, a path cannot call the entry with out the exit
	// and vice versa.
	// Enclosed code may not be guaranteed to be virtualized such as code within switch statements,
	// due to code optimizations finding code is done through hysterics which aren't 100% complete.
	__declspec(dllimport)
	void SingularityVirtualCodeBegin(
		void
	);
	__declspec(dllimport)
	void SingularityVirtualCodeEnd(
		void
	);

#ifdef __cplusplus
}
#endif
