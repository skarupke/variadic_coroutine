#include "coroutine_state.h"
#include "coroutine.h"
#include <istream>
#include <ostream>
#include <iterator>
#include <cassert>

CoroutineState::CoroutineState(std::istream & stored_values)
	: stored_values(stored_values)
	, created_values_head(nullptr)
	, created_values_last(nullptr)
{
}


template<typename InputIterator, typename ForwardIterator>
InputIterator AdvancePastRange(InputIterator begin, InputIterator end, ForwardIterator range_begin, ForwardIterator range_end)
{
	size_t num_elements = std::distance(range_begin, range_end);
	typedef typename std::remove_cv<typename std::remove_reference<decltype(*begin)>::type>::type value_type;
	auto stack_deleter = [num_elements](value_type * elements)
	{
		for (size_t i = 0; i < num_elements; ++i)
		{
			elements[i].~value_type();
		}
	};
	std::unique_ptr<value_type[], decltype(stack_deleter)> read(new (alloca(sizeof(value_type) * num_elements)) value_type[num_elements], stack_deleter);

	ForwardIterator begin_copy = range_begin;
	for (size_t num_read = 0, read_begin = 0;;)
	{
		range_begin = begin_copy;
		bool valid = true;
		for (size_t count = 0; count < num_read; ++count)
		{
			if (read[(read_begin + count) % num_elements] == *range_begin)
			{
				++range_begin;
			}
			else
			{
				valid = false;
				break;
			}
		}
		if (valid) while (*begin == *range_begin)
		{
			read[(read_begin + num_read++) % num_elements] = *begin;
			
			++begin;
			if (num_read == num_elements) return begin;
			if (begin == end) return end;
			++range_begin;
		}
		if (num_read > 0)
		{
			--num_read;
			read_begin = (read_begin + 1) % num_elements;
		}
		else
		{
			++begin;
			if (begin == end) return end;
		}
	}
}

#define CORO_STATE_SEPARATOR "\n\n\n"
namespace
{
	static const char * const separator_begin = CORO_STATE_SEPARATOR;
	static const char * const separator_end = separator_begin + strlen(separator_begin);
}

void CoroutineState::AdvanceToNextStoredValue()
{
	AdvancePastRange(std::istreambuf_iterator<char>(stored_values), std::istreambuf_iterator<char>(), separator_begin, separator_end);
}
bool CoroutineState::AdvanceToValue(const char * name)
{
	for (;;)
	{
		size_t num_read = 0;
		size_t length = strlen(name);
		char read = '\0';
		for (const char * c = name; c != name + length; ++c, ++num_read)
		{
			read = static_cast<char>(stored_values.get());
			if (read != *c) break;
		}
		if (read == std::char_traits<char>::eof()) return false;
		// always unget one character in case we have started reading the
		// separator. the next line will skip past the ungotten char anyway
		stored_values.unget();

		AdvanceToNextStoredValue();
		if (num_read == length) return true;
		AdvanceToNextStoredValue();
	}
}

void CoroutineState::CreatedValue::Store(std::ostream & lhs) const
{
	lhs << name;
	WriteSeparator(lhs);
	store(lhs, value);
}

void CoroutineState::Store(std::ostream & lhs) const
{
	for (const CreatedValue * value = created_values_head; value; value = value->next)
	{
		value->Store(lhs);
	}
}

void CoroutineState::CreatedValue::WriteSeparator(std::ostream & lhs)
{
	lhs << CORO_STATE_SEPARATOR;
}


#ifndef DISABLE_GTEST
#include <gtest/gtest.h>

TEST(advance_past_range, advance)
{
	int a[] = { 1, 1, 3, 1, 3, 4, 1, 1, 3, 4, 5 };
	int b[] = { 1, 1, 3, 4 };
	int c[] = { 4, 1, 1, 1, 3, 4, 5 };
	int d[] = { 7, 1, 1, 3, 5 };

	int * begin = a;
	int * end = a + sizeof(a) / sizeof(*a);
	int * compare_begin = b;
	int * compare_end = b + sizeof(b) / sizeof(*b);
	EXPECT_EQ(end - 1, AdvancePastRange(begin, end, compare_begin, compare_end));

	begin = b;
	end = b + sizeof(b) / sizeof(*b);
	EXPECT_EQ(end, AdvancePastRange(begin, end, compare_begin, compare_end));

	begin = c;
	end = c + sizeof(c) / sizeof(*c);
	EXPECT_EQ(end - 1, AdvancePastRange(begin, end, compare_begin, compare_end));

	begin = d;
	end = d + sizeof(d) / sizeof(*d);
	EXPECT_EQ(end, AdvancePastRange(begin, end, compare_begin, compare_end));
}

