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

#pragma once

//= INCLUDES =======
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <PhasmaMCP/Tool.h>
//==================

namespace pmcp
{
    class CodebaseContext;
    class CodebaseIndexer;
    class HttpTransport;
    class Server;
}

class EditorMcpServer
{
public:
    EditorMcpServer();
    ~EditorMcpServer();

    bool Start(const std::vector<std::string>& args);
    void Stop();

private:
    struct LaunchConfig
    {
        bool enabled            = false;
        bool enable_oauth_shim  = true;
        int port                = 8765;
    };

    LaunchConfig ParseLaunchConfig(const std::vector<std::string>& args) const;
    std::vector<pmcp::ToolDefinition> BuildTools();
    void StartIndexing();

    std::unique_ptr<pmcp::Server> m_server;
    std::unique_ptr<pmcp::HttpTransport> m_transport;
    std::unique_ptr<pmcp::CodebaseContext> m_codebase;
    std::unique_ptr<pmcp::CodebaseIndexer> m_indexer;
    std::thread m_index_thread;

    std::string m_project_root;
    int m_port = 8765;
    std::atomic<int> m_indexed_chunks = 0;

    mutable std::mutex m_progress_mutex;
    int m_index_files_processed = 0;
    int m_index_files_total     = 0;
    std::string m_index_current_file;
};
