.code

stack_switch_finish proc
	; set up the other guy's stack pointers
	mov rsp, qword ptr [rdx]
	; and we are now in the other context
	; restore registers
	movaps xmm15, [rsp - 160]
	movaps xmm14, [rsp - 144]
	movaps xmm13, [rsp - 128]
	movaps xmm12, [rsp - 112]
	movaps xmm11, [rsp - 96]
	movaps xmm10, [rsp - 80]
	movaps xmm9, [rsp - 64]
	movaps xmm8, [rsp - 48]
	movaps xmm7, [rsp - 32]
	movaps xmm6, [rsp - 16]
	add rsp, 8 ; stack alignment
	pop r15
	pop r14
	pop r13
	pop r12
	pop rsi
	pop rdi
	pop rbp
	pop rbx
	ret ; go to whichever code is used by the other stack
stack_switch_finish endp

switch_to_context proc
	; store rbx and r12 to r15 on the stack. these will be restored
	; after we switch back
	push rbx
	push rbp
	push rdi
	push rsi
	push r12
	push r13
	push r14
	push r15
	sub rsp, 8 ; stack alignment
	movaps [rsp - 16], xmm6
	movaps [rsp - 32], xmm7
	movaps [rsp - 48], xmm8
	movaps [rsp - 64], xmm9
	movaps [rsp - 80], xmm10
	movaps [rsp - 96], xmm11
	movaps [rsp - 112], xmm12
	movaps [rsp - 128], xmm13
	movaps [rsp - 144], xmm14
	movaps [rsp - 160], xmm15
	mov qword ptr [rcx], rsp ; store stack pointer
	jmp stack_switch_finish
switch_to_context endp

callable_context_start proc
	mov rcx, qword ptr [rbx + 40] ; function_argument
	call qword ptr [rbx + 32] ; function
	mov rdx, [rbx + 8] ; this->return_context
	jmp stack_switch_finish
callable_context_start endp 

end 
