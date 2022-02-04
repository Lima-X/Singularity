; This file implements asm stub functions for more precise testing cases,
; specifically edgecases that may be hard to reproduce with a c++ compiler

; includelib lvmsdk64.lib
extern SingularityVirtualCodeBegin : proc
extern SingularityVirtualCodeEnd   : proc

.code 
StubTestFunction proc FRAME
	.endprolog
	mov eax, 5
	loophead:
		dec eax
		test eax, eax
		jnz loophead
	loopend:
		ret
StubTestFunction endp
end
