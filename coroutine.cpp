#include "coroutine.h"
#include <cassert>
#include <stdexcept>

namespace coro
{
basic_coroutine::basic_coroutine(size_t stack_size, void (*coroutine_call)(void *), void * initial_argument)
	: stack(new unsigned char[stack_size])
	, stack_size(stack_size)
	, stack_context(new stack::stack_context(stack.get(), stack_size, reinterpret_cast<void (*)(void *)>(coroutine_call), initial_argument))
	, started(false)
	, returned(false)
{
}
basic_coroutine::basic_coroutine(basic_coroutine && other)
	: stack(std::move(other.stack))
	, stack_size(std::move(other.stack_size))
	, stack_context(std::move(other.stack_context))
#	ifndef CORO_NO_EXCEPTIONS
		, exception(std::move(other.exception))
#	endif
	, started(std::move(other.started))
	, returned(std::move(other.returned))
{
	assert(!other.is_running());
}
basic_coroutine & basic_coroutine::operator=(basic_coroutine && other)
{
	assert(!other.is_running());
	stack = std::move(other.stack);
	stack_size = std::move(other.stack_size);
	stack_context = std::move(other.stack_context);
#	ifndef CORO_NO_EXCEPTIONS
		exception = std::move(other.exception);
#	endif
	started = std::move(other.started);
	returned = std::move(other.returned);
	return *this;
}

void basic_coroutine::operator()()
{
#	ifdef CORO_NO_EXCEPTIONS
		assert(!returned);
		if (returned) return;
#	else
		if (returned) throw std::runtime_error("You tried to call a coroutine that has already finished");
#	endif

	stack_context->switch_into(); // will continue here if yielded or returned

#		ifndef CORO_NO_EXCEPTIONS
		if (exception)
		{
			std::rethrow_exception(std::move(exception));
		}
#		endif
}

void basic_coroutine::yield()
{
	stack_context->switch_out_of();
}
bool basic_coroutine::is_running() const
{
	return started && !returned;
}
bool basic_coroutine::has_finished() const
{
	return returned;
}
basic_coroutine::operator bool() const
{
	return !has_finished() && stack;
}


}


#ifndef DISABLE_GTEST
#include <gtest/gtest.h>

TEST(coroutine, simple)
{
	using namespace coro;
	int called = 0;
	coroutine<void ()> test([&called](coroutine<void ()>::self & self)
	{
		++called;
		self.yield();
		++called;
	});
	EXPECT_TRUE(test);
	test();
	EXPECT_EQ(1, called);
	EXPECT_TRUE(test);
	test();
	EXPECT_EQ(2, called);
	EXPECT_FALSE(test);
}

TEST(coroutine, return)
{
	using namespace coro;
	coroutine<int ()> testReturn([](coroutine<int ()>::self & self) -> int
	{
		for (int i = 0; i < 10; ++i)
		{
			self.yield(i);
		}
		return 10;
	});
	for (int i = 0; i < 11; ++i)
	{
		EXPECT_TRUE(testReturn);
		EXPECT_EQ(i, testReturn());
	}
}

TEST(coroutine, argument)
{
	using namespace coro;
	int arg_received = 0;
	coroutine<void (int)> testArgument([&arg_received](coroutine<void (int)>::self & self, int i)
	{
		arg_received = i * 10;
		for (int j = 0; j < 10; ++j)
		{
			arg_received = std::get<0>(self.yield()) * 10;
		}
	});
	for (int i = 0; testArgument; ++i)
	{
		testArgument(i);
		EXPECT_EQ(i * 10, arg_received);
	}
}

TEST(coroutine, arg_and_return)
{
	using namespace coro;
	int arg_received = 0;
	coroutine<int (int)> testBoth([&arg_received](coroutine<int (int)>::self & self, int i) -> int
	{
		arg_received = i;
		for (int j = 0; j < 10; ++j)
		{
			arg_received = std::get<0>(self.yield(arg_received * 100));
		}
		return arg_received * 100;
	});
	for (int i = 0; testBoth; ++i)
	{
		int result = testBoth(i); static_cast<void>(result);
		EXPECT_EQ(i * 100, result);
		EXPECT_EQ(i, arg_received);
	}
}

TEST(coroutine, multiple_args)
{
	using namespace coro;
	coroutine<void (int, double, int)> testMultipleArgs([](coroutine<void (int, double, int)>::self & self, int a, double d, int b)
	{
		for (int i = 0; i < 10; ++i)
		{
			EXPECT_EQ(i, a);
			EXPECT_DOUBLE_EQ(i / 3.0, d);
			EXPECT_EQ(i * 10, b);
			std::tie(a, d, b) = self.yield();
		}
		EXPECT_EQ(10, a);
		EXPECT_DOUBLE_EQ(10 / 3.0, d);
		EXPECT_EQ(100, b);
	});

	for (int i = 0; testMultipleArgs; ++i)
	{
		testMultipleArgs(i, i / 3.0, i * 10);
	}
}

