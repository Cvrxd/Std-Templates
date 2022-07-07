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
        //Funtions variables
        std::function<void()> void_function;
        std::function<std::any()> non_void_function;

        //Result variable
        std::any return_value;

        //Is void flag
        bool is_void;

    public:
        template <typename FuncReturnType, typename ...Args, typename ...FuncTypes>
        Task(FuncReturnType(*func)(FuncTypes...), Args&&... args)
            : is_void{ std::is_void_v<FuncReturnType> }
        {
            if constexpr (std::is_void_v<FuncReturnType>)
            {
                void_function = std::bind(func, args...);
                non_void_function = []()->int { return 0; };
            }
            else
            {
                void_function = []()->void {};
                non_void_function = std::bind(func, args...);
            }
        }

        void run()
        {
            void_function();
            return_value = non_void_function();
        }

        bool has_result()
        {
            return !is_void;
        }

        std::any get_result() const
        {
            assert(!is_void);
            assert(return_value.has_value());

            return return_value;
        }
    };

    using unique_lock_m = std::unique_lock<std::mutex>;
    using lock_guard_m = std::lock_guard<std::mutex>;

    //Threads
    std::vector<std::thread>               threads;

    std::queue<std::pair<Task, uint64_t>>  taskPool;
    std::mutex                             taskPoolMutex;
    std::condition_variable                taskPoolCv;

    std::unordered_map<uint64_t, TaskInfo> tasksInfoMap;
    std::condition_variable                tasksInfoCv;
    std::mutex                             tasksInfoMutex;

    std::condition_variable                waitAllCv;

    std::atomic<bool>                      quite{ false };
    std::atomic<uint64_t>                  lastTaskIndex{ 0 };
    std::atomic<uint64_t>                  completedTasksCounter{ 0 };

    //Run task function
    void run()
    {
        while (!quite)
        {
            unique_lock_m lock(taskPoolMutex);

            taskPoolCv.wait(lock, [this]()->bool { return !taskPool.empty() || quite; });

            if (!taskPool.empty() && !quite)
            {
                std::pair<Task, uint64_t> task = std::move(taskPool.front());
                taskPool.pop();
                lock.unlock();

                task.first.run();

                lock_guard_m lock(tasksInfoMutex);

                if (task.first.has_result())
                {
                    tasksInfoMap[task.second].result = task.first.get_result();
                }

                tasksInfoMap[task.second].status = TaskStatus::completed;
                ++completedTasksCounter;
            }

            waitAllCv.notify_all();
            tasksInfoCv.notify_all(); // notify for wait function
        }
    }

public:
    ThreadsPool(const uint32_t num_threads)
    {
        threads.reserve(num_threads);

        for (int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(&ThreadsPool::run, this);
        }
    }

    template <typename Func, typename ...Args, typename ...FuncTypes>
    uint64_t add_task(Func(*func)(FuncTypes...), Args&&... args)
    {
        const uint64_t task_id = lastTaskIndex++;

        unique_lock_m lock_info_lock(tasksInfoMutex);

        tasksInfoMap[task_id] = TaskInfo();
        lock_info_lock.unlock();

        lock_guard_m pool_lock(taskPoolMutex);

        taskPool.emplace(Task(func, std::forward<Args>(args)...), task_id);
        taskPoolCv.notify_one();

        return task_id;
    }

    void wait(const uint64_t task_id)
    {
        unique_lock_m lock(tasksInfoMutex);

        tasksInfoCv.wait(lock, [this, task_id]() -> bool
            {
                return task_id < lastTaskIndex&& tasksInfoMap[task_id].status == TaskStatus::completed;
            });
    }

    std::any wait_result(const uint64_t task_id)
    {
        wait(task_id);

        return tasksInfoMap[task_id].result;
    }

    template<class T>
    void wait_result(const uint64_t task_id, T& value)
    {
        wait(task_id);

        value = std::any_cast<T>(tasksInfoMap[task_id].result);
    }

    void wait_all()
    {
        unique_lock_m lock(tasksInfoMutex);

        waitAllCv.wait(lock, [this]() -> bool { return completedTasksCounter == lastTaskIndex; });
    }

    bool task_completed(const uint64_t task_id)
    {
        unique_lock_m lock(tasksInfoMutex);

        return task_id < lastTaskIndex&& tasksInfoMap[task_id].status == TaskStatus::completed;
    }

    ~ThreadsPool()
    {
        quite = true;

        taskPoolCv.notify_all();

        for (int i = 0; i < threads.size(); ++i)
        {
            threads[i].join();
        }
    }
};