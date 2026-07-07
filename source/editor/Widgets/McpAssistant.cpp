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
#include "../Editor.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "../ImGui/Source/imgui_internal.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
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
    #include <mmdeviceapi.h>
    #include <endpointvolume.h>
    #include <winrt/Windows.Foundation.h>
    #include <winrt/Windows.Foundation.Collections.h>
    #include <winrt/Windows.Media.SpeechRecognition.h>
    #pragma comment(lib, "ole32.lib")
    #pragma comment(lib, "advapi32.lib")
    #pragma comment(lib, "windowsapp.lib")
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
    bool assistant_busy = false;

    struct AssistantRunState
    {
        bool has_run = false;
        bool active = false;
        bool failed = false;
        bool cancelled = false;
        std::string id;
        std::string prompt;
        std::string intent;
        std::string status;
        std::string summary;
        std::vector<std::string> event_lines;
        std::chrono::steady_clock::time_point started_at;
        int elapsed_ms = 0;
    };

    AssistantRunState assistant_run;
    bool assistant_spawned_this_session = false;
#ifdef _WIN32
    uint32_t tracked_assistant_pid = 0;
#endif

    float ui_scale()
    {
        return spartan::Window::GetDpiScale();
    }

    ImVec4 with_alpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    ImVec4 softened(const ImVec4& color, float amount, float alpha)
    {
        return with_alpha(ImGui::Style::lerp(color, ImGui::Style::bg_color_1, amount), alpha);
    }

    bool push_bold_font()
    {
        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, 0.0f);
            return true;
        }

        return false;
    }

    void pop_bold_font(bool pushed)
    {
        if (pushed)
        {
            ImGui::PopFont();
        }
    }

    void draw_section_title(const char* title, const char* subtitle = nullptr)
    {
        const bool bold_font = push_bold_font();
        ImGui::TextUnformatted(title);
        pop_bold_font(bold_font);

        if (subtitle && subtitle[0] != '\0')
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", subtitle);
            ImGui::PopStyleColor();
        }
    }

    bool begin_card(const char* id, float height = 0.0f)
    {
        const float scale = ui_scale();
        const ImVec4 bg = ImGui::Style::lerp(ImGui::Style::bg_color_1, ImGui::Style::bg_color_2, 0.18f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 9.0f * scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, with_alpha(bg, 0.84f));
        ImGui::PushStyleColor(ImGuiCol_Border, with_alpha(ImGui::Style::color_accent_1, 0.16f));
        return ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    }

    void end_card()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    bool primary_button(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        const ImVec4 accent = ImGui::Style::color_accent_1;
        ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(accent, 0.34f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(accent, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(accent, 0.26f));
        const bool clicked = ImGuiSp::button(label, size);
        ImGui::PopStyleColor(3);
        return clicked;
    }

    void draw_status_pill(const char* label, const ImVec4& color)
    {
        const float scale = ui_scale();
        const ImVec2 padding = ImVec2(8.0f * scale, 3.0f * scale);
        const ImVec2 text_size = ImGui::CalcTextSize(label);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImVec2(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(with_alpha(color, 0.16f)), size.y * 0.5f);
        draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(with_alpha(color, 0.52f)), size.y * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + padding.x, pos.y + padding.y));
        ImGui::TextColored(color, "%s", label);
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(size);
    }

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

    bool terminate_tracked_assistant_process()
    {
    #ifdef _WIN32
        if (tracked_assistant_pid == 0)
        {
            return false;
        }

        const uint32_t pid = tracked_assistant_pid;
        tracked_assistant_pid = 0;
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (process == nullptr)
        {
            return false;
        }

        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
        return true;
    #else
        return std::system("pkill -f assistant.mjs >/dev/null 2>&1") == 0;
    #endif
    }

    bool terminate_any_assistant_process()
    {
    #ifdef _WIN32
        const char* command =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
            "Get-CimInstance Win32_Process -Filter \\\"name = 'node.exe'\\\" | "
            "Where-Object { $_.CommandLine -like '*tools\\\\mcp\\\\spartan_engine\\\\assistant.mjs*' -or $_.CommandLine -like '*tools/mcp/spartan_engine/assistant.mjs*' } | "
            "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
            "\"";
        return std::system(command) == 0;
    #else
        return std::system("pkill -f assistant.mjs >/dev/null 2>&1") == 0;
    #endif
    }

    bool kill_assistant_process()
    {
    #ifdef _WIN32
        if (terminate_tracked_assistant_process())
        {
            return true;
        }

        return terminate_any_assistant_process();
    #else
        return terminate_tracked_assistant_process();
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
        std::string line = message;
        std::replace(line.begin(), line.end(), '\n', ' ');
        std::replace(line.begin(), line.end(), '\r', ' ');
        SP_LOG_ERROR("MCP Assistant: %s", line.c_str());
    }

