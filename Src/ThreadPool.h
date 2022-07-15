#pragma once
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <future>
#include <unordered_map>
#include <any>
#include <atomic>
#include <cassert>
#include <utility>


class ThreadsPool
{
private:
	using unique_lock_m = std::unique_lock <std::mutex>;
	using lock_guard_m  = std::lock_guard  <std::mutex>;

	enum class TaskStatus
	{
		in_proces,
		completed
	};

	struct TaskInfo
	{
		TaskStatus status = TaskStatus::in_proces;
		std::any result;
	};

	class Task
	{
	private:
		std::function<void()> void_function;
		std::function<std::any()> non_void_function;

		std::any return_value;

		bool is_void;
	public:

		template<typename ReturnFunctionType, typename ...FunctionArguments, typename ...FunctionTypes>
		Task(ReturnFunctionType(*function)(FunctionTypes...), FunctionArguments && ...arguments)
			: is_void{ std::is_void_v<FuncReturnType> }
		{
			if constexpr (this->is_void)
			{
				this->void_function = std::bind(function, arguments);

				this->non_void_function = []() -> int {return 0; };
			}
			else
			{
				this->void_function = []() -> void { };

				this->non_void_function = std::bind(function, arguments);
			}
		}

		void run()
		{
			if (this->is_void)
			{
				this->void_function();
			}
			else
			{
				this->return_value = this->non_void_function();
			}	
		}

		bool has_result()
		{
			return !this->is_void;
		}

		std::any get_result() const
		{
			assert(!this->is_void);
			assert(this->return_value.has_value());

			return this->return_value;
		}
	};

	//Threads
	std::vector<std::thread> threads;

	//Tasks pool variables
	std::queue<std::pair<Task, uint64_t>>   tasks_pool;
	std::mutex                             tasks_pool_mutex;
	std::condition_variable                tasks_pool_cv;

	//Tasks info variables
	std::unordered_map<uint64_t, TaskInfo>  tasks_info_map;
	std::mutex                             tasks_info_mutex;
	std::condition_variable                tasks_info_cv;

	std::condition_variable                wait_all_cv;

	std::atomic<bool>                      quite                  { false };
	std::atomic<uint64_t>                  last_task_index        { 0 };
	std::atomic<uint64_t>                  completed_task_counter { 0 };

	//Tasks run function
	void run()
	{
		while (!this->quite)
		{
			unique_lock_m lock(this->tasks_pool_mutex);

			this->tasks_pool_cv.wait(lock, [this]() -> bool { return !this->tasks_pool.empty() || quite; });

			if (!this->tasks_pool.empty() && !this->quite)
			{
				std::pair<Task, uint64_t> task = std::move_if_noexcept(this->tasks_pool.front());

				this->tasks_pool.pop();

				lock.unlock();

				task.first.run();

				lock_guard_m lock(this->tasks_info_mutex);

				if (task.first.has_result())
				{
					this->tasks_info_map[task.second].result = task.first.get_result();
				}

				this->tasks_info_map[task.second].status = TaskStatus::completed;

				this->completed_task_counter.fetch_add(1);
			}

			this->wait_all_cv.notify_all();
			this->tasks_info_cv.notify_all(); 
		}
	}
public:
	ThreadsPool(const ThreadsPool& other)              = delete;
	ThreadsPool(ThreadsPool&& other)                   = delete;
	ThreadsPool& operator = (const ThreadsPool& other) = delete;
	ThreadsPool& operator = (ThreadsPool&& other)      = delete;

	ThreadsPool(const uint64_t& number_of_threads)
	{
		this->threads.reserve(number_of_threads);

		for (int i = 0; i < number_of_threads; ++i)
		{
			this->threads.emplace_back(&ThreadsPool::run, this);
		}
	}

	template <typename FunctionReturntType, typename ...FuntionArguments, typename ...FunctionTypes>
	uint64_t add_task(FunctionReturntType(*function)(FunctionTypes...), FuntionArguments&&... arguments)
	{
		const uint64_t task_id = this->last_task_index++;

		unique_lock_m lock_info_lock(this->tasks_info_mutex);

		this->tasks_info_map[task_id] = TaskInfo();
		lock_info_lock.unlock();

		lock_guard_m pool_lock(this->tasks_pool_mutex);

		this->tasks_pool.emplace(Task(function, std::forward<FuntionArguments>(arguments)...), task_id);

		this->tasks_pool_cv.notify_one();

		return task_id;
	}

	void wait(const uint64_t& task_id)
	{
		unique_lock_m lock(this->tasks_info_mutex);

		this->tasks_info_cv.wait(lock, [this, task_id]() -> bool
			{
				return task_id < last_task_index && tasks_info_map[task_id].status == TaskStatus::completed;
			});
	}

	std::any wait_result(const uint64_t task_id)
	{
		wait(task_id);

		return this->tasks_info_map[task_id].result;
	}

	template<class T>
	void wait_result(const uint64_t task_id, T& value)
	{
		wait(task_id);

		value = std::any_cast<T>(tasksInfoMap[task_id].result);
	}

	void wait_all()
	{
		unique_lock_m lock(this->tasks_info_mutex);

		this->wait_all_cv.wait(lock, [this]() -> bool { return completed_task_counter == last_task_index; });
	}

	bool task_completed(const uint64_t task_id)
	{
		unique_lock_m lock(this->tasks_info_mutex);

		return task_id < last_task_index && tasks_info_map[task_id].status == TaskStatus::completed;
	}

	~ThreadsPool()
	{
		this->quite = true;

		this->wait_all_cv.notify_all();

		for (int i = 0; i < threads.size(); ++i)
		{
			this->threads[i].join();
		}
	}

};
