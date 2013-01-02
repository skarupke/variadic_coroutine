#include "coroutine.h"
#include <iostream>
#include <string>

#ifdef _DEBUG
#define SS_ASSERT(cond) if (!(cond)) __debugbreak(); else static_cast<void>(0)
#else
#define SS_ASSERT(cond) static_cast<void>(0)
#endif

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


int main()
{
	using namespace coro;
	{
		int called = 0;
		coroutine<void ()> test([&called](coroutine<void ()>::self & self)
		{
			++called;
			self.yield();
			++called;
		});
		SS_ASSERT(test);
		test();
		SS_ASSERT(called == 1);
		SS_ASSERT(test);
		test();
		SS_ASSERT(called == 2);
		SS_ASSERT(!test);
	}

	{
		coroutine<int ()> testReturn([](coroutine<int ()>::self & self)
		{
			for (int i = 0; i < 10; ++i)
			{
				self.yield(i);
			}
			return 10;
		});
		for (int i = 0; i < 11; ++i)
		{
			SS_ASSERT(testReturn);
			SS_ASSERT(testReturn() == i);
		}
	}

	{
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
			SS_ASSERT(arg_received == i * 10);
		}
	}

	{
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
			SS_ASSERT(result == i * 100);
			SS_ASSERT(arg_received == i);
		}
	}

	{
		coroutine<void (int, double, int)> testMultipleArgs([](coroutine<void (int, double, int)>::self & self, int a, double d, int b)
		{
			for (int i = 0; i < 10; ++i)
			{
				std::cout << a << ", " << d << ", " << b << std::endl;
				std::tie(a, d, b) = self.yield();
			}
		});

		for (int i = 0; testMultipleArgs; ++i)
		{
			testMultipleArgs(i, i / 3.0, i * 10);
		}
	}

	{
		coroutine<std::string (int, double, int)> testMultipleArgsWithReturn([](coroutine<std::string (int, double, int)>::self & self, int a, double d, int b)
		{
			for (int i = 0; i < 10; ++i)
			{
				std::tie(a, d, b) = self.yield("Hello, World!" + i);
			}
			return "Hello, World!" + 10;
		});
		for (int i = 0; testMultipleArgsWithReturn; ++i)
		{
			std::cout << testMultipleArgsWithReturn(i, i / 3.0, i * 10) << std::endl;
		}
	}

	{
		coroutine<void ()> call_from_within_test([](coroutine<void ()>::self & self)
		{
			coroutine<void ()> inner([](coroutine<void ()>::self & self)
			{
				for (int i = 0; i < 3; ++i)
				{
					std::cout << "inner " << i << std::endl;
					self.yield();
				}
			});
			for (int i = 0; i < 3; ++i)
			{
				std::cout << "outer " << i << std::endl;
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
	}

	{
		typedef coroutine<int & (int &)> ref_test_t;
		ref_test_t test_ref([](ref_test_t::self & self, int & ref) -> int &
		{
			return std::get<0>(self.yield(ref));
		});
		int a = 0;
		test_ref(a) = 1;
		SS_ASSERT(a == 1);
		test_ref(a) = 2;
		SS_ASSERT(a == 2);
	}

	{
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
		SS_ASSERT(copy_count == 0);
	}

	coroutine<void ()> performance_test([](coroutine<void ()>::self & self)
	{
		for (int i = 0; i < 10000000; ++i)
		{
			self.yield();
		}
	});
	while(performance_test)
	{
		performance_test();
	}

	std::cin.get();
	return 0;
}