TEST(coroutine, multiple_args_and_return)
{
	using namespace coro;
	coroutine<std::string (int, double, int)> testMultipleArgsWithReturn([](coroutine<std::string (int, double, int)>::self & self, int a, double d, int b) -> std::string
	{
		for (int i = 0; i < 10; ++i)
		{
			EXPECT_EQ(i, a);
			EXPECT_DOUBLE_EQ(i / 3.0, d);
			EXPECT_EQ(i * 10, b);
			std::tie(a, d, b) = self.yield(&"Hello, World!"[i]);
		}
		EXPECT_EQ(10, a);
		EXPECT_DOUBLE_EQ(10 / 3.0, d);
		EXPECT_EQ(100, b);
		return &"Hello, World!"[10];
	});
	for (int i = 0; testMultipleArgsWithReturn; ++i)
	{
		EXPECT_EQ(std::string(&"Hello, World!"[i]), testMultipleArgsWithReturn(i, i / 3.0, i * 10));
	}
}

TEST(coroutine, call_from_within)
{
	using namespace coro;
	std::vector<int> pushed;
	coroutine<void ()> call_from_within_test([&pushed](coroutine<void ()>::self & self)
	{
		coroutine<void ()> inner([&pushed](coroutine<void ()>::self & self)
		{
			for (int i = 0; i < 3; ++i)
			{
				pushed.push_back(1);
				self.yield();
			}
		});
		for (int i = 0; i < 3; ++i)
		{
			pushed.push_back(2);
			while (inner)
			{
				inner();
				self.yield();
			}
		}
	});

	while (call_from_within_test)
	{
		call_from_within_test();
	}
	EXPECT_EQ(6, pushed.size());
	EXPECT_EQ(2, pushed[0]);
	EXPECT_EQ(1, pushed[1]);
	EXPECT_EQ(1, pushed[2]);
	EXPECT_EQ(1, pushed[3]);
	EXPECT_EQ(2, pushed[4]);
	EXPECT_EQ(2, pushed[5]);
}

TEST(coroutine, reference)
{
	using namespace coro;
	typedef coroutine<int & (int &)> ref_test_t;
	ref_test_t test_ref([](ref_test_t::self & self, int & ref) -> int &
	{
		return std::get<0>(self.yield(ref));
	});
	int a = 0;
	test_ref(a) = 1;
	EXPECT_EQ(1, a);
	test_ref(a) = 2;
	EXPECT_EQ(2, a);
}

namespace
{
struct copy_counter
{
	copy_counter(int * copy_count = nullptr)
		: copy_count(copy_count)
	{
	}
	copy_counter(const copy_counter & other)
		: copy_count(other.copy_count)
	{
		++*copy_count;
	}
	copy_counter(copy_counter && other)
		: copy_count(other.copy_count)
	{
	}
	copy_counter & operator=(const copy_counter & other)
	{
		copy_count = other.copy_count;
		++*copy_count;
		return *this;
	}
	copy_counter & operator=(copy_counter && other)
	{
		copy_count = other.copy_count;
		return *this;
	}
private:
	int * copy_count;
};
}

TEST(coroutine, copy_count)
{
	using namespace coro;

	int copy_count = 0;

	typedef coroutine<copy_counter (copy_counter, copy_counter)> coroutine_t;
	coroutine_t test_copies([&copy_count](coroutine_t::self & self, copy_counter, copy_counter) -> copy_counter
	{
		for (int i = 0; i < 10; ++i)
		{
			copy_counter to_yield(&copy_count);
			to_yield = std::get<0>(self.yield(std::move(to_yield)));
		}
		copy_counter to_return(&copy_count);
		return to_return;
	});

	while(test_copies)
	{
		copy_counter returned(&copy_count);
		returned = test_copies(copy_counter(&copy_count), copy_counter(&copy_count));
	}
	EXPECT_EQ(0, copy_count);
}

#ifndef CORO_NO_EXCEPTIONS
TEST(coroutine, exception)
{
	using namespace coro;
	coroutine<void ()> thrower([](coroutine<void ()>::self &)
	{
		throw 10;
	});
	bool thrown = false;
	try
	{
		thrower();
	}
	catch(int i)
	{
		EXPECT_EQ(10, i);
		thrown = true;
	}
	EXPECT_TRUE(thrown);
}
#endif

#endif
