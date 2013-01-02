#pragma once

#include <cstddef>


namespace stack
{
struct callable_context;

struct context
{
	void switch_to(const context & other);
	void switch_to(const callable_context & other);

protected:
#ifndef _MSC_VER
	void * stack_base;
#endif
	void * stack_top;
};

struct callable_context : context
{
	callable_context(context * return_context, void * stack, size_t stack_size, void (* function)(void *), void * function_argument);

	callable_context(callable_context &&);
	callable_context & operator=(callable_context &&);

private:
	context * return_context;
	void * stack;
	size_t stack_size;
	void (* function)(void *);
	void * function_argument;
	friend context;

	void ensure_alignment();

	// intentionally left unimplemented
	callable_context(const callable_context &);
	callable_context & operator=(const callable_context &);
};

}
