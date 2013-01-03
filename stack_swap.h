#pragma once

#include <cstddef>


namespace stack
{
struct stack_context
{
	stack_context(void * stack, size_t stack_size, void (* function)(void *), void * function_argument);
	void switch_into();
	void switch_out_of();

private:
	void * caller_stack_top;
	void * my_stack_top;

	void * stack;
	size_t stack_size;
	void (* function)(void *);
	void * function_argument;

	void ensure_alignment();

	// intentionally left unimplemented. there is a this pointer stored on the
	// stack and as such not even moving makes sense, because then that this
	// pointer would point to the old address
	stack_context(const stack_context &);
	stack_context & operator=(const stack_context &);
	stack_context(stack_context &&);
	stack_context & operator=(stack_context &&);
};

}
