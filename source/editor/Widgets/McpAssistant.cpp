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
#include <cctype>
#include <chrono>
#include <cstdlib>
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
    #include <Windows.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace
{
    constexpr uint16_t assistant_port = 47778;
    std::mutex assistant_mutex;
    std::string assistant_response;
    std::string assistant_models;
    std::string assistant_activity;
    bool assistant_busy = false;

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
        assistant_busy = busy;
        assistant_activity = activity;
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

    std::string thinking_text()
    {
        const int dot_count = static_cast<int>(ImGui::GetTime() * 2.5) % 4;
        std::string activity;
        {
            std::lock_guard<std::mutex> lock(assistant_mutex);
            activity = assistant_activity.empty() ? "thinking" : assistant_activity;
        }

        return activity + std::string(static_cast<size_t>(dot_count), '.');
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

    bool ensure_assistant_dependencies(const std::filesystem::path& script_directory)
    {
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

    bool start_assistant_process()
    {
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
        return true;
    #else
        const std::string shell_command = command + " >/dev/null 2>&1 &";
        return std::system(shell_command.c_str()) == 0;
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
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        set_receive_timeout(socket, 210000);

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

        std::string buffer;
        char receive_buffer[4096];
        while (buffer.find('\n') == std::string::npos)
        {
            const int received = recv(socket, receive_buffer, sizeof(receive_buffer), 0);
            if (received <= 0)
            {
                response = "assistant did not return a response before timeout";
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
            response = "assistant returned an invalid response";
            return false;
        }

        const std::string status = line.substr(0, separator);
        response = url_decode(line.substr(separator + 1));
        return status == "ok";
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
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        set_receive_timeout(socket, 45000);

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

void McpAssistant::OnTickVisible()
{
    if (!m_visible)
    {
        m_blocks_input = false;
        spartan::Input::SetBlockedByUi(false);
        return;
    }

    const bool is_running = spartan::McpServer::IsRunning();
    const bool has_api_key = m_cursor_api_key[0] != '\0';
    bool is_assistant_busy = false;
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        is_assistant_busy = assistant_busy;
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
        ImGui::TextWrapped("Paste your key here first. It is kept in memory for this Spartan session and is not written to disk.");
        return;
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
                set_busy(false);
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            if (!send_prompt_to_assistant(prompt, api_key, model_id, response))
            {
                log_error(
                    response.empty() ?
                    "failed to connect to Cursor assistant. install Node.js, then try again." :
                    response
                );
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
                set_busy(false);
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            if (!send_models_to_assistant(api_key, response))
            {
                log_error(response.empty() ? "failed to refresh Cursor agents." : response);
                set_busy(false);
                return;
            }
        }

        set_models(response);
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