#ifdef _WIN32
    std::thread voice_thread;
    std::atomic<bool> voice_stop_flag = false;
    std::atomic<bool> voice_listening = false;
    std::atomic<bool> voice_failed = false;
    std::atomic<float> voice_level = 0.0f;
    std::mutex voice_text_mutex;
    std::string voice_text_pending;

    void voice_accept_speech_privacy_policy()
    {
        // pressing the voice button is the consent, newer windows builds no longer show the settings toggle
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Speech_OneCore\\Settings\\OnlineSpeechPrivacy", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD accepted = 1;
            RegSetValueExW(key, L"HasAccepted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&accepted), sizeof(accepted));
            RegCloseKey(key);
        }
    }

    void voice_run()
    {
        namespace speech = winrt::Windows::Media::SpeechRecognition;

        voice_accept_speech_privacy_policy();
        speech::SpeechRecognizer recognizer;
        recognizer.Constraints().Append(speech::SpeechRecognitionTopicConstraint(speech::SpeechRecognitionScenario::Dictation, L"dictation"));
        const auto compile_status = recognizer.CompileConstraintsAsync().get().Status();
        if (compile_status != speech::SpeechRecognitionResultStatus::Success)
        {
            voice_failed.store(true);
            log_error("Voice input could not compile the dictation grammar, status " + std::to_string(static_cast<int>(compile_status)) + ".");
            return;
        }

        auto session = recognizer.ContinuousRecognitionSession();
        session.AutoStopSilenceTimeout(std::chrono::minutes(30));
        session.ResultGenerated([](auto const&, speech::SpeechContinuousRecognitionResultGeneratedEventArgs const& args)
        {
            const std::string text = winrt::to_string(args.Result().Text());
            if (!text.empty())
            {
                std::lock_guard<std::mutex> lock(voice_text_mutex);
                if (!voice_text_pending.empty())
                {
                    voice_text_pending += ' ';
                }
                voice_text_pending += text;
            }
        });
        session.Completed([](auto const&, speech::SpeechContinuousRecognitionCompletedEventArgs const& args)
        {
            if (!voice_stop_flag.load())
            {
                voice_failed.store(true);
                log_error("Voice input session ended unexpectedly, status " + std::to_string(static_cast<int>(args.Status())) + ".");
            }
        });
        session.StartAsync().get();
        voice_listening.store(true);
        log_info("Voice input is listening, dictated text is appended to the message box.");

        // default microphone peak meter for the ui
        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        winrt::com_ptr<IMMDevice> device;
        winrt::com_ptr<IAudioMeterInformation> meter;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), enumerator.put_void())))
        {
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, device.put())))
            {
                device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, meter.put_void());
            }
        }

        while (!voice_stop_flag.load())
        {
            if (meter)
            {
                float peak = 0.0f;
                if (SUCCEEDED(meter->GetPeakValue(&peak)))
                {
                    voice_level.store(peak);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        try
        {
            session.StopAsync().get();
        }
        catch (winrt::hresult_error const&)
        {
            // the session may have already completed on its own
        }
    }

    void voice_thread_main()
    {
        winrt::init_apartment();
        try
        {
            voice_run();
        }
        catch (winrt::hresult_error const& error)
        {
            voice_failed.store(true);
            if (error.code() == static_cast<winrt::hresult>(0x80045509))
            {
                log_error("Voice input needs online speech recognition enabled, turn it on in the settings page that just opened.");
                spartan::FileSystem::OpenUrl("ms-settings:privacy-speech");
            }
            else
            {
                log_error("Voice input failed: " + winrt::to_string(error.message()) + " Check microphone access and online speech recognition in windows privacy settings.");
            }
        }
        voice_listening.store(false);
        voice_level.store(0.0f);
        winrt::uninit_apartment();
    }

    void draw_voice_meter(const std::array<float, 64>& history, int head)
    {
        const float scale = ui_scale();
        const ImVec2 size = ImVec2(110.0f * scale, ImGui::GetFrameHeight());
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int bar_count = static_cast<int>(history.size());
        const float bar_stride = size.x / static_cast<float>(bar_count);
        const float bar_width = std::max(1.0f, bar_stride - 1.0f * scale);
        const float center_y = pos.y + size.y * 0.5f;
        const bool ready = voice_listening.load();
        const ImU32 color = ImGui::GetColorU32(ready ? ImVec4(0.35f, 0.8f, 0.45f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 0.6f));
        for (int i = 0; i < bar_count; i++)
        {
            const float value = history[(head + i) % bar_count];
            const float half_height = std::max(1.0f * scale, value * size.y * 0.5f);
            const float x = pos.x + bar_stride * static_cast<float>(i);
            draw_list->AddRectFilled(ImVec2(x, center_y - half_height), ImVec2(x + bar_width, center_y + half_height), color, bar_width * 0.5f);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(ready ? "Microphone level." : "Starting the speech engine...");
        }
    }
#endif

    std::string trim_copy_paste_whitespace(const std::string& value);
    void set_run_status_locked(const std::string& status);

    bool is_generic_activity(const std::string& activity)
    {
        std::string value = activity;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        value = trim_copy_paste_whitespace(value);
        return
            value.empty() ||
            value == "thinking" ||
            value == "writing" ||
            value == "using mcp" ||
            value == "using tool" ||
            value == "tool call" ||
            value == "callmcptool" ||
            (value.find("thinking") != std::string::npos && value.size() < 32) ||
            (value.find("writing") != std::string::npos && value.size() < 32);
    }

    void set_busy(bool busy, const std::string& activity = "")
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        assistant_busy = busy;
        const std::string visible_activity = is_generic_activity(activity) ? "working on request" : activity;
        assistant_activity = visible_activity;
        if (busy && !visible_activity.empty())
        {
            if (!assistant_run.has_run || !assistant_run.active)
            {
                assistant_run = AssistantRunState();
                assistant_run.has_run = true;
                assistant_run.active = true;
                assistant_run.status = visible_activity;
                assistant_run.elapsed_ms = 0;
                assistant_run.started_at = std::chrono::steady_clock::now();
            }
            else
            {
                set_run_status_locked(visible_activity);
            }
        }
    }

    void finish_run_locally(bool failed, bool cancelled, const std::string& summary)
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        assistant_busy = false;
        assistant_activity.clear();
        assistant_run.active = false;
        assistant_run.failed = failed;
        assistant_run.cancelled = cancelled;
        assistant_run.summary = summary;
        set_run_status_locked(cancelled ? "cancelled" : (failed ? "failed" : "done"));
    }

    void start_local_run(const std::string& prompt, const std::string& status)
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        assistant_run = AssistantRunState();
        assistant_run.has_run = true;
        assistant_run.active = true;
        assistant_run.prompt = prompt;
        assistant_run.intent = "starting";
        assistant_run.status = status;
        assistant_run.started_at = std::chrono::steady_clock::now();
        assistant_busy = true;
        assistant_activity = status;
    }

    void set_run_status_locked(const std::string& status)
    {
        if (status.empty() || status == assistant_run.status)
        {
            return;
        }

        // the previous status becomes history, so the status line accumulates over time
        if (!assistant_run.status.empty() && assistant_run.status != "starting")
        {
            assistant_run.event_lines.emplace_back(assistant_run.status);
            if (assistant_run.event_lines.size() > 256)
            {
                assistant_run.event_lines.erase(assistant_run.event_lines.begin());
            }
        }

        assistant_run.status = status;
        assistant_activity = status;
    }

    AssistantRunState get_run_snapshot()
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);
        return assistant_run;
    }

    std::string json_get_string(const std::string& json, const std::string& key);
    int json_get_int(const std::string& json, const std::string& key, int fallback = 0);

    void handle_assistant_event(const std::string& json)
    {
        std::lock_guard<std::mutex> lock(assistant_mutex);

        const std::string type = json_get_string(json, "type");
        if (type.empty())
        {
            return;
        }

        if (type == "run_started")
        {
            assistant_run = AssistantRunState();
            assistant_run.has_run = true;
            assistant_run.active = true;
            assistant_run.id = json_get_string(json, "run_id");
            assistant_run.prompt = json_get_string(json, "prompt");
            assistant_run.intent = json_get_string(json, "intent");
            assistant_run.elapsed_ms = json_get_int(json, "elapsed_ms");
            assistant_run.started_at = std::chrono::steady_clock::now();
            assistant_run.status = "starting";
            assistant_busy = true;
            assistant_activity = "starting";
        }
        else if (type == "stage_started")
        {
            const std::string title = json_get_string(json, "title");
            const std::string status = json_get_string(json, "status");
            set_run_status_locked(status.empty() ? title : status);
        }
        else if (type == "stage_finished")
        {
            if (json_get_string(json, "status") == "failed")
            {
                set_run_status_locked(json_get_string(json, "error"));
            }
        }
        else if (type == "tool_started")
        {
            const std::string name = json_get_string(json, "name");
            if (!name.empty())
            {
                set_run_status_locked("running " + name);
            }
        }
        else if (type == "tool_finished")
        {
            const std::string name = json_get_string(json, "name");
            if (!name.empty())
            {
                const std::string prefix = json.find("\"ok\":false") == std::string::npos ? "finished " : "failed ";
                set_run_status_locked(prefix + name);
            }
        }
        else if (type == "receipt")
        {
            set_run_status_locked(json_get_string(json, "title"));
        }
        else if (type == "heartbeat" || type == "stage_note")
        {
            const std::string status = json_get_string(json, type == "stage_note" ? "text" : "status");
            if (!is_generic_activity(status))
            {
                set_run_status_locked(status);
            }
        }
        else if (type == "run_finished")
        {
            assistant_run.active = false;
            assistant_run.summary = json_get_string(json, "summary");
            assistant_run.elapsed_ms = json_get_int(json, "elapsed_ms");
            set_run_status_locked("done");
        }
        else if (type == "run_failed")
        {
            assistant_run.active = false;
            assistant_run.failed = true;
            assistant_run.summary = json_get_string(json, "summary");
            assistant_run.elapsed_ms = json_get_int(json, "elapsed_ms");
            set_run_status_locked("failed");
        }
        else if (type == "run_cancelled")
        {
            assistant_run.active = false;
            assistant_run.cancelled = true;
            assistant_run.summary = json_get_string(json, "summary");
            assistant_run.elapsed_ms = json_get_int(json, "elapsed_ms");
            set_run_status_locked("cancelled");
        }

        assistant_run.elapsed_ms = json_get_int(json, "elapsed_ms", assistant_run.elapsed_ms);
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

    std::string json_unescape(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (size_t i = 0; i < value.size(); i++)
        {
            if (value[i] != '\\' || i + 1 >= value.size())
            {
                result.push_back(value[i]);
                continue;
            }

            const char escaped = value[++i];
            if (escaped == 'n')
            {
                result.push_back('\n');
            }
            else if (escaped == 'r')
            {
                result.push_back('\r');
            }
            else if (escaped == 't')
            {
                result.push_back('\t');
            }
            else
            {
                result.push_back(escaped);
            }
        }

        return result;
    }

    size_t json_value_start(const std::string& json, const std::string& key)
    {
        const std::string token = "\"" + key + "\":";
        const size_t key_pos = json.find(token);
        if (key_pos == std::string::npos)
        {
            return std::string::npos;
        }

        size_t pos = key_pos + token.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        {
            pos++;
        }
        return pos;
    }

    std::string json_get_string(const std::string& json, const std::string& key)
    {
        size_t pos = json_value_start(json, key);
        if (pos == std::string::npos || pos >= json.size() || json[pos] != '"')
        {
            return "";
        }

        pos++;
        std::string value;
        bool escaped = false;
        for (; pos < json.size(); pos++)
        {
            const char c = json[pos];
            if (escaped)
            {
                value.push_back('\\');
                value.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\')
            {
                escaped = true;
                continue;
            }
            if (c == '"')
            {
                break;
            }
            value.push_back(c);
        }

        return json_unescape(value);
    }

    int json_get_int(const std::string& json, const std::string& key, int fallback)
    {
        const size_t pos = json_value_start(json, key);
        if (pos == std::string::npos)
        {
            return fallback;
        }

        try
        {
            return std::stoi(json.substr(pos));
        }
        catch (...)
        {
            return fallback;
        }
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
            if (assistant_spawned_this_session)
            {
                return true;
            }

            // an assistant left over from a previous session runs stale code, replace it
            log_info("Replacing an assistant from a previous session so the current code is loaded.");
            kill_assistant_process();
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

        tracked_assistant_pid = static_cast<uint32_t>(process_info.dwProcessId);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        if (!wait_for_assistant_ready())
        {
            terminate_tracked_assistant_process();
            return false;
        }
        assistant_spawned_this_session = true;
        return true;
    #else
        const std::string shell_command = command + " >/dev/null 2>&1 &";
        if (std::system(shell_command.c_str()) != 0)
        {
            return false;
        }

        assistant_spawned_this_session = wait_for_assistant_ready();
        return assistant_spawned_this_session;
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
        if (status == "event")
        {
            handle_assistant_event(text);
            final = false;
            return true;
        }
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

    bool should_restart_assistant_after_failure(const std::string& response)
    {
        return
            response == "assistant is not running" ||
            response == "failed to initialize Winsock" ||
            response == "failed to create assistant socket" ||
            response == "failed to send prompt to assistant" ||
            response == "failed to send model request to assistant" ||
            response == "assistant did not return a response before timeout" ||
            response == "assistant returned an invalid response" ||
            response == "assistant returned an invalid model response";
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

    bool send_cancel_to_assistant(std::string& response)
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

        set_receive_timeout(socket, 5000);
        if (!send_all(socket, "cancel \n"))
        {
            response = "failed to send cancel request";
            close_socket(socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
            return false;
        }

        std::string buffer;
        char receive_buffer[1024];
        while (buffer.find('\n') == std::string::npos)
        {
            const int received = recv(socket, receive_buffer, sizeof(receive_buffer), 0);
            if (received <= 0)
            {
                response = "assistant did not acknowledge cancellation";
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
            response = "assistant returned an invalid cancel response";
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
    m_size_initial = spartan::math::Vector2(660.0f, 540.0f);
    m_size_min     = spartan::math::Vector2(460.0f, 360.0f);
    m_api_key_file_status = "Will look for cursor_api_key.txt next to the exe when opened.";
    assistant_response.clear();
}

McpAssistant::~McpAssistant()
{
    StopVoiceCapture();
    kill_assistant_process();
}

void McpAssistant::OnTick()
{
    DrainAssistantResults();

    if (!m_visible && m_blocks_input)
    {
        m_blocks_input = false;
        spartan::Input::SetBlockedByUi(false);
    }
}

void McpAssistant::OnInvisible()
{
    StopVoiceCapture();
    m_blocks_input = false;
    spartan::Input::SetBlockedByUi(false);
}

void McpAssistant::DrainAssistantResults()
{
    if (std::string response = take_response(); !response.empty())
    {
        m_messages.push_back({ false, response });
        m_scroll_to_bottom = true;
    }

    if (std::string model_list = take_models(); !model_list.empty())
    {
        ApplyModelList(model_list);
    }
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
        if (!spartan::McpServer::Start())
        {
            set_response("failed to start engine MCP bridge on 127.0.0.1:" + std::to_string(spartan::McpServer::GetPort()) + ".");
        }
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

    UpdateInputOwnership();

    const float scale = ui_scale();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 status_color = is_running ? ImGui::Style::color_ok : ImGui::Style::color_error;

    if (begin_card("##mcp_header"))
    {
        ImGuiSp::image(spartan::IconType::Mcp, 22.0f * scale, ImGui::Style::color_accent_1);
        ImGui::SameLine();
        ImGui::BeginGroup();
        const bool bold_font = push_bold_font();
        ImGui::TextUnformatted("Spartan AI");
        pop_bold_font(bold_font);
        ImGui::TextDisabled("Ask the engine-aware assistant to inspect, explain, and control the running scene.");
        ImGui::EndGroup();

        ImGui::SameLine();
        const float pill_width = (has_api_key ? 132.0f : 118.0f) * scale;
        const float pill_x = std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - pill_width);
        ImGui::SetCursorPosX(pill_x);
        draw_status_pill(is_assistant_busy ? "working" : (has_api_key ? "ready" : "setup required"), is_assistant_busy ? ImGui::Style::color_warning : (has_api_key ? ImGui::Style::color_ok : ImGui::Style::color_info));
    }
    end_card();

    ImGui::Spacing();

    if (!has_api_key)
    {
        if (begin_card("##mcp_setup_card"))
        {
            draw_section_title("Connect Cursor", "Paste a Cursor API key to start chatting with the assistant.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Cursor API key");

            const float button_width = 78.0f * scale;
            const float input_width = std::max(140.0f * scale, ImGui::GetContentRegionAvail().x - button_width * 2.0f - style.ItemSpacing.x * 2.0f);
            ImGui::SetNextItemWidth(input_width);
            ImGui::InputTextWithHint("##cursor_api_key", "paste key", m_cursor_api_key.data(), m_cursor_api_key.size(), ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (primary_button("Get key", ImVec2(button_width, 0.0f)))
            {
                spartan::FileSystem::OpenUrl("https://cursor.com/dashboard/api?section=user-keys#user-api-keys");
            }
            ImGui::SameLine();
            if (ImGuiSp::button("Clear", ImVec2(button_width, 0.0f)))
            {
                m_cursor_api_key[0] = '\0';
            }

            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_api_key_file_status.c_str());
            ImGui::TextWrapped("The key stays in memory for this Spartan session and is not written to disk.");
        }
        end_card();

        return;
    }

    if (begin_card("##mcp_controls_card"))
    {
        draw_section_title("Assistant Control", "Manage the local MCP bridge and choose the Cursor agent.");
        ImGui::Spacing();

        ImGui::TextUnformatted("Cursor");
        ImGui::SameLine(0.0f, 8.0f * scale);
        draw_status_pill("connected", ImGui::Style::color_ok);
        ImGui::SameLine(0.0f, 8.0f * scale);
        if (ImGuiSp::button("Clear key"))
        {
            m_cursor_api_key[0] = '\0';
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Engine");
        ImGui::SameLine(0.0f, 8.0f * scale);
        draw_status_pill(is_running ? "active" : "inactive", status_color);
        ImGui::SameLine(0.0f, 8.0f * scale);
        ImGui::TextDisabled("127.0.0.1:%u", spartan::McpServer::GetPort());
        ImGui::SameLine(0.0f, 10.0f * scale);
        if (is_running)
        {
            if (ImGuiSp::button("Stop MCP"))
            {
                spartan::McpServer::Shutdown();
                log_info("MCP stopped.");
            }
        }
        else
        {
            if (primary_button("Start MCP"))
            {
                if (!spartan::McpServer::Start())
                {
                    set_response("failed to start engine MCP bridge on 127.0.0.1:" + std::to_string(spartan::McpServer::GetPort()) + ".");
                }
            }
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Agent");
        ImGui::SameLine(0.0f, 8.0f * scale);
        const float actions_width = 224.0f * scale;
        ImGui::SetNextItemWidth(std::max(160.0f * scale, ImGui::GetContentRegionAvail().x - actions_width));
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

        ImGui::SameLine();
        if (is_assistant_busy)
        {
            ImGui::BeginDisabled();
        }
        if (ImGuiSp::button("Refresh"))
        {
            RefreshModels();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_assistant_busy)
        {
            ImGui::SetTooltip("Spartan AI is still working.");
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Restart"))
        {
            RestartAssistant();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Kill and relaunch the Node assistant helper so updated tools and prompt load.");
        }
        if (is_assistant_busy)
        {
            ImGui::EndDisabled();
        }

        if (!m_api_key_file_status.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_api_key_file_status.c_str());
        }
    }
    end_card();

    ImGui::Spacing();

    const float composer_height = std::max(132.0f * scale, ImGui::GetTextLineHeight() * 6.8f + style.ItemSpacing.y * 7.0f);
    const ImVec4 chat_bg = ImGui::Style::lerp(ImGui::Style::bg_color_1, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.22f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, with_alpha(chat_bg, 0.72f));
    if (ImGui::BeginChild("##mcp_chat_history", ImVec2(0.0f, -composer_height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        if (m_messages.empty() && !is_assistant_busy)
        {
            const bool bold_font = push_bold_font();
            ImGui::TextUnformatted("Start with a concrete engine task");
            pop_bold_font(bold_font);
            ImGui::TextDisabled("Try: what is selected, create an empty entity, summarize the world.");
            ImGui::TextDisabled("The assistant can use the running MCP bridge when the engine is active.");
        }

        for (int i = 0; i < static_cast<int>(m_messages.size()); i++)
        {
            DrawChatMessage(m_messages[i], i);
        }

        if (is_assistant_busy)
        {
            DrawAssistantRun();
        }

        if (m_scroll_to_bottom || is_assistant_busy)
        {
            ImGui::SetScrollHereY(1.0f);
            m_scroll_to_bottom = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::Spacing();

    if (begin_card("##mcp_composer_card"))
    {
        draw_section_title("Message");
        if (m_voice_active)
        {
            PollVoiceCapture();
        }
        ImGui::InputTextMultiline(
            "##mcp_prompt",
            m_prompt.data(),
            m_prompt.size(),
            ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.3f)
        );

        const bool submit_from_keyboard = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter);
        const bool has_prompt = m_prompt[0] != '\0';
        if (submit_from_keyboard && has_prompt && !is_assistant_busy)
        {
            SubmitPrompt();
        }

        ImGui::Spacing();
        if (!has_prompt || is_assistant_busy)
        {
            ImGui::BeginDisabled();
        }
        if (primary_button("Send", ImVec2(96.0f * scale, 0.0f)))
        {
            SubmitPrompt();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_assistant_busy)
        {
            ImGui::SetTooltip("Spartan AI is still working.");
        }
        if (!has_prompt || is_assistant_busy)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+Enter");
        ImGui::SameLine();
        if (ImGuiSp::button("Clear"))
        {
            m_prompt[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGuiSp::button("Clear chat"))
        {
            m_messages.clear();
            set_response("");
        }
        ImGui::SameLine();
        if (ImGuiSp::button(m_voice_active ? "Stop voice" : "Voice"))
        {
            if (m_voice_active)
            {
                StopVoiceCapture();
            }
            else
            {
                StartVoiceCapture();
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(m_voice_active ? "Listening, click to stop." : "Dictate into the message box with the default microphone.");
        }
#ifdef _WIN32
        if (m_voice_active)
        {
            ImGui::SameLine();
            draw_voice_meter(m_voice_history, m_voice_history_index);
        }
#endif
    }
    end_card();
}

void McpAssistant::StartVoiceCapture()
{
#ifdef _WIN32
    if (m_voice_active)
    {
        return;
    }

    if (voice_thread.joinable())
    {
        voice_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(voice_text_mutex);
        voice_text_pending.clear();
    }
    voice_stop_flag.store(false);
    voice_failed.store(false);
    voice_level.store(0.0f);
    m_voice_history.fill(0.0f);
    voice_thread = std::thread(voice_thread_main);
    m_voice_active = true;
#else
    log_error("Voice input is only available on Windows.");
#endif
}

void McpAssistant::StopVoiceCapture()
{
#ifdef _WIN32
    voice_stop_flag.store(true);
    if (voice_thread.joinable())
    {
        voice_thread.join();
    }
#endif
    m_voice_active = false;
}

void McpAssistant::PollVoiceCapture()
{
#ifdef _WIN32
    if (voice_failed.load())
    {
        StopVoiceCapture();
        return;
    }

    // scroll the level history so the meter animates, decay so bars fall between events
    const float level = voice_level.load();
    m_voice_history[m_voice_history_index] = level;
    m_voice_history_index = (m_voice_history_index + 1) % static_cast<int>(m_voice_history.size());
    voice_level.store(level * 0.92f);

    std::string text;
    {
        std::lock_guard<std::mutex> lock(voice_text_mutex);
        text.swap(voice_text_pending);
    }
    if (text.empty())
    {
        return;
    }

    std::string prompt = m_prompt.data();
    if (!prompt.empty() && prompt.back() != ' ' && prompt.back() != '\n')
    {
        prompt += ' ';
    }
    prompt += text;
    const size_t copy_length = std::min(prompt.size(), m_prompt.size() - 1);
    std::memcpy(m_prompt.data(), prompt.c_str(), copy_length);
    m_prompt[copy_length] = '\0';

    // if the input box is being edited it caches the buffer, release it so the new text shows
    if (ImGui::GetActiveID() == ImGui::GetID("##mcp_prompt"))
    {
        ImGui::ClearActiveID();
    }
#endif
}

void McpAssistant::SubmitPrompt()
{
    if (!spartan::McpServer::IsRunning())
    {
        if (!spartan::McpServer::Start())
        {
            set_response("failed to start engine MCP bridge on 127.0.0.1:" + std::to_string(spartan::McpServer::GetPort()) + ".");
            return;
        }
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
    start_local_run(prompt, "sending request to assistant");
    log_info("Sending prompt to assistant with model " + model_id + ".");

    std::thread([prompt, api_key, model_id]()
    {
        std::string response;
        if (!send_prompt_to_assistant(prompt, api_key, model_id, response))
        {
            if (!should_restart_assistant_after_failure(response))
            {
                log_error(response);
                set_response(response);
                set_busy(false);
                return;
            }

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

void McpAssistant::CancelRun()
{
    log_info("Cancelling active assistant run.");
    std::thread([]()
    {
        std::string response;
        if (!send_cancel_to_assistant(response))
        {
            log_error(response);
            set_response(response.empty() ? "failed to cancel assistant run." : response);
            finish_run_locally(false, true, "cancel request failed");
            return;
        }

        log_info(response);
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
            if (!should_restart_assistant_after_failure(response))
            {
                log_error(response);
                set_response(response);
                set_busy(false);
                return;
            }

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

void McpAssistant::DrawAssistantRun()
{
    const AssistantRunState run = get_run_snapshot();
    if (!run.has_run)
    {
        DrawChatMessage({ false, "working on request" }, static_cast<int>(m_messages.size()));
        return;
    }

    const float scale = ui_scale();
    const ImVec4 state_color =
        run.cancelled ? ImGui::Style::color_warning :
        run.failed ? ImGui::Style::color_error :
        run.active ? ImGui::Style::color_info :
        ImGui::Style::color_ok;

    ImGui::PushID("assistant_run");
    if (begin_card("##assistant_run_card"))
    {
        const bool bold_font = push_bold_font();
        ImGui::TextColored(state_color, "Spartan AI");
        pop_bold_font(bold_font);
        ImGui::SameLine();
        draw_status_pill(run.active ? "working" : (run.failed ? "failed" : (run.cancelled ? "cancelled" : "done")), state_color);

        if (run.active)
        {
            ImGui::SameLine();
            const float cancel_x = std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 88.0f * scale);
            ImGui::SetCursorPosX(cancel_x);
            if (ImGuiSp::button("Cancel", ImVec2(82.0f * scale, 0.0f)))
            {
                CancelRun();
            }
        }

        ImGui::Spacing();
        const float line_height    = ImGui::GetTextLineHeightWithSpacing();
        const float history_height = std::min(static_cast<float>(run.event_lines.size() + 2), 12.0f) * line_height;
        if (ImGui::BeginChild("##assistant_run_history", ImVec2(0.0f, history_height)))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            for (const std::string& line : run.event_lines)
            {
                ImGui::TextWrapped("%s", line.c_str());
            }
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s", run.status.empty() ? "working on request" : run.status.c_str());

            // stick to the latest status unless the user scrolled up to read history
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - line_height)
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();

        int elapsed_ms = run.elapsed_ms;
        if (run.active && run.started_at.time_since_epoch().count() != 0)
        {
            elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - run.started_at).count());
        }
        ImGui::TextDisabled("%0.1fs", static_cast<float>(elapsed_ms) / 1000.0f);

        if (!run.summary.empty() && !run.active)
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", run.summary.c_str());
        }
    }
    end_card();
    ImGui::PopID();
    ImGui::Spacing();
}

void McpAssistant::DrawChatMessage(const ChatMessage& message, int index)
{
    const float scale = ui_scale();
    const float width_available = ImGui::GetContentRegionAvail().x;
    const float bubble_width = std::max(240.0f * scale, width_available * 0.78f);
    const float bubble_width_clamped = std::min(bubble_width, width_available);

    if (message.is_user)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, width_available - bubble_width_clamped));
    }

    const ImVec4 accent = ImGui::Style::color_accent_1;
    const ImVec4 user_bg = with_alpha(accent, 0.20f);
    const ImVec4 assistant_bg = softened(ImGui::Style::bg_color_2, 0.46f, 0.94f);
    const ImVec4 border = message.is_user ? with_alpha(accent, 0.34f) : with_alpha(ImGui::Style::color_ok, 0.18f);
    const ImVec4 role_color = message.is_user ? accent : ImGui::Style::color_ok;

    ImGui::PushID(index);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11.0f * scale, 9.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, message.is_user ? user_bg : assistant_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);

    if (ImGui::BeginChild("##mcp_message", ImVec2(bubble_width_clamped, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        const bool bold_font = push_bold_font();
        ImGui::TextColored(role_color, "%s", message.is_user ? "You" : "Spartan AI");
        pop_bold_font(bold_font);

        std::vector<char> selectable_text(message.text.begin(), message.text.end());
        selectable_text.push_back('\0');
        const float text_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float text_height = ImGui::CalcTextSize(message.text.c_str(), nullptr, false, text_width).y + ImGui::GetStyle().FramePadding.y * 2.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::InputTextMultiline(
            "##mcp_message_text",
            selectable_text.data(),
            selectable_text.size(),
            ImVec2(-FLT_MIN, text_height),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_WordWrap
        );
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor(2);
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
