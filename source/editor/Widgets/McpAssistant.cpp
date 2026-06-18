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

//= INCLUDES =======================
#include "pch.h"
#include "McpAssistant.h"
#include "MCP/McpServer.h"
#include "Input/Input.h"
#include "FileSystem/FileSystem.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
//==================================

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <Windows.h>
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace
{
    constexpr uint16_t assistant_port = 47778;
    constexpr int assistant_prompt_timeout_ms = 240000;
    constexpr int assistant_model_timeout_ms = 60000;
    constexpr int assistant_start_timeout_ms = 10000;
    constexpr int assistant_start_poll_ms = 100;
    std::mutex assistant_mutex;
    std::mutex assistant_start_mutex;
    std::string assistant_response;
    std::string assistant_models;
    std::string assistant_activity;
    std::deque<std::string> assistant_activity_history;
    bool assistant_busy = false;
    std::chrono::steady_clock::time_point assistant_busy_start;
    constexpr size_t assistant_activity_history_max = 8;

#ifdef _WIN32
    using socket_t = SOCKET;
    constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
    using socket_t = int;
    constexpr socket_t invalid_socket = -1;
#endif

    void close_socket(socket_t socket)
    {
        if (socket == invalid_socket)
        {
            return;
        }

    #ifdef _WIN32
        closesocket(socket);
    #else
        close(socket);
    #endif
    }

#ifdef _WIN32
    uint32_t find_pid_on_port(uint16_t port)
    {
        ULONG size = 0;
        if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER)
        {
            return 0;
        }

        std::vector<char> buffer(size);
        PMIB_TCPTABLE_OWNER_PID table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
        if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        {
            return 0;
        }

        const u_short target = htons(port);
        for (DWORD i = 0; i < table->dwNumEntries; i++)
        {
            const MIB_TCPROW_OWNER_PID& row = table->table[i];
            if (row.dwState == MIB_TCP_STATE_LISTEN && static_cast<u_short>(row.dwLocalPort & 0xFFFF) == target)
            {
                return static_cast<uint32_t>(row.dwOwningPid);
            }
        }

        return 0;
    }
