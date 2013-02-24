#include "coroutine.h"
#include <iostream>
#include <string>

#ifdef _MSC_VER
#	ifdef _DEBUG
#		define SS_ASSERT(cond) if (!(cond)) __debugbreak(); else static_cast<void>(0)
#	else
#		define SS_ASSERT(cond) static_cast<void>(0)
#	endif
#else
#	include <cassert>
#	define SS_ASSERT(cond) assert(cond)
#endif
#ifdef _DEBUG
#	define SS_VERIFY(cond) SS_ASSERT(cond)
#else
#	define SS_VERIFY(cond) static_cast<bool>(cond)
#endif

#ifndef DISABLE_GTEST
#include <gtest/gtest.h>
#endif


int main(int argc, char * argv[])
{
#ifndef DISABLE_GTEST
	::testing::InitGoogleTest(&argc, argv);
	SS_VERIFY(!RUN_ALL_TESTS());
#endif
	using namespace coro;

	{
		coroutine<void (int)> coro([](coroutine<void (int)>::self & self, int first_argument)
		{
			std::cout << "first " << first_argument << std::endl;
			std::tuple<int> second_argument = self.yield();
			std::cout << "second " << std::get<0>(second_argument) << std::endl;
			std::tuple<int> third_argument = self.yield();
			std::cout << "third " << std::get<0>(third_argument) << std::endl;
		});
		coro(7);
		coro(5);
		coro(8);
		bool fired = false;
		coro = [&fired](coroutine<void (int)>::self &, int)
		{
			fired = true;
		};
		coro(5);
		SS_ASSERT(fired);
	}

#if 0
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
#endif

	std::cin.get();
	return 0;
}

