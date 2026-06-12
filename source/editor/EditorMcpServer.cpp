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

//= INCLUDES ====================================
#include "pch.h"
#include "EditorMcpServer.h"
#include <PhasmaMCP/Codebase/CodebaseContext.h>
#include <PhasmaMCP/Codebase/CodebaseIndexer.h>
#include <PhasmaMCP/HttpTransport.h>
#include <PhasmaMCP/Server.h>
#include <exception>
#include <filesystem>
#include <optional>
//===============================================

//= NAMESPACES =====
using namespace std;
namespace fs = std::filesystem;
//==================

namespace
{
    string to_generic_path(const fs::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    fs::path find_project_root()
    {
        error_code ec;
        fs::path current = fs::current_path(ec);
        if (ec)
        {
            return {};
        }

        for (fs::path candidate = current; !candidate.empty(); candidate = candidate.parent_path())
        {
            if (fs::exists(candidate / "source", ec) && fs::exists(candidate / "build_scripts" / "premake.lua", ec))
            {
                fs::path canonical = fs::weakly_canonical(candidate, ec);
                return ec ? candidate : canonical;
            }

            if (candidate == candidate.parent_path())
            {
                break;
            }
        }

        return current;
    }

    optional<int> parse_port(const string& value)
    {
        try
        {
            const int port = stoi(value);
            if (port >= 1 && port <= 65535)
            {
                return port;
            }
        }
        catch (...)
        {
        }

        return nullopt;
    }

    bool starts_with(const string& value, const string& prefix)
    {
        return value.rfind(prefix, 0) == 0;
    }

    pmcp::LogCallback make_log_callback()
    {
        return [](pmcp::LogLevel level, const string& message)
        {
            switch (level)
            {
                case pmcp::LogLevel::Warn:
                    SP_LOG_WARNING("%s", message.c_str());
                    break;
                case pmcp::LogLevel::Error:
                    SP_LOG_ERROR("%s", message.c_str());
                    break;
                case pmcp::LogLevel::Debug:
                case pmcp::LogLevel::Info:
                default:
                    SP_LOG_INFO("%s", message.c_str());
                    break;
            }
        };
    }

    int get_json_int(const nlohmann::json& json, const char* name, int fallback)
    {
        if (json.contains(name) && json[name].is_number_integer())
        {
            return json[name].get<int>();
        }

        return fallback;
    }
}

EditorMcpServer::EditorMcpServer() = default;

EditorMcpServer::~EditorMcpServer()
{
    Stop();
}

bool EditorMcpServer::Start(const vector<string>& args)
{
    const LaunchConfig config = ParseLaunchConfig(args);
    if (!config.enabled)
    {
        return false;
    }

    m_port         = config.port;
    m_project_root = to_generic_path(find_project_root());

    m_codebase = make_unique<pmcp::CodebaseContext>();
    m_codebase->EnsureStores();

    pmcp::ServerConfig server_config;
    server_config.name         = "spartan-engine";
    server_config.title        = "Spartan Engine MCP";
    server_config.version      = spartan::version::c_str();
    server_config.instructions = "Use search_codebase to inspect Spartan source code. The server is bound to loopback only.";
    server_config.log          = make_log_callback();

    m_server = make_unique<pmcp::Server>(server_config);
    m_server->SetToolProvider([this]() { return BuildTools(); });

    pmcp::HttpTransportConfig http_config;
    http_config.bindAddress           = "127.0.0.1";
    http_config.port                  = m_port;
    http_config.enableLocalOauthShim  = config.enable_oauth_shim;
    http_config.log                   = make_log_callback();

    m_transport = make_unique<pmcp::HttpTransport>(m_server.get(), http_config);
    m_transport->Start();

    if (!m_transport->IsRunning())
    {
        Stop();
        return false;
    }

    StartIndexing();
    SP_LOG_INFO("PhasmaMCP is enabled at http://127.0.0.1:%d/mcp", m_port);

    return true;
}

void EditorMcpServer::Stop()
{
    if (m_transport)
    {
        m_transport->Stop();
        m_transport.reset();
    }

    if (m_indexer)
    {
        m_indexer->Cancel();
    }

    if (m_index_thread.joinable())
    {
        m_index_thread.join();
    }

    m_indexer.reset();
    m_server.reset();
    m_codebase.reset();
}

EditorMcpServer::LaunchConfig EditorMcpServer::ParseLaunchConfig(const vector<string>& args) const
{
    LaunchConfig config;

    for (size_t i = 0; i < args.size(); ++i)
    {
        const string& arg = args[i];

        if (arg == "--mcp")
        {
            config.enabled = true;
        }
        else if (starts_with(arg, "--mcp="))
        {
            const string value = arg.substr(strlen("--mcp="));
            config.enabled = value != "0" && value != "false" && value != "off";
        }
        else if (arg == "--mcp-no-oauth")
        {
            config.enable_oauth_shim = false;
        }
        else if (arg == "--mcp-port")
        {
            config.enabled = true;
            if (i + 1 < args.size())
            {
                if (optional<int> port = parse_port(args[i + 1]))
                {
                    config.port = *port;
                    ++i;
                }
                else
                {
                    SP_LOG_WARNING("Ignoring invalid --mcp-port value '%s'", args[i + 1].c_str());
                }
            }
        }
        else if (starts_with(arg, "--mcp-port="))
        {
            config.enabled = true;
            const string value = arg.substr(strlen("--mcp-port="));
            if (optional<int> port = parse_port(value))
            {
                config.port = *port;
            }
            else
            {
                SP_LOG_WARNING("Ignoring invalid --mcp-port value '%s'", value.c_str());
            }
        }
    }

    return config;
}

vector<pmcp::ToolDefinition> EditorMcpServer::BuildTools()
{
    vector<pmcp::ToolDefinition> tools;

    pmcp::ToolDefinition status;
    status.name        = "spartan_status";
    status.title       = "Spartan Status";
    status.description = "Returns the Spartan MCP server and codebase-index status.";
    status.handler     = [this](const nlohmann::json&, pmcp::Context&)
    {
        const pmcp::CodebaseIndexStatus index_status = m_codebase ? m_codebase->GetStatus() : pmcp::CodebaseIndexStatus{};

        int files_processed = 0;
        int files_total     = 0;
        string current_file;
        {
            lock_guard<mutex> lock(m_progress_mutex);
            files_processed = m_index_files_processed;
            files_total     = m_index_files_total;
            current_file    = m_index_current_file;
        }

        return pmcp::CallToolResult::Json(nlohmann::json{
            {"server", "Spartan Engine MCP"},
            {"version", spartan::version::c_str()},
            {"endpoint", "http://127.0.0.1:" + to_string(m_port) + "/mcp"},
            {"project_root", m_project_root},
            {"engine", {
                {"editor_visible", spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible)},
                {"playing", spartan::Engine::IsFlagSet(spartan::EngineMode::Playing)},
                {"paused", spartan::Engine::IsFlagSet(spartan::EngineMode::Paused)}
            }},
            {"codebase", {
                {"indexing", index_status.indexing},
                {"ready", index_status.ready},
                {"chunks", m_indexed_chunks.load()},
                {"files_processed", files_processed},
                {"files_total", files_total},
                {"current_file", current_file}
            }}
        });
    };
    tools.push_back(std::move(status));

