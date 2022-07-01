#pragma once
#include<string>
#include<iostream>

template <typename T>
class shared_ptr;
template <typename T, typename... Args>
shared_ptr<T> make_shared(T, Args&&... args);


////////////////// move //////////////////
template <typename T>
std::remove_reference_t<T>&& _move(T&& value) noexcept
{
	return dynamic_cast<std::remove_reference_t<T>&&>(value);
}

////////////////// Alloc //////////////////
template <typename T>
class MyAlloc
{
public:
#define null_error if(!ptr) return

	T * allocate(size_t count)
	{
		return static_cast<T*>(::operator new(count * sizeof(T)));
	}
	void deallocate(T* ptr, size_t)
	{
		::operator delete(ptr);
	}
	template<typename... Args>
	void construct(T* ptr, Args&&... args)
	{
		null_error;
		new(reinterpret_cast<void*>(ptr)) T{ std::forward<Args>(args)... };
	}
	void destroy(T* ptr)
	{
		null_error;
		ptr->~T();
	}
};

//////////////////Uniq Ptr//////////////////

template<typename T>
class UniqPtr
{
public:
	UniqPtr(T* optr) : _ptr(optr) {}
	UniqPtr() : _ptr(nullptr) {}

	UniqPtr(UniqPtr&& other)
		: _ptr(other._ptr)
	{
		other._ptr = nullptr;
	}
	UniqPtr& operator=(UniqPtr&& other)
	{
		if (this == &other)
			return *this;

		delete this->_ptr;
		this->_ptr = other._ptr;
		other._ptr = nullptr;
		return *this;
	}
	T& operator*() const
	{
		return *_ptr;
	}
	T* operator->()const
	{
		return _ptr;
	}
	~UniqPtr()
	{
		delete _ptr;
	}
	UniqPtr(const UniqPtr&) = delete;
	UniqPtr& operator=(const UniqPtr&) = delete;
private:
	T* _ptr;
};


//////////////////Shared Ptr//////////////////
template <typename U>
struct ControlBlock
{
	ControlBlock(U data) : object(data) { counter = 1; }
	size_t counter;
	U object;
};

template <typename T>
class shared_ptr
{
private:
	template <typename T, typename... Args>
	friend shared_ptr<T> make_shared(T, Args&&... args);

	ControlBlock<T>* cptr = nullptr; // make shared 
	T* _ptr = nullptr; // c pointer
	size_t* counter = nullptr; // c pointer

	struct make_shared_t; // some struct

public:
	shared_ptr() {}
	shared_ptr(ControlBlock<T>* storage) : cptr(storage) {}
	shared_ptr(T* ptr) : _ptr(ptr)
	{
		counter = new size_t(1);
	}
	shared_ptr(shared_ptr& other) : _ptr(other._ptr), counter(other.counter)
	{
		++(*counter);
	}
	shared_ptr(shared_ptr&& other) : _ptr(other._ptr), counter(other.counter)
	{
		other._ptr = nullptr;
		other.counter = nullptr;
	}
	shared_ptr& operator=(shared_ptr& other)
	{
		if (*this == &other) return *this;
		--(*counter);
		_ptr = other._ptr;
		counter = other.counter;
		++(*counter);
		return *this;
	}
	shared_ptr& operator=(shared_ptr&& other)
	{
		if (*this == &other) return *this;
		_ptr = other._ptr;
		counter = other.counter;
		other._ptr = nullptr;
		other.counter = nullptr;
		return *this;
	}

	shared_ptr& operator*() { return *_ptr; }
	size_t use_count() { return *counter; }

	~shared_ptr()
	{
		if (*counter > 1)
		{
			--(*counter);
			return;
		}
		delete counter;
		delete _ptr;
		_ptr = nullptr;
		counter = nullptr;
	}
};

template <typename T, typename... Args>
shared_ptr<T> make_shared(T, Args&&... args)
{
	auto ptr = new ControlBlock<T>(T{ std::forward<Args>(args)... });
	return ::shared_ptr<T>(ptr);
}

//////////////////// Variant /////////////////////////

template<size_t N, typename T, typename... Types>
struct get_index_by_type
{
	static const size_t value = N;
};

template<size_t N, typename T, typename Head, typename... Tail>
struct get_index_by_type<N, T, Head, Tail...>
{
	static const size_t value = std::is_same_v<T, Head> ? N
		: get_index_by_type<N + 1, T, Tail...>::value;
};

template<typename... Types>
class Variant
{
private:
	template<typename... TTypes>
	union VariadicUnion {};

	template<typename Head, typename... Tail>
	union VariadicUnion<Head, Tail...>
	{
		Head head;
		VariadicUnion<Tail...> tail;

		template<size_t N, typename T>
		void put(const T& value)
		{
			if constexpr (N == 0)
			{
				new(&head) T(value);
			}
			else
			{
				tail.put<N - 1>(value);
			}
		}
	};

	VariadicUnion<Types...> storage;
	size_t current = 0;

public:
	template<typename T>
	Variant(const T& value)
	{
		current = get_index_by_type<0, T, Types...>::value;
		storage.put<get_index_by_type<0, T, Types...>::value>::value > (value);
	}

	size_t index() const
	{
		return current;
	}

	template<typename T>
	bool holds_alternative() const
	{
		return get_index_by_type<0, T, Types...>::value == current;
	}
};

/////////////////// Any ////////////////////////

class Any
{
private:
	struct Base
	{
		virtual Base* get_copy();
		virtual ~Base() {}
	};

	template<typename T>
	struct  Derived : public Base
	{
		T value;
		Base* get_copy() override
		{
			return new Derived<T>(value);
		}
		Derived(const T& other_value) : value(other_value) {}
		Derived(T&& other_value) : value(std::move_if_noexcept(other_value)) {}
	};

	Base* storage = nullptr;
public:
	template<typename U>
	Any(const U& value) : storage(new Derived<U>(value)) {}
	template<typename U>
	Any(U&& value) : storage(new Derived<U>(std::move(value))) {}

	Any(const Any& other) : storage(other.storage->get_copy()) {}
	Any& operator=(Any&& other) noexcept
	{
		if (this == &other) return *this;
		delete this->storage;
		storage = other.storage;
		other.storage = nullptr;
		return *this;
	}
	Any(Any&& other) noexcept : storage(other.storage)
	{
		other.storage = nullptr;
	}

	template<typename U>
	Any& operator=(const U& object)
	{
		delete storage;
		storage = new Derived<U>(object);
		return *this;
	}


	~Any()
	{
		delete storage;
	}
};

