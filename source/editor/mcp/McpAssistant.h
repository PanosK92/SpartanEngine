/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include "../widgets/Widget.h"
#include <array>
#include <memory>
#include <string>
#include <vector>
//=====================

class FileDialog;

class McpAssistant : public Widget
{
public:
    McpAssistant(Editor* editor);
    ~McpAssistant() override;

    void OnTick() override;
    void OnVisible() override;
    void OnTickVisible() override;
    void OnInvisible() override;

private:
    struct ChatMessage
    {
        bool is_user = false;
        std::string text;
        std::vector<std::string> images;
    };

    void SubmitPrompt();
    void AttachReferenceImage(const std::string& path);
    void TickImageBrowser();
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
    std::vector<std::string> m_reference_images;
    std::unique_ptr<FileDialog> m_image_dialog;
    bool m_image_dialog_visible = false;
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
    bool m_show_settings = false;
    // expands a terse request into a design brief before the build starts
    bool m_enrich_prompt = true;
};
