/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ================
#include "Widget.h"
#include <vector>
#include "../imgui/TextEditor.h"
//===========================

namespace spartan
{
    class RHI_Shader;
}

class ShaderEditor : public Widget
{
public:
    ShaderEditor(Editor* editor);

    void OnTickVisible() override;

private:
    void ShowShaderSource(float width, float height);
    void ShowShaderList(float width, float height);
    void ShowControls();
    void ShowUnsavedChangesDialog();
    void GetShaderInstances();
    void RequestShaderSelection(
        spartan::RHI_Shader* shader,
        const std::string& name
    );
    void SelectShader(
        spartan::RHI_Shader* shader,
        const std::string& name
    );
    void ReloadShader();
    bool SaveAndCompile();

    spartan::RHI_Shader* m_shader = nullptr;
    spartan::RHI_Shader* m_pending_shader = nullptr;
    std::string m_shader_name     = "N/A";
    std::string m_pending_shader_name;
    int32_t m_index_displayed     = -1;
    int32_t m_stage_filter        = 0;
    bool m_first_run              = true;
    bool m_source_dirty           = false;
    bool m_open_unsaved_dialog    = false;
    float m_shader_list_width     = 290.0f;
    ImGuiTextFilter m_shader_filter;
    TextEditor m_text_editor;
    std::vector<spartan::RHI_Shader*> m_shaders;
};
