#pragma once

#include "stack_swap.h"
#include <tuple>
#include <functional>
#include <memory>
#ifndef CORO_NO_EXCEPTIONS
#	include <exception>
#	include <stdexcept>
#endif
#include <cstdint>

#ifndef CORO_DEFAULT_STACK_SIZE
#define CORO_DEFAULT_STACK_SIZE 64 * 1024
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
		coroutine_context(size_t stack_size, void (*coroutine_call)(coroutine_context **))
			: stack(new unsigned char[stack_size])
			, this_indirection(new coroutine_context *(this))
			, stack_context(new stack::stack_context(stack.get(), stack_size, reinterpret_cast<void (*)(void *)>(coroutine_call), this_indirection.get()))
			, returned(false)
		{
		}
		coroutine_context(coroutine_context && other)
			: stack(std::move(other.stack))
			, this_indirection(std::move(other.this_indirection))
			, stack_context(std::move(other.stack_context))
#			ifndef CORO_NO_EXCEPTIONS
				, exception(std::move(other.exception))
#			endif
			, returned(std::move(other.returned))
		{
			*this_indirection = this;
		}
		coroutine_context & operator=(coroutine_context && other)
		{
			stack = std::move(other.stack);
			this_indirection = std::move(other.this_indirection);
			stack_context = std::move(other.stack_context);
#			ifndef CORO_NO_EXCEPTIONS
				exception = std::move(other.exception);
#			endif
			returned = std::move(other.returned);
			*this_indirection = this;
			return *this;
		}

		void operator()()
		{
#			ifdef CORO_NO_EXCEPTIONS
				if (returned) return;
#			else
				if (returned) throw std::runtime_error("This coroutine has already finished");
#			endif

			stack_context->switch_into(); // will continue here if yielded or returned

#			ifndef CORO_NO_EXCEPTIONS
				if (exception)
				{
					std::rethrow_exception(std::move(exception));
				}
#			endif
		}

		void yield()
		{
			stack_context->switch_out_of();
		}

		std::unique_ptr<unsigned char[]> stack;
		std::unique_ptr<coroutine_context *> this_indirection;
		std::unique_ptr<stack::stack_context> stack_context;
#		ifndef CORO_NO_EXCEPTIONS
			std::exception_ptr exception;
#		endif
		bool returned;
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
			coroutine_context::yield();
			return std::move(this->arguments);
		}

	protected:
		coroutine_yielder_base(size_t stack_size, void (*coroutine_call)(coroutine_context **))
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
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(coroutine_context **))
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
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(coroutine_context **))
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

#	ifndef CORO_NO_EXCEPTIONS
		// this is to get around a bug with variadic templates and the catch(...)
		// statement in visual studio (dec 2012)
		template<typename T, typename GetExceptionPointer>
		void run_and_store_exception(T && to_run, GetExceptionPointer && get_exception_pointer)
		{
			try
			{
				to_run();
			}
			catch(...)
			{
				get_exception_pointer() = std::current_exception();
			}
		}
#	endif

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
		template<typename S, typename R, R (*Func)(S &)>
		struct returner;
		template<typename S, typename R, typename... A>
		struct caller;

		typedef returner<Self, Result, &caller<Self, Result, Arguments...>::call> Returner;

	public:
		Result operator()(Arguments... args)
		{
			this->arguments = std::make_tuple(any_storage<Arguments>(std::forward<Arguments>(args))...);
			Super::operator()();
			return Returner::return_result(*this);
		}

	protected:
		coroutine_preparer(std::function<Result (Self &, Arguments...)> func, size_t stack_size)
			: Super(stack_size, reinterpret_cast<void (*)(coroutine_context **)>(&Returner::coroutine_start)), func(std::move(func))
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
		template<typename S, typename R, typename... A>
		struct caller
		{
			static R call(S & this_)
			{
				return unrolling_caller<sizeof...(A)>::call(this_, this_.arguments);
			}

			template<size_t N, typename Dummy = void>
			struct unrolling_caller
			{
				template<typename... UnrolledArguments>
				static Result call(S & this_, std::tuple<any_storage<A>...> & tuple, any_storage<UnrolledArguments> &... arguments)
				{
					return unrolling_caller<N - 1>::call(this_, tuple, std::get<N - 1>(tuple), arguments...);
				}
			};
			template<typename Dummy>
			struct unrolling_caller<0, Dummy>
			{
				static Result call(S & this_, std::tuple<any_storage<A>...> &, any_storage<A> &... arguments)
				{
					return this_.func(this_, static_cast<A>(arguments)...);
				}
			};
		};


		/**
		 * the returner is responsible for returning the result of the coroutine or of yielding
		 * it also provides the entry point for the coroutine
		 */
		template<typename S, typename R, R (*Func)(S &)>
		struct returner
		{
			static R return_result(coroutine_preparer & this_)
			{
				return static_cast<R>(this_.result);
			}

			// this is the function that the coroutine will start off in
			static void coroutine_start(S ** this_)
			{
#				ifndef CORO_NO_EXCEPTIONS
					run_and_store_exception([this_]
					{
#				endif
						R result = Func(**this_);
						// store in a separate line in case the coroutine has been moved
						(*this_)->result = std::forward<R>(result);
#				ifndef CORO_NO_EXCEPTIONS
					},
					[this_]() -> std::exception_ptr &
					{
						// don't dereference before this in case the coroutine
						// has been moved
						return (*this_)->exception;
					});
#				endif
				(*this_)->returned = true;
			}
		};
		/**
		 * specialization for void return
		 */
		template<typename S, void (*Func)(S &)>
		struct returner<S, void, Func>
		{
			static void return_result(coroutine_preparer &)
			{
			}

			// this is the function that the coroutine will start off in
			static void coroutine_start(S ** this_)
			{
#				ifndef CORO_NO_EXCEPTIONS
					run_and_store_exception([this_]
					{
#				endif
						Func(**this_);
#				ifndef CORO_NO_EXCEPTIONS
					},
					[this_]() -> std::exception_ptr &
					{
						// don't dereference before this in case the coroutine
						// has been moved
						return (*this_)->exception;
					});
#				endif
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
