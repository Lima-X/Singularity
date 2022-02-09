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

StubTestFunction2 proc FRAME
	.endprolog

	jmp loop_content

	loop_repeat:
		xor eax, eax

	loop_content:
		dec eax
		jz  loop_end
		jmp loop_repeat

	loop_end:
		ret

StubTestFunction2 endp
end
