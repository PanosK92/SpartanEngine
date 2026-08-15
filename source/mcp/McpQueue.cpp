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
#include <cstdio>
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
            // queued work is skipped after timeout, executing handlers cannot be preempted
            bool abandoned = false;
            std::mutex mutex;
            std::condition_variable completed_condition;
        };

        std::mutex queue_mutex;
        std::deque<std::shared_ptr<McpJob>> jobs;
        bool shutting_down = false;
        constexpr auto mcp_job_timeout = std::chrono::seconds(25);

        // a message can hold a quote or a newline, an exception's text especially, and an unescaped one
        // produces a reply the client cannot parse, which reads as the engine having gone silent
        std::string error_response(const std::string& message)
        {
            std::string escaped;
            for (const char character : message)
            {
                switch (character)
                {
                    case '\"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b";  break;
                    case '\f': escaped += "\\f";  break;
                    case '\n': escaped += "\\n";  break;
                    case '\r': escaped += "\\r";  break;
                    case '\t': escaped += "\\t";  break;
                    default:
                    {
                        if (static_cast<unsigned char>(character) < 0x20)
                        {
                            char buffer[7] = {};
                            snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(character)));
                            escaped += buffer;
                        }
                        else
                        {
                            escaped += character;
                        }
                        break;
                    }
                }
            }

            return "{\"ok\":false,\"error\":\"" + escaped + "\"}";
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
            // callers must inspect state before retrying mutations because executing handlers may still finish
            job->abandoned = true;
            return error_response("engine did not answer within 25000ms");
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
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                if (job->abandoned)
                {
                    continue;
                }
            }

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
