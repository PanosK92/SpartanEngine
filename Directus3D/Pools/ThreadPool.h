/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool
{
public:
	ThreadPool();
	template <class F, class... Args>
	auto enqueue(F&& f, Args&&... args)
	-> std::future<typename std::result_of<F(Args ...)>::type>;
	~ThreadPool();

	static ThreadPool& GetInstance()
	{
		static ThreadPool instance;
		return instance;
	}

private:
	// need to keep track of threads so we can join them
	std::vector<std::thread> workers;
	// the task queue
	std::queue<std::function<void()>> tasks;

	// synchronization
	std::mutex queue_mutex;
	std::condition_variable condition;
	bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool() : stop(false)
{
	size_t threads = 4;
	for (size_t i = 0; i < threads; ++i)
		workers.emplace_back(
			[this]
			{
				for (;;)
				{
					std::function<void()> task;

					{
						std::unique_lock<std::mutex> lock(this->queue_mutex);
						this->condition.wait(lock,
						                     [this]
						                     {
							                     return this->stop || !this->tasks.empty();
						                     });
						if (this->stop && this->tasks.empty())
							return;
						task = std::move(this->tasks.front());
						this->tasks.pop();
					}

					task();
				}
			}
		);
}

// add new work item to the pool
template <class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args ...)>::type>
{
	using return_type = typename std::result_of<F(Args ...)>::type;

	auto task = std::make_shared<std::packaged_task<return_type()>>(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...)
	);

	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);

		// don't allow enqueueing after stopping the pool
		if (stop)
			throw std::runtime_error("enqueue on stopped ThreadPool");

		tasks.emplace([task]()
			{
				(*task)();
			});
	}
	condition.notify_one();
	return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		stop = true;
	}
	condition.notify_all();
	for (std::thread& worker : workers)
		worker.join();
}