#endif

    bool kill_assistant_process()
    {
    #ifdef _WIN32
        bool killed = false;
        for (int attempt = 0; attempt < 20; attempt++)
        {
            const uint32_t pid = find_pid_on_port(assistant_port);
            if (pid == 0)
            {
                break;
            }

            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
            if (process == nullptr)
            {
                break;
            }

            TerminateProcess(process, 0);
            WaitForSingleObject(process, 2000);
            CloseHandle(process);
            killed = true;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return killed;
    #else
        return std::system("pkill -f assistant.mjs >/dev/null 2>&1") == 0;
    #endif
    }

    void set_response(const std::string& response)
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        assistant_response = response;
    }

    std::string take_response()
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        std::string response = assistant_response;
        assistant_response.clear();
        return response;
    }

    void set_models(const std::string& models)
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        assistant_models = models;
    }

    std::string take_models()
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        std::string models = assistant_models;
        assistant_models.clear();
        return models;
    }

    void log_info(const std::string& message)
    {
        SP_LOG_INFO("MCP Assistant: %s", message.c_str());
    }

    void log_error(const std::string& message)
    {
        SP_LOG_ERROR("MCP Assistant: %s", message.c_str());
    }

    void set_busy(bool busy, const std::string& activity = "")
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        if (busy && !assistant_busy)
        {
            assistant_activity_history.clear();
            assistant_busy_start = std::chrono::steady_clock::now();
        }

        assistant_busy = busy;
        assistant_activity = activity;
        if (busy && !activity.empty() && (assistant_activity_history.empty() || assistant_activity_history.back() != activity))
        {
            assistant_activity_history.emplace_back(activity);
            while (assistant_activity_history.size() > assistant_activity_history_max)
            {
                assistant_activity_history.pop_front();
            }
        }
    }

    std::string url_encode(const std::string& value)
    {
        std::ostringstream stream;
        stream << std::hex << std::uppercase;

        for (const unsigned char c : value)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                stream << c;
            }
            else
            {
                stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            }
        }

        return stream.str();
    }

    int hex_to_int(char value)
    {
        if (value >= '0' && value <= '9')
        {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f')
        {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F')
        {
            return value - 'A' + 10;
        }

        return -1;
    }

    std::string url_decode(const std::string& value)
    {
        std::string decoded;
        decoded.reserve(value.size());

        for (size_t i = 0; i < value.size(); i++)
        {
            if (value[i] == '%' && i + 2 < value.size())
            {
                const int high = hex_to_int(value[i + 1]);
                const int low  = hex_to_int(value[i + 2]);
                if (high >= 0 && low >= 0)
                {
                    decoded.push_back(static_cast<char>((high << 4) | low));
                    i += 2;
                    continue;
                }
            }

            decoded.push_back(value[i]);
        }

        return decoded;
    }

    std::string trim_copy_paste_whitespace(const std::string& value)
    {
        const size_t start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return "";
        }

        const size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, end - start + 1);
    }

    std::filesystem::path executable_directory()
    {
    #ifdef _WIN32
        char path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return std::filesystem::path(path).parent_path();
        }
    #endif

        return std::filesystem::current_path();
    }

    void add_key_file_candidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& directory)
    {
        if (directory.empty())
        {
            return;
        }

        const std::filesystem::path candidate = std::filesystem::absolute(directory / "cursor_api_key.txt");
        for (const std::filesystem::path& existing_candidate : candidates)
        {
            if (existing_candidate == candidate)
            {
                return;
            }
        }

        candidates.emplace_back(candidate);
    }

    std::vector<std::filesystem::path> api_key_file_candidates()
    {
        std::vector<std::filesystem::path> candidates;
        add_key_file_candidate(candidates, executable_directory());
        add_key_file_candidate(candidates, std::filesystem::current_path());
        add_key_file_candidate(candidates, spartan::FileSystem::GetWorkingDirectory());

        std::filesystem::path directory = std::filesystem::current_path();
        for (uint32_t i = 0; i < 4 && !directory.empty(); i++)
        {
            add_key_file_candidate(candidates, directory);
            directory = directory.parent_path();
        }

        return candidates;
    }

    bool read_api_key_file(std::string& api_key, std::filesystem::path& loaded_path, std::vector<std::filesystem::path>& searched_paths)
    {
        searched_paths = api_key_file_candidates();
        for (const std::filesystem::path& key_path : searched_paths)
        {
            std::string contents;
            if (!spartan::FileSystem::ReadFile(key_path.string(), contents))
            {
                continue;
            }

            std::istringstream stream(contents);
            std::string line;
            while (std::getline(stream, line))
            {
                api_key = trim_copy_paste_whitespace(line);
                if (api_key.rfind("\xEF\xBB\xBF", 0) == 0)
                {
                    api_key.erase(0, 3);
                }
                if (!api_key.empty())
                {
                    loaded_path = key_path;
                    return true;
                }
            }
        }

        return false;
    }

    std::string thinking_text()
    {
        const int dot_count = static_cast<int>(ImGui::GetTime() * 2.5) % 4;
        std::string activity;
        std::deque<std::string> history;
        int elapsed_seconds = 0;
        {
            std::lock_guard<std::mutex> lock(assistant_mutex);
            activity = assistant_activity.empty() ? "thinking" : assistant_activity;
            history  = assistant_activity_history;
            elapsed_seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - assistant_busy_start).count());
        }

        std::string text = activity + std::string(static_cast<size_t>(dot_count), '.');
        text += " (" + std::to_string(elapsed_seconds) + "s)";
        if (!history.empty())
        {
            text += "\n\nActivity";
            for (const std::string& entry : history)
            {
                text += "\n- " + entry;
            }
        }

        return text;
    }

    bool find_assistant_script(std::filesystem::path& script_path)
    {
        const std::filesystem::path candidates[] =
        {
            "tools/mcp/spartan_engine/assistant.mjs",
            "../tools/mcp/spartan_engine/assistant.mjs",
            "../../tools/mcp/spartan_engine/assistant.mjs",
            "../../../tools/mcp/spartan_engine/assistant.mjs"
        };

        for (const std::filesystem::path& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
            {
                script_path = std::filesystem::absolute(candidate);
                return true;
            }
        }

        return false;
    }

    bool run_process_and_wait(const std::string& command, const std::filesystem::path& working_directory, const std::string& failure_message)
    {
    #ifdef _WIN32
        STARTUPINFOA startup_info = {};
        PROCESS_INFORMATION process_info = {};
        startup_info.cb = sizeof(startup_info);

        std::string command_mutable = command;
        std::string working_directory_string = working_directory.string();
        BOOL success = CreateProcessA(
            nullptr,
            command_mutable.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            working_directory_string.c_str(),
            &startup_info,
            &process_info
        );

        if (!success)
        {
            log_error(failure_message);
            return false;
        }

        WaitForSingleObject(process_info.hProcess, INFINITE);

        DWORD exit_code = 1;
        GetExitCodeProcess(process_info.hProcess, &exit_code);

        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);

        if (exit_code != 0)
        {
            log_error(failure_message);
            return false;
        }

        return true;
    #else
        const std::string shell_command = "cd \"" + working_directory.string() + "\" && " + command;
        if (std::system(shell_command.c_str()) != 0)
        {
            log_error(failure_message);
            return false;
        }

        return true;
    #endif
    }

    bool assistant_dependencies_ready(const std::filesystem::path& script_directory)
    {
        const std::filesystem::path node_modules = script_directory / "node_modules";
        return
            std::filesystem::exists(node_modules / "@cursor" / "sdk") &&
            std::filesystem::exists(node_modules / "@modelcontextprotocol" / "sdk") &&
            std::filesystem::exists(node_modules / "zod");
    }

    bool ensure_assistant_dependencies(const std::filesystem::path& script_directory)
    {
        if (assistant_dependencies_ready(script_directory))
        {
            return true;
        }

        set_busy(true, "installing assistant dependencies");
        log_info("Verifying MCP assistant dependencies.");
    #ifdef _WIN32
        return run_process_and_wait(
            "cmd.exe /c npm install --silent",
            script_directory,
            "failed to install MCP assistant dependencies. install Node.js, then try again."
        );
    #else
        return run_process_and_wait(
            "npm install --silent",
            script_directory,
            "failed to install MCP assistant dependencies. install Node.js, then try again."
        );
    #endif
    }

    bool is_assistant_ready()
    {
    #ifdef _WIN32
        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return false;
        }
    #endif

        socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket)
        {
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        sockaddr_in address = {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(assistant_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        const bool ready = connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        close_socket(socket);
    #ifdef _WIN32
        WSACleanup();
    #endif

        return ready;
    }

    bool wait_for_assistant_ready()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(assistant_start_timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (is_assistant_ready())
            {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(assistant_start_poll_ms));
        }

        return false;
    }

    bool start_assistant_process()
    {
        std::lock_guard<std::mutex> start_lock(assistant_start_mutex);
        if (is_assistant_ready())
        {
            return true;
        }

        std::filesystem::path script_path;
        if (!find_assistant_script(script_path))
        {
            log_error("assistant.mjs was not found under tools/mcp/spartan_engine.");
            return false;
        }

        const std::filesystem::path script_directory = script_path.parent_path();
        if (!ensure_assistant_dependencies(script_directory))
        {
            return false;
        }

        set_busy(true, "starting assistant");
        const std::string command =
            "node \"" + script_path.string() + "\" --port=" + std::to_string(assistant_port) +
            " --engine-port=" + std::to_string(spartan::McpServer::GetPort());

    #ifdef _WIN32
        STARTUPINFOA startup_info = {};
        PROCESS_INFORMATION process_info = {};
        startup_info.cb = sizeof(startup_info);

        std::string command_mutable = command;
        std::string working_directory = script_directory.string();
        BOOL success = CreateProcessA(
            nullptr,
            command_mutable.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            working_directory.c_str(),
            &startup_info,
            &process_info
        );

        if (!success)
        {
            log_error("failed to start node assistant process. make sure node is installed and in PATH.");
            return false;
        }

        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return wait_for_assistant_ready();
    #else
        const std::string shell_command = command + " >/dev/null 2>&1 &";
        if (std::system(shell_command.c_str()) != 0)
        {
            return false;
        }

        return wait_for_assistant_ready();
    #endif
    }

    bool send_all(socket_t socket, const std::string& data)
    {
        size_t bytes_sent = 0;
        while (bytes_sent < data.size())
        {
            const int sent = send(socket, data.data() + bytes_sent, static_cast<int>(data.size() - bytes_sent), 0);
            if (sent <= 0)
            {
                return false;
            }

            bytes_sent += static_cast<size_t>(sent);
        }

        return true;
    }

    bool set_receive_timeout(socket_t socket, int milliseconds)
    {
    #ifdef _WIN32
        DWORD timeout = static_cast<DWORD>(milliseconds);
        return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
    #else
        timeval timeout = {};
        timeout.tv_sec = milliseconds / 1000;
        timeout.tv_usec = (milliseconds % 1000) * 1000;
        return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
    #endif
    }

    bool parse_assistant_line(const std::string& line, std::string& response, bool& final)
    {
        const size_t separator = line.find(' ');
        if (separator == std::string::npos)
        {
            response = "assistant returned an invalid response";
            final = true;
            return false;
        }

        const std::string status = line.substr(0, separator);
        const std::string text = url_decode(line.substr(separator + 1));
        if (status == "activity")
        {
            set_busy(true, text);
            final = false;
            return true;
        }

        response = text;
        final = true;
        return status == "ok";
    }

    bool read_assistant_response(socket_t socket, std::string& response)
    {
        std::string buffer;
        char receive_buffer[4096];

        while (true)
        {
            const int received = recv(socket, receive_buffer, sizeof(receive_buffer), 0);
            if (received <= 0)
            {
                response = "assistant did not return a response before timeout";
                return false;
            }

            buffer.append(receive_buffer, received);
            size_t newline = buffer.find('\n');
            while (newline != std::string::npos)
            {
                std::string line = buffer.substr(0, newline);
                buffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    bool final = false;
                    const bool ok = parse_assistant_line(line, response, final);
                    if (final)
                    {
                        return ok;
                    }
                }

                newline = buffer.find('\n');
            }
        }
    }

    bool send_prompt_to_assistant(const std::string& prompt, const std::string& api_key, const std::string& model_id, std::string& response)
    {
    #ifdef _WIN32
        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            response = "failed to initialize Winsock";
            return false;
        }
    #endif

        socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket)
        {
            response = "failed to create assistant socket";
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        sockaddr_in address = {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(assistant_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            response = "assistant is not running";
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        set_receive_timeout(socket, assistant_prompt_timeout_ms);

        const std::string request = "prompt api_key=" + url_encode(api_key) + "&model=" + url_encode(model_id) + "&prompt=" + url_encode(prompt) + "\n";
        if (!send_all(socket, request))
        {
            response = "failed to send prompt to assistant";
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        const bool ok = read_assistant_response(socket, response);
        close_socket(socket);
    #ifdef _WIN32
        WSACleanup();
    #endif

        return ok;
    }

    bool send_models_to_assistant(const std::string& api_key, std::string& response)
    {
    #ifdef _WIN32
        WSADATA data = {};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            response = "failed to initialize Winsock";
            return false;
        }
    #endif

        socket_t socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket)
        {
            response = "failed to create assistant socket";
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        sockaddr_in address = {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(assistant_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            response = "assistant is not running";
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        set_receive_timeout(socket, assistant_model_timeout_ms);

        const std::string request = "models api_key=" + url_encode(api_key) + "\n";
        if (!send_all(socket, request))
        {
            response = "failed to request Cursor models";
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        std::string buffer;
        char receive_buffer[4096];
        while (buffer.find('\n') == std::string::npos)
        {
            const int received = recv(socket, receive_buffer, sizeof(receive_buffer), 0);
            if (received <= 0)
            {
                response = "assistant did not return models before timeout";
                close_socket(socket);
            #ifdef _WIN32
                WSACleanup();
            #endif
                return false;
            }

            buffer.append(receive_buffer, received);
        }

        close_socket(socket);
    #ifdef _WIN32
        WSACleanup();
    #endif

        const size_t newline = buffer.find('\n');
        std::string line = buffer.substr(0, newline);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const size_t separator = line.find(' ');
        if (separator == std::string::npos)
        {
            response = "assistant returned an invalid model response";
            return false;
        }

        const std::string status = line.substr(0, separator);
        response = url_decode(line.substr(separator + 1));
        return status == "ok";
    }
}

McpAssistant::McpAssistant(Editor* editor) : Widget(editor)
{
    m_title        = "MCP Assistant";
    m_visible      = false;
    m_size_initial = spartan::math::Vector2(520.0f, 340.0f);
    m_size_min     = spartan::math::Vector2(360.0f, 260.0f);
    m_api_key_file_status = "Will look for cursor_api_key.txt next to the exe when opened.";
    assistant_response.clear();
}

void McpAssistant::OnTick()
{
    if (!m_visible && m_blocks_input)
    {
        m_blocks_input = false;
        spartan::Input::SetBlockedByUi(false);
    }
}

void McpAssistant::OnInvisible()
{
    m_blocks_input = false;
    spartan::Input::SetBlockedByUi(false);
}

bool McpAssistant::LoadApiKeyFromFile()
{
    if (m_api_key_file_checked)
    {
        return false;
    }

    m_api_key_file_checked = true;
    std::string api_key;
    std::filesystem::path loaded_path;
    std::vector<std::filesystem::path> searched_paths;
    if (!read_api_key_file(api_key, loaded_path, searched_paths))
    {
        m_api_key_file_status =
            searched_paths.empty() ?
            "No cursor_api_key.txt found next to the exe or working directory." :
            "No cursor_api_key.txt found. First checked " + searched_paths.front().string();
        return false;
    }

    if (api_key.size() >= m_cursor_api_key.size())
    {
        m_api_key_file_status = "cursor_api_key.txt was found, but the key is too long.";
        log_error("cursor_api_key.txt key is too long.");
        return false;
    }

    std::fill(m_cursor_api_key.begin(), m_cursor_api_key.end(), '\0');
    std::copy(api_key.begin(), api_key.end(), m_cursor_api_key.begin());
    m_api_key_file_status = "Loaded Cursor API key from " + loaded_path.string();
    log_info(m_api_key_file_status);
    return true;
}

void McpAssistant::OnTickVisible()
{
    if (!m_visible)
    {
        m_blocks_input = false;
        spartan::Input::SetBlockedByUi(false);
        return;
    }

    if (m_cursor_api_key[0] == '\0' && LoadApiKeyFromFile())
    {
        m_refresh_models_after_key_load = true;
    }

    const bool has_api_key = m_cursor_api_key[0] != '\0';
    if (has_api_key && !m_mcp_auto_start_attempted && !spartan::McpServer::IsRunning())
    {
        m_mcp_auto_start_attempted = true;
        spartan::McpServer::Start();
    }

    const bool is_running = spartan::McpServer::IsRunning();
    bool is_assistant_busy = false;
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        is_assistant_busy = assistant_busy;
    }

    if (has_api_key && m_refresh_models_after_key_load && !is_assistant_busy)
    {
        m_refresh_models_after_key_load = false;
        RefreshModels();
        is_assistant_busy = true;
    }

    if (std::string response = take_response(); !response.empty())
    {
        m_messages.push_back({ false, response });
        m_scroll_to_bottom = true;
    }

    if (std::string model_list = take_models(); !model_list.empty())
    {
        ApplyModelList(model_list);
    }

    UpdateInputOwnership();

    ImGui::TextUnformatted("Cursor key");
    ImGui::SameLine();
    ImGui::PushItemWidth(-170.0f);
    ImGui::InputText("##cursor_api_key", m_cursor_api_key.data(), m_cursor_api_key.size(), ImGuiInputTextFlags_Password);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Get key"))
    {
        spartan::FileSystem::OpenUrl("https://cursor.com/dashboard/api?section=user-keys#user-api-keys");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear key"))
    {
        m_cursor_api_key[0] = '\0';
    }

    if (!has_api_key)
    {
        ImGui::TextWrapped("%s", m_api_key_file_status.c_str());
        ImGui::TextWrapped("Paste your key here first. It is kept in memory for this Spartan session and is not written to disk.");
        return;
    }

    if (!m_api_key_file_status.empty())
    {
        ImGui::TextDisabled("%s", m_api_key_file_status.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Engine");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextColored(is_running ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f) : ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s", is_running ? "active" : "inactive");

    if (is_running)
    {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("127.0.0.1:%u", spartan::McpServer::GetPort());
        ImGui::SameLine();
        if (ImGui::Button("Stop MCP"))
        {
            spartan::McpServer::Shutdown();
            log_info("MCP stopped.");
        }
    }
    else if (ImGui::Button("Start MCP"))
    {
        spartan::McpServer::Start();
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextUnformatted("Agent");
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::PushItemWidth(210.0f);
    const char* model_preview = m_model_labels.empty() ? "Auto" : m_model_labels[static_cast<size_t>(m_model_index)].c_str();
    if (ImGui::BeginCombo("##mcp_agent", model_preview))
    {
        for (int i = 0; i < static_cast<int>(m_model_labels.size()); i++)
        {
            const bool selected = i == m_model_index;
            if (ImGui::Selectable(m_model_labels[static_cast<size_t>(i)].c_str(), selected))
            {
                m_model_index = i;
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (is_assistant_busy)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Refresh agents"))
    {
        RefreshModels();
    }
    if (is_assistant_busy)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Restart assistant"))
    {
        RestartAssistant();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Kill and relaunch the Node assistant helper so updated tools and prompt load.");
    }

    ImGui::Separator();

    const float composer_height = ImGui::GetTextLineHeight() * 5.5f + ImGui::GetStyle().ItemSpacing.y * 5.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.045f, 0.45f));
    if (ImGui::BeginChild("##mcp_chat_history", ImVec2(0.0f, -composer_height), true))
    {
        if (m_messages.empty() && !is_assistant_busy)
        {
            ImGui::TextDisabled("Ask Spartan AI to inspect or control the running engine.");
            ImGui::TextDisabled("Examples: what is selected, create an empty entity, summarize the world.");
        }

        for (int i = 0; i < static_cast<int>(m_messages.size()); i++)
        {
            DrawChatMessage(m_messages[i], i);
        }

        if (is_assistant_busy)
        {
            DrawChatMessage({ false, thinking_text() }, static_cast<int>(m_messages.size()));
        }

        if (m_scroll_to_bottom || is_assistant_busy)
        {
            ImGui::SetScrollHereY(1.0f);
            m_scroll_to_bottom = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::TextUnformatted("Message");
    ImGui::InputTextMultiline(
        "##mcp_prompt",
        m_prompt.data(),
        m_prompt.size(),
        ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f)
    );

    const bool has_prompt = m_prompt[0] != '\0';
    if (!has_prompt)
    {
        ImGui::BeginDisabled();
    }

    if (is_assistant_busy)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Send"))
    {
        SubmitPrompt();
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_assistant_busy)
    {
        ImGui::SetTooltip("Spartan AI is still working.");
    }

    if (is_assistant_busy)
    {
        ImGui::EndDisabled();
    }

    if (!has_prompt)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        m_prompt[0] = '\0';
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear chat"))
    {
        m_messages.clear();
        set_response("");
    }
}

void McpAssistant::SubmitPrompt()
{
    if (!spartan::McpServer::IsRunning())
    {
        spartan::McpServer::Start();
    }

    std::string prompt = m_prompt.data();
    std::string api_key = trim_copy_paste_whitespace(m_cursor_api_key.data());
    std::string model_id = GetSelectedModelId();
    if (api_key.empty())
    {
        log_error("Cursor API key is empty.");
        return;
    }

    m_messages.push_back({ true, prompt });
    m_prompt[0] = '\0';
    m_scroll_to_bottom = true;
    set_response("");
    set_busy(true, "thinking");
    log_info("Sending prompt to Cursor with model " + model_id + ".");

    std::thread([prompt, api_key, model_id]()
    {
        std::string response;
        if (!send_prompt_to_assistant(prompt, api_key, model_id, response))
        {
            log_info("Starting Cursor assistant.");
            if (!start_assistant_process())
            {
                set_response("failed to start Cursor assistant. install Node.js and run npm install under tools/mcp/spartan_engine, then try again.");
                set_busy(false);
                return;
            }

            if (!send_prompt_to_assistant(prompt, api_key, model_id, response))
            {
                response = response.empty() ? "failed to connect to Cursor assistant. install Node.js, then try again." : response;
                log_error(response);
                set_response(response);
                set_busy(false);
                return;
            }
        }

        set_response(response);
        set_busy(false);
    }).detach();
}

void McpAssistant::RefreshModels()
{
    std::string api_key = trim_copy_paste_whitespace(m_cursor_api_key.data());
    if (api_key.empty())
    {
        log_error("Cursor API key is empty.");
        return;
    }

    set_busy(true, "loading agents");
    log_info("Refreshing Cursor agent list.");

    std::thread([api_key]()
    {
        std::string response;
        if (!send_models_to_assistant(api_key, response))
        {
            log_info("Starting Cursor assistant.");
            if (!start_assistant_process())
            {
                set_response("failed to start Cursor assistant. install Node.js and run npm install under tools/mcp/spartan_engine, then try again.");
                set_busy(false);
                return;
            }

            if (!send_models_to_assistant(api_key, response))
            {
                response = response.empty() ? "failed to refresh Cursor agents." : response;
                log_error(response);
                set_response(response);
                set_busy(false);
                return;
            }
        }

        set_models(response);
        set_busy(false);
    }).detach();
}

void McpAssistant::RestartAssistant()
{
    set_busy(true, "restarting assistant");
    log_info("Restarting Cursor assistant.");

    std::thread([]()
    {
        const bool killed = kill_assistant_process();
        if (killed)
        {
            log_info("Stopped the running Cursor assistant.");
        }

        if (!start_assistant_process())
        {
            set_response("failed to restart Cursor assistant. install Node.js and run npm install under tools/mcp/spartan_engine, then try again.");
            set_busy(false);
            return;
        }

        set_response("Cursor assistant restarted. Updated tools and prompt are now loaded.");
        set_busy(false);
    }).detach();
}

void McpAssistant::ApplyModelList(const std::string& model_list)
{
    const std::string previous_model = GetSelectedModelId();
    m_model_ids.clear();
    m_model_labels.clear();

    std::istringstream stream(model_list);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

        const size_t separator = line.find('\t');
        const std::string id = trim_copy_paste_whitespace(separator == std::string::npos ? line : line.substr(0, separator));
        const std::string label = trim_copy_paste_whitespace(separator == std::string::npos ? line : line.substr(separator + 1));
        if (id.empty())
        {
            continue;
        }

        m_model_ids.push_back(id);
        m_model_labels.push_back(label.empty() ? id : label + " (" + id + ")");
    }

    if (m_model_ids.empty())
    {
        m_model_ids.push_back("auto");
        m_model_labels.push_back("Auto");
    }

    m_model_index = 0;
    for (int i = 0; i < static_cast<int>(m_model_ids.size()); i++)
    {
        if (m_model_ids[static_cast<size_t>(i)] == previous_model)
        {
            m_model_index = i;
            break;
        }
    }
}

std::string McpAssistant::GetSelectedModelId() const
{
    if (m_model_ids.empty() || m_model_index < 0 || m_model_index >= static_cast<int>(m_model_ids.size()))
    {
        return "auto";
    }

    return m_model_ids[static_cast<size_t>(m_model_index)];
}

void McpAssistant::DrawChatMessage(const ChatMessage& message, int index)
{
    const float width_available = ImGui::GetContentRegionAvail().x;
    const float bubble_width = std::max(220.0f, width_available * 0.76f);
    const float bubble_width_clamped = std::min(bubble_width, width_available);

    if (message.is_user)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, width_available - bubble_width_clamped));
    }

    const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    const ImVec4 user_bg = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    const ImVec4 assistant_bg = ImVec4(0.12f, 0.12f, 0.135f, 0.92f);
    const ImVec4 role_color = message.is_user ? ImVec4(0.82f, 0.92f, 1.0f, 1.0f) : ImVec4(0.70f, 0.95f, 0.75f, 1.0f);

    ImGui::PushID(index);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, message.is_user ? user_bg : assistant_bg);

    if (ImGui::BeginChild("##mcp_message", ImVec2(bubble_width_clamped, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        ImGui::TextColored(role_color, "%s", message.is_user ? "You" : "Spartan AI");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", message.text.c_str());
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::Spacing();
}

void McpAssistant::UpdateInputOwnership()
{
    const bool owns_input =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    m_blocks_input = owns_input;
    spartan::Input::SetBlockedByUi(owns_input);

    if (owns_input)
    {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        ImGui::SetNextFrameWantCaptureMouse(true);
    }
}
