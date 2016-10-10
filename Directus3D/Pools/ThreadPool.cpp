//= INCLUDES ==========
#include "ThreadPool.h"
//=====================

//= NAMESPACES ======
using namespace  std;
//===================

ThreadPool::ThreadPool()
{
	for (int i = 0; i < 5; i)
		m_threads.emplace_back(thread(&ThreadPool::Invoke, this));
}

ThreadPool::~ThreadPool()
{
	// Put unique lock on task mutex.
	unique_lock<mutex> lock(m_tasksMutex);

	// Set termination flag to true.
	m_stopping = true;

	lock.unlock();

	// Wake up all threads.
	m_conditionVar.notify_all();

	// Join all threads.
	for (thread &thread : m_threads)
		thread.join();

	// Empty workers vector.
	m_threads.empty();
}

void ThreadPool::Invoke()
{
	function<void()> task;
	while (true)
	{
		// Lock tasks mutex
		unique_lock<mutex> lock(m_tasksMutex);

		// Check condition on notification
		m_conditionVar.wait(lock, [this] { return !m_tasks.empty() || m_stopping; });

		// If m_stopping is true, it's time to shut everything down
		if (m_stopping && m_tasks.empty())
			return;

		// Get next task in the queue.
		task = m_tasks.front();

		// Remove it from the queue.
		m_tasks.pop();

		// Unlock the mutex
		lock.unlock();

		// Execute the task.
		task();
	}
}

void ThreadPool::AddTask(function<void()> task)
{
	// Lock tasks mutex
	unique_lock<mutex> lock(m_tasksMutex);

	// Save the task
	m_tasks.push(task);

	// Unlock the mutex
	lock.unlock();

	// Wake up a thread
	m_conditionVar.notify_one();
}
