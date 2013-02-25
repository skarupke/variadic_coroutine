#pragma once

#include <iosfwd>

#ifdef _MSC_VER
#define MULTILINE_MACRO_BEGIN \
__pragma(warning(push))\
__pragma(warning(disable : 4127))\
	if (true)\
	{\
__pragma(warning(pop))

#define MULTILINE_MACRO_END \
	}\
	else static_cast<void>(0)
#else
#define MULTILINE_MACRO_BEGIN \
	if (true)\
	{

#define MULTILINE_MACRO_END \
	}\
	else static_cast<void>(0)
#endif


class CoroutineState
{
public:
	CoroutineState(std::istream & stored_values);

	bool AdvanceToValue(const char * name);

	template<typename T>
	T GetNextValue()
	{
		T to_return;
		stored_values >> to_return;
		AdvanceToNextStoredValue();
		return to_return;
	}
	
	struct CreatedValue
	{
		const char * const name;
		template<typename T>
		inline CreatedValue(CoroutineState & parent, const char * name, const T & value)
			: name(name), value(&value), store(&StoreValue<T>)
			, previous_next(parent.created_values_head ? &parent.created_values_last->next : &parent.created_values_head)
			, next(nullptr)
		{
			*previous_next = this;
			parent.created_values_last = this;
		}
		inline ~CreatedValue()
		{
			*previous_next = nullptr;
		}

		void Store(std::ostream & lhs) const;

	private:
		friend class CoroutineState;
		CreatedValue ** previous_next;
		CreatedValue * next;
		const void * const value;
		void (* const store)(std::ostream &, const void *);

		static void WriteSeparator(std::ostream & lhs);

		template<typename T>
		static void StoreValue(std::ostream & lhs, const void * void_value)
		{
			const T & value = *static_cast<const T *>(void_value);
			lhs << value;
			WriteSeparator(lhs);
		}
	};


	template<typename T>
	CreatedValue KeepReference(const char * name, T & reference)
	{
		return CreatedValue(*this, name, reference);
	}
	void Store(std::ostream &) const;

private:
	void AdvanceToNextStoredValue();

	std::istream & stored_values;
	CreatedValue * created_values_head;
	CreatedValue * created_values_last;
};

#define CORO_CONCAT2(x, y) x ## y
#define CORO_CONCAT(x, y) CORO_CONCAT2(x, y)
#define CORO_SERIALIZABLE2(state, type, name, initial_value)\
	CoroutineState & CORO_CONCAT(_state_, name) = state;\
	type name = CORO_CONCAT(_state_, name).AdvanceToValue(#name) ? CORO_CONCAT(_state_, name).GetNextValue<type>() : initial_value;\
	auto CORO_CONCAT(_scope_, name) = CORO_CONCAT(_state_, name).KeepReference(#name, name)
#define CORO_SERIALIZABLE(state, type, name, initial_value) CORO_SERIALIZABLE2(state, type, name, initial_value)

#define CORO_RUN_ONCE(state, ...)\
	MULTILINE_MACRO_BEGIN\
	CORO_SERIALIZABLE(state, bool, CORO_CONCAT(_run_once_, __LINE__), true);\
	if (CORO_CONCAT(_run_once_, __LINE__))\
	{\
		CORO_CONCAT(_run_once_, __LINE__) = false;\
		__VA_ARGS__\
	}\
	MULTILINE_MACRO_END