    pmcp::ToolDefinition search;
    search.name        = "search_codebase";
    search.title       = "Search Codebase";
    search.description = "Searches the indexed Spartan source tree using BM25 keyword matching.";
    search.inputSchema = pmcp::schema::Object({
        {"query", "Search text, symbol name, file name, or engine concept.", pmcp::schema::String(), true},
        {"top_k", "Maximum number of chunks to return, from 1 to 25.", pmcp::schema::Integer(), false}
    });
    search.handler = [this](const nlohmann::json& args, pmcp::Context&)
    {
        const string query = args.contains("query") && args["query"].is_string() ? args["query"].get<string>() : string{};
        if (query.empty())
        {
            return pmcp::CallToolResult::Error("query is required");
        }

        shared_ptr<pmcp::BM25Index> index = m_codebase ? m_codebase->GetCodebaseBM25Shared() : nullptr;
        if (!index || index->Size() == 0)
        {
            const pmcp::CodebaseIndexStatus index_status = m_codebase ? m_codebase->GetStatus() : pmcp::CodebaseIndexStatus{};
            return pmcp::CallToolResult::Json(nlohmann::json{
                {"ready", false},
                {"indexing", index_status.indexing},
                {"results", nlohmann::json::array()}
            });
        }

        const int top_k = clamp(get_json_int(args, "top_k", 8), 1, 25);
        nlohmann::json results = nlohmann::json::array();
        for (const pmcp::BM25Index::Result& result : index->Search(query, top_k))
        {
            results.push_back({
                {"id", result.id},
                {"score", result.score},
                {"content", result.content}
            });
        }

        return pmcp::CallToolResult::Json(nlohmann::json{
            {"ready", true},
            {"query", query},
            {"results", std::move(results)}
        });
    };
    tools.push_back(std::move(search));

    return tools;
}

void EditorMcpServer::StartIndexing()
{
    if (!m_codebase || m_codebase->GetStatus().indexing)
    {
        return;
    }

    if (m_index_thread.joinable())
    {
        m_index_thread.join();
    }

    pmcp::IndexerConfig index_config;
    index_config.directories = { to_generic_path(fs::path(m_project_root) / "source") };
    index_config.skip_directories = {
        to_generic_path(fs::path(m_project_root) / "source" / "editor" / "ImGui" / "Source")
    };
    index_config.skip_files = { "desktop.ini" };
    index_config.max_chunk_chars = 6000;

    pmcp::CodebaseIndexingConfig& stored_config = m_codebase->MutableIndexingConfig();
    stored_config.directories      = index_config.directories;
    stored_config.skip_directories = index_config.skip_directories;
    stored_config.skip_files       = index_config.skip_files;
    stored_config.skip_extensions  = index_config.skip_extensions;

    {
        lock_guard<mutex> lock(m_progress_mutex);
        m_index_files_processed = 0;
        m_index_files_total     = 0;
        m_index_current_file.clear();
    }

    m_indexed_chunks.store(0);
    m_codebase->SetIndexing(true);

    m_indexer = make_unique<pmcp::CodebaseIndexer>(
        m_codebase->GetCodebaseBM25Shared().get(),
        [this](int files_processed, int files_total, const string& current_file)
        {
            lock_guard<mutex> lock(m_progress_mutex);
            m_index_files_processed = files_processed;
            m_index_files_total     = files_total;
            m_index_current_file    = current_file;
        },
        [](const string& message)
        {
            SP_LOG_INFO("[MCP] %s", message.c_str());
        }
    );

    m_index_thread = thread([this, index_config]()
    {
        int chunks = 0;
        try
        {
            chunks = m_indexer ? m_indexer->Index(index_config) : 0;
        }
        catch (const exception& exception)
        {
            SP_LOG_ERROR("[MCP] Codebase indexing failed: %s", exception.what());
        }
        catch (...)
        {
            SP_LOG_ERROR("[MCP] Codebase indexing failed");
        }

        m_indexed_chunks.store(chunks);
        if (m_codebase)
        {
            m_codebase->SetIndexing(false);
        }
    });
}
