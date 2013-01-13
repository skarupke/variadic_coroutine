#pragma once

#include "stack_swap.h"
#include <tuple>
#include <functional>
#include <memory>
#include <exception>
#include <cstdint>

#ifndef CORO_DEFAULT_STACK_SIZE
#define CORO_DEFAULT_STACK_SIZE 128 * 1024
#endif

namespace coro
{
namespace detail
{
	/**
	 * the coroutine_context holds the stack context and the current state
	 * of the coroutine. it also starts the coroutine
	 */
	struct coroutine_context
	{
	protected:
		coroutine_context(size_t stack_size, void (*coroutine_call)(void *))
			: stack(new unsigned char[stack_size])
			, this_indirection(new coroutine_context *(this))
			, callee(new stack::stack_context(stack.get(), stack_size, coroutine_call, this_indirection.get()))
			, returned(false)
		{
		}
		coroutine_context(coroutine_context && other)
			: stack(std::move(other.stack))
			, this_indirection(std::move(other.this_indirection))
			, callee(std::move(other.callee))
			, exception(std::move(other.exception))
			, returned(std::move(other.returned))
		{
			*this_indirection = this;
		}
		coroutine_context & operator=(coroutine_context && other)
		{
			stack = std::move(other.stack);
			this_indirection = std::move(other.this_indirection);
			callee = std::move(other.callee);
			exception = std::move(other.exception);
			returned = std::move(other.returned);
			*this_indirection = this;
			return *this;
		}

		void operator()()
		{
			if (returned) throw "This coroutine has already finished";

			callee->switch_into(); // will continue here if yielded or returned

			if (exception)
			{
				std::rethrow_exception(std::move(exception));
			}
		}

		std::unique_ptr<unsigned char[]> stack;
		std::unique_ptr<coroutine_context *> this_indirection;
		std::unique_ptr<stack::stack_context> callee;
		std::exception_ptr exception;
		bool returned;

		template<typename T>
		friend void run_and_store_exception(T &&, coroutine_context **);
	};

	// a type that can store both objects and references
	template<typename T>
	struct any_storage
	{
		any_storage()
			: stored()
		{
		}
		any_storage(any_storage && other)
			: stored(std::move(other.stored))
		{
		}
		any_storage & operator=(any_storage && other)
		{
			stored = std::move(other.stored);
			return *this;
		}
		any_storage(T to_store)
			: stored(std::move(to_store))
		{
		}
		any_storage & operator=(T to_store)
		{
			stored = std::move(to_store);
			return *this;
		}
		operator T()
		{
			return std::move(stored);
		}

	private:
		T stored;
#ifdef _MSC_VER
		// intentionally not implemented
		any_storage(const any_storage & other);
		any_storage & operator=(const any_storage & other);
#endif
	};

	// specialization for void
	template<>
	struct any_storage<void>
	{
	};

	// specialization for lvalue references
	template<typename T>
	struct any_storage<T &>
	{
		any_storage()
			: stored(nullptr)
		{
		}
		any_storage(any_storage && other)
			: stored(other.stored)
		{
		}
		any_storage & operator=(any_storage && other)
		{
			stored = other.stored;
			return *this;
		}
		any_storage(T & to_store)
			: stored(&to_store)
		{
		}
		any_storage & operator=(T & to_store)
		{
			stored = &to_store;
			return *this;
		}
		operator T &()
		{
			return *stored;
		}

	private:
		T * stored;
#ifdef _MSC_VER
		// intentionally not implemented
		any_storage(const any_storage & other);
		any_storage & operator=(const any_storage & other);
#endif
	};

	// specialization for rvalue references
	template<typename T>
	struct any_storage<T &&>
	{
		any_storage()
			: stored(nullptr)
		{
		}
		any_storage(any_storage && other)
			: stored(other.stored)
		{
		}
		any_storage & operator=(any_storage && other)
		{
			stored = other.stored;
			return *this;
		}
		any_storage(T && to_store)
			: stored(&to_store)
		{
		}
		any_storage & operator=(T && to_store)
		{
			stored = &to_store;
			return *this;
		}
		operator T &&()
		{
			return *stored;
		}

	private:
		T * stored;
#ifdef _MSC_VER
		// intentionally not implemented
		any_storage(const any_storage & other);
		any_storage & operator=(const any_storage & other);
#endif
	};


	template<typename Result, typename... Arguments>
	struct coroutine_yielder_base
		: protected coroutine_context
	{
		std::tuple<Arguments...> yield()
		{
			this->callee->switch_out_of();
			return std::move(this->arguments);
		}

	protected:
		coroutine_yielder_base(size_t stack_size, void (*coroutine_call)(void *))
			: coroutine_context(stack_size, coroutine_call)
		{
		}
		coroutine_yielder_base(coroutine_yielder_base && other)
			: coroutine_context(std::move(other))
			, result(std::move(other.result))
			, arguments(std::move(other.arguments))
		{
		}
		coroutine_yielder_base & operator=(coroutine_yielder_base && other)
		{
			coroutine_context::operator=(std::move(other));
			result = std::move(other.result);
			arguments = std::move(other.arguments);
			return *this;
		}

		any_storage<Result> result;
		std::tuple<any_storage<Arguments>...> arguments;
	};

