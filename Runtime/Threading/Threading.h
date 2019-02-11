/*
Copyright(c) 2016-2019 Panos Karabelas

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

#pragma once

//= INCLUDES =================
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include "../Core/ISubsystem.h"
#include "../Logging/Log.h"
//============================

namespace Directus
{
	//= TASK ===============================================================================
	class Task
	{
	public:
		typedef std::function<void()> functionType;

		Task(functionType&& function) { m_function = std::forward<functionType>(function); }
		void Execute() { m_function(); }

	private:
		functionType m_function;
	};
	//======================================================================================

	class Threading : public ISubsystem
	{
	public:
		Threading(Context* context);
		~Threading();

		// This function is invoked by the threads
		void Invoke();

		// Add a task
		template <typename Function>
		void AddTask(Function&& function)
		{
			if (m_threads.empty())
			{
				LOG_WARNING("Threading::AddTask: No available threads, function will execute in the same thread");
				function();
				return;
			}

			// Lock tasks mutex
			std::unique_lock<std::mutex> lock(m_tasksMutex);

			// Save the task
			m_tasks.push(std::make_shared<Task>(std::bind(std::forward<Function>(function))));

			// Unlock the mutex
			lock.unlock();

			// Wake up a thread
			m_conditionVar.notify_one();
		}

	private:
		unsigned int m_threadCount;
		std::vector<std::thread> m_threads;
		std::queue<std::shared_ptr<Task>> m_tasks;
		std::mutex m_tasksMutex;
		std::condition_variable m_conditionVar;
		bool m_stopping;
	};
}