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

//= INCLUDES ==========
#include "Widget.h"
#include <array>
#include <string>
#include <vector>
//=====================

class McpAssistant : public Widget
{
public:
    McpAssistant(Editor* editor);
    ~McpAssistant() override;

    void OnTick() override;
    void OnTickVisible() override;
    void OnInvisible() override;

private:
    struct ChatMessage
    {
        bool is_user = false;
        std::string text;
    };

    void SubmitPrompt();
    void StartVoiceCapture();
    void StopVoiceCapture();
    void PollVoiceCapture();
    void CancelRun();
    void RefreshModels();
    void RestartAssistant();
    bool LoadApiKeyFromFile();
    void DrainAssistantResults();
    void ApplyModelList(const std::string& model_list);
    void DrawChatMessage(const ChatMessage& message, int index);
    void DrawAssistantRun();
    void UpdateInputOwnership();
    std::string GetSelectedModelId() const;

    std::array<char, 512> m_cursor_api_key = {};
    std::array<char, 4096> m_prompt = {};
    std::string m_api_key_file_status;
    std::vector<ChatMessage> m_messages;
    std::vector<std::string> m_model_ids = { "auto" };
    std::vector<std::string> m_model_labels = { "Auto" };
    int m_model_index = 0;
    bool m_blocks_input = false;
    bool m_voice_active = false;
    int m_voice_history_index = 0;
    std::array<float, 64> m_voice_history = {};
    bool m_scroll_to_bottom = false;
    bool m_api_key_file_checked = false;
    bool m_refresh_models_after_key_load = false;
    bool m_mcp_auto_start_attempted = false;
};
