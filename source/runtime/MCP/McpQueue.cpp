/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ================
#include "pch.h"
#include "McpQueue.h"
#include "McpCommands.h"
#include <condition_variable>
#include <deque>
#include <exception>
//===========================

namespace spartan
{
    namespace
    {
        struct McpJob
        {
            McpRequest request;
            std::string response;
            bool completed = false;
            std::mutex mutex;
            std::condition_variable completed_condition;
        };

        std::mutex queue_mutex;
        std::deque<std::shared_ptr<McpJob>> jobs;
        bool shutting_down = false;
        constexpr auto mcp_job_timeout = std::chrono::seconds(30);

        std::string error_response(const char* message)
        {
            return std::string("{\"ok\":false,\"error\":\"") + message + "\"}";
        }
    }

    void McpQueue::Initialize()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        shutting_down = false;
    }

    std::string McpQueue::Submit(const McpRequest& request)
    {
        std::shared_ptr<McpJob> job = std::make_shared<McpJob>();
        job->request = request;

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (shutting_down)
            {
                return error_response("MCP is shutting down");
            }

            jobs.emplace_back(job);
        }

        std::unique_lock<std::mutex> lock(job->mutex);
        if (!job->completed_condition.wait_for(lock, mcp_job_timeout, [&job]() { return job->completed; }))
        {
            return error_response("engine did not answer within 30000ms");
        }

        return job->response;
    }

    void McpQueue::Tick()
    {
        std::deque<std::shared_ptr<McpJob>> jobs_to_execute;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            jobs_to_execute.swap(jobs);
        }

        for (const std::shared_ptr<McpJob>& job : jobs_to_execute)
        {
            std::string response;
            try
            {
                response = ExecuteMcpCommand(job->request);
            }
            catch (const std::exception& exception)
            {
                response = error_response(exception.what());
            }
            catch (...)
            {
                response = error_response("command failed");
            }

            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->response  = response;
                job->completed = true;
            }
            job->completed_condition.notify_one();
        }
    }

    void McpQueue::Shutdown()
    {
        std::deque<std::shared_ptr<McpJob>> jobs_to_cancel;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            shutting_down = true;
            jobs_to_cancel.swap(jobs);
        }

        for (const std::shared_ptr<McpJob>& job : jobs_to_cancel)
        {
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->response  = error_response("MCP is shutting down");
                job->completed = true;
            }
            job->completed_condition.notify_one();
        }
    }
}