	/**
	 * The coroutine_yielder is responsible for providing a yield
	 * function
	 */
	template<typename Result, typename... Arguments>
	struct coroutine_yielder
		: protected coroutine_yielder_base<Result, Arguments...>
	{
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(void *))
			: coroutine_yielder_base<Result, Arguments...>(stack_size, coroutine_call)
		{
		}
		coroutine_yielder(coroutine_yielder && other)
			: coroutine_yielder_base<Result, Arguments...>(std::move(other))
		{
		}
		coroutine_yielder & operator=(coroutine_yielder && other)
		{
			coroutine_yielder_base<Result, Arguments...>::operator=(std::move(other));
			return *this;
		}
		std::tuple<Arguments...> yield(Result result)
		{
			this->result = std::forward<Result>(result);
			return coroutine_yielder_base<Result, Arguments...>::yield();
		}
	};
	// specialization for void
	template<typename... Arguments>
	struct coroutine_yielder<void, Arguments...>
		: coroutine_yielder_base<void, Arguments...>
	{
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(void *))
			: coroutine_yielder_base<void, Arguments...>(stack_size, coroutine_call)
		{
		}
		coroutine_yielder(coroutine_yielder && other)
			: coroutine_yielder_base<void, Arguments...>(std::move(other))
		{
		}
		coroutine_yielder & operator=(coroutine_yielder && other)
		{
			coroutine_yielder_base<void, Arguments...>::operator=(std::move(other));
			return *this;
		}
	};

	// this is to get around a bug with variadic templates and the catch(...)
	// statement in visual studio (dec 2012)
	template<typename T>
	void run_and_store_exception(T && to_run, coroutine_context ** context)
	{
		try
		{
			to_run();
		}
		catch(...)
		{
			(*context)->exception = std::current_exception();
		}
	}

	/**
	 * The coroutine_prepare exposes the public operator(). It stores the arguments to be passed to the
	 * coroutine, and then hands control off to the coroutine_caller and coroutine_returner
	 */
	template<typename Self, typename Result, typename... Arguments>
	struct coroutine_preparer
		: coroutine_yielder<Result, Arguments...>
	{
	private:
		typedef coroutine_yielder<Result, Arguments...> Super;
		template<Result (*Func)(Self &)>
		struct returner;
		struct caller;

		typedef returner<&caller::call> Returner;

	public:
		Result operator()(Arguments... args)
		{
			this->arguments = std::make_tuple(any_storage<Arguments>(std::forward<Arguments>(args))...);
			Super::operator()();
			return Returner::return_result(*this);
		}

	protected:
		coroutine_preparer(std::function<Result (Self &, Arguments...)> func, size_t stack_size)
			: Super(stack_size, reinterpret_cast<void (*)(void *)>(&Returner::coroutine_start)), func(std::move(func))
		{
		}
		coroutine_preparer(coroutine_preparer && other)
			: Super(std::move(other)), func(std::move(other.func))
		{
		}
		coroutine_preparer & operator=(coroutine_preparer && other)
		{
			Super::operator=(std::move(other));
			func = std::move(other.func);
			return *this;
		}

	private:
		std::function<Result (Self &, Arguments...)> func;

		/**
		 * The caller calls the provided std::function. it is responsible for
		 * unrolling the arguments tuple. it is being called by the returner
		 */
		struct caller
		{
			static Result call(Self & this_)
			{
				return unrolling_caller<sizeof...(Arguments)>::call(this_, this_.arguments);
			}

			template<size_t N, typename Dummy = void>
			struct unrolling_caller
			{
				template<typename... UnrolledArguments>
				static Result call(Self & this_, std::tuple<any_storage<Arguments>...> & tuple, any_storage<UnrolledArguments> &... arguments)
				{
					return unrolling_caller<N - 1>::call(this_, tuple, std::get<N - 1>(tuple), arguments...);
				}
			};
			template<typename Dummy>
			struct unrolling_caller<0, Dummy>
			{
				static Result call(Self & this_, std::tuple<any_storage<Arguments>...> &, any_storage<Arguments> &... arguments)
				{
					return this_.func(this_, std::forward<Arguments>(arguments)...);
				}
			};
		};


		/**
		 * the returner is responsible for returning the result of the coroutine or of yielding
		 * it also provides the entry point for the coroutine
		 */
		template<Result (*Func)(Self &)>
		struct returner
		{
			static Result return_result(coroutine_preparer & this_)
			{
				return std::forward<Result>(this_.result);
			}

			// this is the function that the coroutine will start off in
			static void coroutine_start(Self ** this_)
			{
				run_and_store_exception([this_]
				{
					Result result = std::forward<Result>(Func(**this_));
					(*this_)->result = std::forward<Result>(result);
				}, reinterpret_cast<coroutine_context **>(this_));
				(*this_)->returned = true;
			}
		};
		/**
		 * specialization for void return
		 */
		template<void (*Func)(Self &)>
		struct returner<Func>
		{
			static void return_result(coroutine_preparer &)
			{
			}

			// this is the function that the coroutine will start off in
			static void coroutine_start(Self ** this_)
			{
				run_and_store_exception([this_]{ Func(**this_); }, reinterpret_cast<coroutine_context **>(this_));
				(*this_)->returned = true;
			}
		};
	};
}

template<typename>
struct coroutine;

template<typename Result, typename... Arguments>
struct coroutine<Result (Arguments...)> : detail::coroutine_preparer<coroutine<Result (Arguments...)>, Result, Arguments...>
{
public:
	typedef coroutine<Result (Arguments...)> self;
private:
	typedef detail::coroutine_preparer<self, Result, Arguments...> Super;
public:

	coroutine(std::function<Result (self &, Arguments...)> func, size_t stack_size = CORO_DEFAULT_STACK_SIZE)
		: Super(std::move(func), stack_size)
	{
	}
	coroutine(coroutine && other)
		: Super(std::move(other))
	{
	}
	coroutine & operator=(coroutine && other)
	{
		Super::operator=(std::move(other));
		return *this;
	}

	operator bool() const
	{
		return !this->returned;
	}


private:
	// intentionally not implemented
	coroutine(const coroutine &);
	coroutine & operator=(const coroutine &);
};

}
