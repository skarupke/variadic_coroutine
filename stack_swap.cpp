#include "stack_swap.h"
#include <cstring>
#include <utility>

namespace stack
{

#ifdef _MSC_VER
extern "C" void switch_to_context(void ** old_stack_top, const void * new_stack_top);
extern "C" void callable_context_start();
#else
asm
(
	"switch_to_context:\n\t"
	"pushq %rbp\n\t"
	"movq %rsp, %rbp\n\t"
	// store rbx and r12 to r15 on the stack. these will be restored
	// after we switch back
	"pushq %rbx\n\t"
	"pushq %r12\n\t"
	"pushq %r13\n\t"
	"pushq %r14\n\t"
	"pushq %r15\n\t"
	"movq %rsp, (%rdi)\n\t" // store stack pointer
	// set up the other guy's stack pointers
	"switch_point:\n\t"
	"movq %rsi, %rsp\n\t"
	// and we are now in the other context
	// restore registers
	"popq %r15\n\t"
	"popq %r14\n\t"
	"popq %r13\n\t"
	"popq %r12\n\t"
	"popq %rbx\n\t"
	"popq %rbp\n\t"
	"retq\n\t" // go to whichever code is used by the other stack
);
asm
(
	"switch_to_callable_context:\n\t"
	"pushq %rbp\n\t"
	"movq %rsp, %rbp\n\t"
	// store rbx and r12 to r15 on the stack. these will be restored
	// after we jump back
	"pushq %rbx\n\t"
	"pushq %r12\n\t"
	"pushq %r13\n\t"
	"pushq %r14\n\t"
	"pushq %r15\n\t"
	"movq %rsp, (%rdi)\n\t" // store stack pointer
	// set up the other guy's stack pointers to make
	// debugging easier
	"movq %rbp, (%rdx)\n\t"
	"movq $switch_point, 8(%rdx)\n\t"
	"jmp switch_point\n\t"
);

asm
(
	"callable_context_start:\n\t"
	"movq 40(%rbx), %rdi\n\t" // function_argument
	"callq *32(%rbx)\n\t" // function
	"movq (%rbx), %rsi\n\t" // this->caller_stack_top
	"jmp switch_point\n\t"
);
#endif

void stack_context::switch_into()
{
#ifdef _MSC_VER
	switch_to_context(&caller_stack_top, my_stack_top);
#else
	size_t other_base = reinterpret_cast<size_t>(stack) + stack_size - sizeof(void *) * 2;
	asm("call switch_to_callable_context"
		: : "D"(&caller_stack_top), "S"(my_stack_top), "d"(other_base));
#endif
}
void stack_context::switch_out_of()
{
#ifdef _MSC_VER
	switch_to_context(&my_stack_top, caller_stack_top);
#else
	asm("call switch_to_context"
		: : "D"(&my_stack_top), "S"(caller_stack_top));
#endif
}

stack_context::stack_context(void * stack, size_t stack_size, void (* function)(void *), void * function_argument)
	: caller_stack_top(nullptr), my_stack_top(nullptr)
	, stack(stack), stack_size(stack_size), function(function), function_argument(function_argument)
{
	ensure_alignment();
#ifdef _MSC_VER
	unsigned char * math_stack = static_cast<unsigned char *>(stack) + stack_size;
	my_stack_top = math_stack - sizeof(void *) // space for return address (initial call)
							- sizeof(void *) * 2 // space for stack info
							- sizeof(void *) * 4 // space for arguments
							- sizeof(void *) * 8 // space for non-volatile integer registers
							//- sizeof(void *) * 2 * 10 // space for non-volatile xmm registers
							//- sizeof(void *) // stack alignment
							;
	void ** initial_stack = static_cast<void **>(my_stack_top);
	// initial_stack[11] to initial_stack[14] are space for arguments. I won't
	// use that space but the calling convention says it has to be there
	initial_stack[10] = &callable_context_start;
	initial_stack[9] = math_stack;
	initial_stack[8] = stack;
	initial_stack[7] = this; // initial rbx
	initial_stack[6] = nullptr; // initial rbp
	initial_stack[5] = nullptr; // initial rdi
	initial_stack[4] = nullptr; // initial rsi
	initial_stack[3] = nullptr; // initial r12
	initial_stack[2] = nullptr; // initial r13
	initial_stack[1] = nullptr; // initial r14
	initial_stack[0] = nullptr; // initial r15
	initial_stack[-1] = nullptr; // stack alignment
	initial_stack[-3] = initial_stack[-2] = nullptr; // initial xmm6
	initial_stack[-5] = initial_stack[-4] = nullptr; // initial xmm7
	initial_stack[-7] = initial_stack[-6] = nullptr; // initial xmm8
	initial_stack[-9] = initial_stack[-8] = nullptr; // initial xmm9
	initial_stack[-11] = initial_stack[-10] = nullptr; // initial xmm10
	initial_stack[-13] = initial_stack[-12] = nullptr; // initial xmm11
	initial_stack[-15] = initial_stack[-14] = nullptr; // initial xmm12
	initial_stack[-17] = initial_stack[-16] = nullptr; // initial xmm13
	initial_stack[-19] = initial_stack[-18] = nullptr; // initial xmm14
	initial_stack[-21] = initial_stack[-20] = nullptr; // initial xmm15
#else
	unsigned char * math_stack = static_cast<unsigned char *>(stack) + stack_size;
	my_stack_top = math_stack - sizeof(void *) * 9;
	void ** initial_stack = static_cast<void **>(my_stack_top);
	initial_stack[8] = nullptr; // will store the return address here to make the debuggers life easier
	initial_stack[7] = nullptr; // will store rbp here to make the debuggers life easier
	asm("movq $callable_context_start, %0\n\t" : : "m"(initial_stack[6]));
	initial_stack[5] = &initial_stack[7]; // initial rbp
	initial_stack[4] = this; // initial rbx
	initial_stack[3] = nullptr; // initial r12
	initial_stack[2] = nullptr; // initial r13
	initial_stack[1] = nullptr; // initial r14
	initial_stack[0] = nullptr; // initial r15
#endif
}

void stack_context::ensure_alignment()
{
	static const size_t CONTEXT_STACK_ALIGNMENT = 16;
	// if the user gave me a non-aligned stack, just cut a couple bytes off from the top
	stack_size -= reinterpret_cast<size_t>(static_cast<unsigned char *>(stack) + stack_size) % CONTEXT_STACK_ALIGNMENT;
}


}
