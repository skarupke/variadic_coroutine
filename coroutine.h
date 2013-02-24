#pragma once

#include "stack_swap.h"
#include <tuple>
#include <functional>
#include <memory>
#ifndef CORO_NO_EXCEPTIONS
#	include <exception>
#endif

#ifndef CORO_DEFAULT_STACK_SIZE
#define CORO_DEFAULT_STACK_SIZE 64 * 1024
#endif

namespace coro
{
/**
 * the basic_coroutine is a minimal implementation of a coroutine. it is used
 * by the coroutine class below, and I recommend that you use that one instead
 */
struct basic_coroutine
{
	basic_coroutine(size_t stack_size, void (*coroutine_call)(void *), void * initial_argument);
	// use this only to create from a coroutine that's not already running
	basic_coroutine(basic_coroutine && other);
	// use this only to assign to or from a coroutine that's not already running
	basic_coroutine & operator=(basic_coroutine && other);

	bool is_running() const;
	bool has_finished() const;
	// will return true if you can call this coroutine
	operator bool() const;

	void operator()();
	void yield();

protected:
	std::unique_ptr<unsigned char[]> stack;
	size_t stack_size;
	std::unique_ptr<stack::stack_context> stack_context;
#	ifndef CORO_NO_EXCEPTIONS
		std::exception_ptr exception;
#	endif
	bool started;
	bool returned;
};
namespace detail
{
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
		: basic_coroutine
	{
		std::tuple<Arguments...> yield()
		{
			basic_coroutine::yield();
			return std::move(arguments);
		}

	protected:
		coroutine_yielder_base(size_t stack_size, void (*coroutine_call)(void *), void * initial_argument)
			: basic_coroutine(stack_size, coroutine_call, initial_argument)
		{
		}
		coroutine_yielder_base & operator=(coroutine_yielder_base && other)
		{
			basic_coroutine::operator=(std::move(other));
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
		: coroutine_yielder_base<Result, Arguments...>
	{
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(void *), void * initial_argument)
			: coroutine_yielder_base<Result, Arguments...>(stack_size, coroutine_call, initial_argument)
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
		coroutine_yielder(size_t stack_size, void (*coroutine_call)(void *), void * initial_argument)
			: coroutine_yielder_base<void, Arguments...>(stack_size, coroutine_call, initial_argument)
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
		template<typename T>
		void run_and_store_exception(T && to_run, std::exception_ptr & exception)
		{
			try
			{
				to_run();
			}
			catch(...)
			{
				exception = std::current_exception();
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
		coroutine_preparer(std::function<Result (Self &, Arguments...)> func, size_t stack_size, Self * self)
			: Super(stack_size, reinterpret_cast<void (*)(void *)>(&Returner::coroutine_start), self), func(std::move(func))
		{
		}
		void recreate(std::function<Result (Self &, Arguments...)> func, size_t stack_size, Self * self)
		{
			Super::operator=(Super(stack_size, reinterpret_cast<void (*)(void *)>(&Returner::coroutine_start), self));
			this->func = std::move(func);
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
			static R call(S & self)
			{
				return unrolling_caller<sizeof...(A)>::call(self, self.arguments);
			}

			template<size_t N, typename Dummy = void>
			struct unrolling_caller
			{
				template<typename... UnrolledArguments>
				static Result call(S & self, std::tuple<any_storage<A>...> & tuple, any_storage<UnrolledArguments> &... arguments)
				{
					return unrolling_caller<N - 1>::call(self, tuple, std::get<N - 1>(tuple), arguments...);
				}
			};
			template<typename Dummy>
			struct unrolling_caller<0, Dummy>
			{
				static Result call(S & self, std::tuple<any_storage<A>...> &, any_storage<A> &... arguments)
				{
					return self.func(self, static_cast<A>(arguments)...);
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
			static R return_result(coroutine_preparer & self)
			{
				return static_cast<R>(self.result);
			}

			// this is the function that the coroutine will start off in
			static void coroutine_start(S * self)
			{
				self->started = true;
#				ifndef CORO_NO_EXCEPTIONS
					run_and_store_exception([self]
					{
#				endif
					self->result = Func(*self);
#				ifndef CORO_NO_EXCEPTIONS
					}, self->exception);
#				endif
				self->returned = true;
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
			static void coroutine_start(S * self)
			{
				self->started = true;
#				ifndef CORO_NO_EXCEPTIONS
					run_and_store_exception([self]
					{
#				endif
					Func(*self);
#				ifndef CORO_NO_EXCEPTIONS
					}, self->exception);
#				endif
				self->returned = true;
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
		: Super(std::move(func), stack_size, this)
	{
	}
	coroutine & operator=(std::function<Result (self &, Arguments...)> func)
	{
		Super::recreate(std::move(func), this->stack_size, this);
		return *this;
	}

private:
	// intentionally not implemented
	coroutine(const coroutine &);
	coroutine & operator=(const coroutine &);
};

}