TEST(coroutine_state, store_int)
{
	using namespace coro;
	std::stringstream old_state;
	CoroutineState state(old_state);
	auto callable = [](coroutine<int (CoroutineState &)>::self & self, CoroutineState & state) -> int
	{
		CORO_SERIALIZABLE(state, int, i, 0);
		for (; i < 2;)
		{
			self.yield(++i);
		}
		return ++i;
	};
	coroutine<int (CoroutineState &)> to_call(callable);
	EXPECT_EQ(1, to_call(state));
	{
		std::stringstream stored;
		state.Store(stored);
		EXPECT_EQ("i" CORO_STATE_SEPARATOR "1" CORO_STATE_SEPARATOR, stored.str());
	}
	EXPECT_EQ(2, to_call(state));
	{
		std::stringstream stored;
		state.Store(stored);
		EXPECT_EQ("i" CORO_STATE_SEPARATOR "2" CORO_STATE_SEPARATOR, stored.str());
		coroutine<int (CoroutineState &)> another_call(callable);
		CoroutineState copy(stored);
		EXPECT_EQ(3, another_call(copy));
		std::stringstream copy_stored;
		copy.Store(copy_stored);
		EXPECT_EQ("", copy_stored.str());
	}
	EXPECT_EQ(3, to_call(state));
	{
		std::stringstream stored;
		state.Store(stored);
		EXPECT_EQ("", stored.str());
	}
	EXPECT_FALSE(to_call);
}

TEST(coroutine_state, run_once)
{
	using namespace coro;
	std::stringstream old_state;
	CoroutineState state(old_state);
	auto callable = [](coroutine<int (CoroutineState &)>::self & self, CoroutineState & state) -> int
	{
		CORO_RUN_ONCE(state,
		{
			CORO_SERIALIZABLE(state, int, i, 0);
			for (; i < 3; ++i)
			{
				self.yield(i);
			}
		});
		CORO_SERIALIZABLE(state, int, j, 10);
		self.yield(j);
		return 6;
	};
	coroutine<int (CoroutineState &)> to_call(callable);
	EXPECT_EQ(0, to_call(state));
	EXPECT_EQ(1, to_call(state));
	{
		std::stringstream stored;
		state.Store(stored);
		coroutine<int (CoroutineState &)> another_call(callable);
		CoroutineState copy(stored);
		EXPECT_EQ(10, another_call(copy));
		EXPECT_EQ(6, another_call(copy));
		EXPECT_FALSE(another_call);
	}
	EXPECT_EQ(2, to_call(state));
	EXPECT_EQ(10, to_call(state));
	EXPECT_EQ(6, to_call(state));
	EXPECT_FALSE(to_call);
}

TEST(coroutine_state, restore_and_store_again)
{
	using namespace coro;
	auto callable = [](coroutine<int (CoroutineState &)>::self & self, CoroutineState & state) -> int
	{
		CORO_SERIALIZABLE(state, int, i, 0);
		if (i == 0)
		{
			self.yield(++i);
		}
		if (i == 1)
		{
			self.yield(++i);
		}
		if (i == 2)
		{
			self.yield(++i);
		}
		return ++i;
	};
	std::stringstream storage;
	{
		std::stringstream old_state;
		CoroutineState state(old_state);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(1, to_call(state));
		state.Store(storage);
	}
	{
		CoroutineState state(storage);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(2, to_call(state));
		std::stringstream new_storage;
		state.Store(new_storage);
		storage = std::move(new_storage);
	}
	{
		CoroutineState state(storage);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(3, to_call(state));
		std::stringstream new_storage;
		state.Store(new_storage);
		storage = std::move(new_storage);
	}
	{
		CoroutineState state(storage);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(4, to_call(state));
		EXPECT_FALSE(to_call);
	}
}

TEST(coroutine_state, remove_head_of_list)
{
	using namespace coro;
	int some_global = 0;
	auto callable = [&some_global](coroutine<int (CoroutineState &)>::self & self, CoroutineState & state) -> int
	{
		if (some_global == 0)
		{
			++some_global;
			CORO_SERIALIZABLE(state, int, i, 0);
			self.yield(++i);
		}
		CORO_SERIALIZABLE(state, int, j, 1);
		if (some_global == 1)
		{
			++some_global;
			self.yield(++j);
		}
		return ++j;
	};
	std::stringstream storage;
	{
		std::stringstream old_state;
		CoroutineState state(old_state);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(1, to_call(state));
		EXPECT_EQ(2, to_call(state));
		state.Store(storage);
	}
	{
		CoroutineState state(storage);
		coroutine<int (CoroutineState &)> to_call(callable);
		EXPECT_EQ(3, to_call(state));
		EXPECT_FALSE(to_call);
	}
}

#endif
