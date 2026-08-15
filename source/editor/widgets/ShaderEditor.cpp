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

//= INCLUDES ========================
#include "pch.h"
#include "ShaderEditor.h"
#include <fstream>
#include "rhi/RHI_Shader.h"
#include "resource/ResourceCache.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    const char* shader_stage_name(const RHI_Shader_Type stage)
    {
        switch (stage)
        {
        case RHI_Shader_Type::Vertex:        return "Vertex";
        case RHI_Shader_Type::Pixel:         return "Pixel";
        case RHI_Shader_Type::Compute:       return "Compute";
        case RHI_Shader_Type::Domain:        return "Domain";
        case RHI_Shader_Type::Hull:          return "Hull";
        case RHI_Shader_Type::RayGeneration: return "Ray generation";
        case RHI_Shader_Type::RayMiss:       return "Ray miss";
        case RHI_Shader_Type::RayHit:        return "Ray hit";
        default:                             return "Unknown";
        }
    }

    const char* compilation_state_name(const RHI_ShaderCompilationState state)
    {
        switch (state)
        {
        case RHI_ShaderCompilationState::Idle:      return "Not compiled";
        case RHI_ShaderCompilationState::Compiling: return "Compiling";
        case RHI_ShaderCompilationState::Succeeded: return "Compiled";
        case RHI_ShaderCompilationState::Failed:    return "Failed";
        default:                                    return "Unknown";
        }
    }

    ImVec4 compilation_state_color(const RHI_ShaderCompilationState state)
    {
        switch (state)
        {
        case RHI_ShaderCompilationState::Compiling:
            return ImGui::Style::color_warning;
        case RHI_ShaderCompilationState::Succeeded:
            return ImGui::Style::color_ok;
        case RHI_ShaderCompilationState::Failed:
            return ImGui::Style::color_error;
        default:
            return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
        }
    }

    bool stage_filter_matches(const RHI_Shader_Type stage, const int32_t filter)
    {
        if (filter == 0)
        {
            return true;
        }

        if (filter == 1)
        {
            return stage == RHI_Shader_Type::Vertex;
        }

        if (filter == 2)
        {
            return stage == RHI_Shader_Type::Pixel;
        }

        if (filter == 3)
        {
            return stage == RHI_Shader_Type::Compute;
        }

        if (filter == 4)
        {
            return
                stage == RHI_Shader_Type::RayGeneration ||
                stage == RHI_Shader_Type::RayMiss       ||
                stage == RHI_Shader_Type::RayHit;
        }

        return
            stage == RHI_Shader_Type::Hull   ||
            stage == RHI_Shader_Type::Domain;
    }

    string shader_display_name(RHI_Shader* shader)
    {
        string name = shader->GetObjectName();
        for (const auto& define : shader->GetDefines())
        {
            if (define.second != "0")
            {
                name += " " + define.first;
            }
        }
        return name;
    }

    string shader_filter_name(RHI_Shader* shader)
    {
        return
            shader_display_name(shader) + " " +
            shader_stage_name(shader->GetShaderStage()) + " " +
            shader->GetFilePath();
    }
}

ShaderEditor::ShaderEditor(Editor* editor) : Widget(editor)
{
    m_title           = "Shader Editor";
    m_flags           = ImGuiWindowFlags_NoScrollbar;
    m_visible         = false;
    m_toolbar_order   = 3;
    m_toolbar_icon    = static_cast<int>(spartan::IconType::Shader);
    m_alpha           = 1.0f;
    m_index_displayed = -1;

    m_text_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
    m_text_editor.SetReadOnly(false);
}

void ShaderEditor::OnTickVisible()
{
    GetShaderInstances();
    if (m_first_run && !m_shaders.empty())
    {
        SelectShader(m_shaders.front(), shader_display_name(m_shaders.front()));
        m_first_run = false;
    }

    ShowControls();
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float dpi = Window::GetDpiScale();
    const float maximum_list_width = max(
        1.0f,
        min(420.0f * dpi, available.x * 0.45f)
    );
    const float minimum_list_width = min(
        220.0f * dpi,
        maximum_list_width
    );
    m_shader_list_width = clamp(
        m_shader_list_width,
        minimum_list_width,
        maximum_list_width
    );

    ShowShaderList(m_shader_list_width, available.y);
    ImGui::SameLine();

    const float splitter_width = 4.0f * dpi;
    ImGui::InvisibleButton(
        "##shader_editor_splitter",
        ImVec2(splitter_width, available.y)
    );
    if (ImGui::IsItemActive())
    {
        m_shader_list_width += ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::SameLine();
    const float source_width = max(
        1.0f,
        available.x -
        m_shader_list_width -
        splitter_width -
        ImGui::GetStyle().ItemSpacing.x * 2.0f
    );
    ShowShaderSource(source_width, available.y);
    ShowUnsavedChangesDialog();
}

void ShaderEditor::ShowShaderSource(const float width, const float height)
{
    if (ImGui::BeginChild(
        "##shader_editor_source",
        ImVec2(width, height),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar
    ))
    {
        if (m_shader)
        {
            const RHI_ShaderCompilationState state =
                m_shader->GetCompilationState();

            if (Editor::font_bold)
            {
                ImGui::PushFont(Editor::font_bold, 0.0f);
            }
            ImGui::TextUnformatted(m_shader_name.c_str());
            if (Editor::font_bold)
            {
                ImGui::PopFont();
            }

            ImGui::SameLine();
            ImGui::TextColored(
                compilation_state_color(state),
                "%s",
                compilation_state_name(state)
            );

            ImGui::TextDisabled(
                "%s",
                m_shader->GetFilePath().c_str()
            );
            ImGuiSp::tooltip(m_shader->GetFilePath().c_str());
            ImGui::Separator();

            if (ImGui::BeginTabBar(
                "##shader_editor_tab_bar",
                ImGuiTabBarFlags_FittingPolicyScroll
            ))
            {
                const std::vector<std::string>& names   = m_shader->GetNames();
                const std::vector<std::string>& sources = m_shader->GetSources();

                const uint32_t source_count = min(
                    static_cast<uint32_t>(names.size()),
                    static_cast<uint32_t>(sources.size())
                );
                for (uint32_t i = 0; i < source_count; i++)
                {
                    const string tab_label =
                        names[i] +
                        "###shader_source_tab_" +
                        to_string(i);
                    if (ImGui::BeginTabItem(tab_label.c_str()))
                    {
                        if (m_index_displayed != static_cast<int32_t>(i))
                        {
                            m_text_editor.SetText(sources[i]);
                            m_index_displayed =
                                static_cast<int32_t>(i);
                        }

                        const float status_height =
                            ImGui::GetTextLineHeightWithSpacing();
                        const float editor_height = max(
                            1.0f,
                            ImGui::GetContentRegionAvail().y -
                            status_height
                        );
                        m_text_editor.Render(
                            "##shader_source",
                            ImVec2(-FLT_MIN, editor_height),
                            false
                        );
                        if (m_text_editor.IsTextChanged())
                        {
                            m_shader->SetSource(
                                i,
                                m_text_editor.GetText()
                            );
                            m_source_dirty = true;
                        }

                        const TextEditor::Coordinates cursor =
                            m_text_editor.GetCursorPosition();
                        ImGui::TextDisabled(
                            "Ln %d, Col %d   %d lines   HLSL",
                            cursor.mLine + 1,
                            cursor.mColumn + 1,
                            m_text_editor.GetTotalLines()
                        );
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            const char* title = "No shader selected";
            const char* message =
                "Choose a shader from the library to view and edit its source";
            const ImVec2 title_size = ImGui::CalcTextSize(title);
            const ImVec2 message_size = ImGui::CalcTextSize(message);

            ImGui::SetCursorPos(ImVec2(
                max(
                    ImGui::GetCursorPosX(),
                    (width - title_size.x) * 0.5f
                ),
                max(ImGui::GetCursorPosY(), height * 0.4f)
            ));
            ImGui::TextUnformatted(title);
            ImGui::SetCursorPosX(max(
                ImGui::GetCursorPosX(),
                (width - message_size.x) * 0.5f
            ));
            ImGui::TextDisabled("%s", message);
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowShaderList(const float width, const float height)
{
    if (ImGui::BeginChild(
        "##shader_editor_list",
        ImVec2(width, height),
        ImGuiChildFlags_Borders
    ))
    {
        uint32_t filtered_count = 0;
        for (RHI_Shader* shader : m_shaders)
        {
            const string filter_name = shader_filter_name(shader);
            if (
                m_shader_filter.PassFilter(filter_name.c_str()) &&
                stage_filter_matches(
                    shader->GetShaderStage(),
                    m_stage_filter
                )
            )
            {
                filtered_count++;
            }
        }

        const string count_text =
            to_string(filtered_count) + " of " +
            to_string(m_shaders.size());

        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, 0.0f);
        }
        ImGui::TextUnformatted("Shader library");
        if (Editor::font_bold)
        {
            ImGui::PopFont();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", count_text.c_str());

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetNextItemShortcut(
            ImGuiMod_Ctrl | ImGuiKey_F,
            ImGuiInputFlags_Tooltip
        );
        if (ImGui::InputTextWithHint(
            "##shader_filter",
            "Search name, stage, define or path",
            m_shader_filter.InputBuf,
            IM_ARRAYSIZE(m_shader_filter.InputBuf),
            ImGuiInputTextFlags_EscapeClearsAll
        ))
        {
            m_shader_filter.Build();
        }

        const char* stage_filters[] =
        {
            "All stages",
            "Vertex",
            "Pixel",
            "Compute",
            "Ray tracing",
            "Hull and domain"
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo(
            "##shader_stage_filter",
            &m_stage_filter,
            stage_filters,
            IM_ARRAYSIZE(stage_filters)
        );
        ImGui::Separator();

        uint32_t visible_count = 0;
        for (RHI_Shader* shader : m_shaders)
        {
            const string name = shader_display_name(shader);
            const string filter_name = shader_filter_name(shader);
            if (
                !m_shader_filter.PassFilter(filter_name.c_str()) ||
                !stage_filter_matches(
                    shader->GetShaderStage(),
                    m_stage_filter
                )
            )
            {
                continue;
            }

            visible_count++;
            ImGui::PushID(shader);
            const RHI_ShaderCompilationState state = shader->GetCompilationState();

            const float row_height =
                ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                ImGui::GetStyle().FramePadding.y;
            const ImVec2 row_position = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(
                "##shader",
                m_shader == shader,
                ImGuiSelectableFlags_None,
                ImVec2(0.0f, row_height)
            ))
            {
                RequestShaderSelection(shader, name);
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float status_width = 3.0f * Window::GetDpiScale();
            draw_list->AddRectFilled(
                row_position,
                ImVec2(
                    row_position.x + status_width,
                    row_position.y + row_height
                ),
                ImGui::ColorConvertFloat4ToU32(
                    compilation_state_color(state)
                )
            );

            const ImVec2 text_position = ImVec2(
                row_position.x +
                status_width +
                ImGui::GetStyle().FramePadding.x,
                row_position.y +
                ImGui::GetStyle().FramePadding.y * 0.5f
            );
            draw_list->AddText(
                text_position,
                ImGui::GetColorU32(ImGuiCol_Text),
                name.c_str()
            );

            const string metadata =
                string(shader_stage_name(shader->GetShaderStage())) +
                "  " +
                compilation_state_name(state);
            draw_list->AddText(
                ImVec2(
                    text_position.x,
                    text_position.y +
                    ImGui::GetTextLineHeightWithSpacing()
                ),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                metadata.c_str()
            );

            const string tooltip =
                metadata + "\n" + shader->GetFilePath();
            ImGuiSp::tooltip(tooltip.c_str());
            ImGui::PopID();
        }

        if (visible_count == 0)
        {
            const char* message = m_shaders.empty() ?
                "No shaders available" :
                "No shaders match the current filters";
            const ImVec2 message_size = ImGui::CalcTextSize(message);
            ImGui::SetCursorPosX(max(
                ImGui::GetCursorPosX(),
                (width - message_size.x) * 0.5f
            ));
            ImGui::Dummy(ImVec2(
                0.0f,
                24.0f * Window::GetDpiScale()
            ));
            ImGui::TextDisabled("%s", message);
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowControls()
{
    const bool has_source =
        m_shader &&
        m_index_displayed != -1;
    const bool compiling =
        m_shader &&
        m_shader->GetCompilationState() ==
        RHI_ShaderCompilationState::Compiling;

    ImGui::BeginDisabled(!has_source || compiling);
    ImGui::SetNextItemShortcut(
        ImGuiMod_Ctrl | ImGuiKey_S,
        ImGuiInputFlags_Tooltip
    );
    const char* compile_label = m_source_dirty ?
        "Save and compile *" :
        "Compile";
    if (ImGuiSp::button(compile_label))
    {
        SaveAndCompile();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("Save and compile, Ctrl+S");

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_source || m_source_dirty);
    if (ImGuiSp::button("Reload"))
    {
        ReloadShader();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip(
        m_source_dirty ?
        "Save or discard changes before reloading" :
        "Reload source files from disk"
    );

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_source);
    if (ImGuiSp::button("Open externally"))
    {
        const vector<string>& paths = m_shader->GetFilePaths();
        if (
            m_index_displayed >= 0 &&
            m_index_displayed < static_cast<int32_t>(paths.size())
        )
        {
            FileSystem::OpenUrl(paths[m_index_displayed]);
        }
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("Open the active source file");

    if (m_source_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(
            ImGui::Style::color_warning,
            "Unsaved changes"
        );
    }
}

void ShaderEditor::ShowUnsavedChangesDialog()
{
    if (m_open_unsaved_dialog)
    {
        ImGui::OpenPopup("Unsaved shader changes");
        m_open_unsaved_dialog = false;
    }

    if (ImGui::BeginPopupModal(
        "Unsaved shader changes",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
    ))
    {
        ImGui::TextUnformatted(
            "Save the current shader before switching?"
        );
        ImGui::TextDisabled(
            "Unsaved source changes will otherwise be discarded."
        );
        ImGui::Separator();

        if (ImGuiSp::button("Save and compile"))
        {
            if (SaveAndCompile())
            {
                SelectShader(
                    m_pending_shader,
                    m_pending_shader_name
                );
                m_pending_shader = nullptr;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGuiSp::button("Discard"))
        {
            SelectShader(
                m_pending_shader,
                m_pending_shader_name
            );
            m_pending_shader = nullptr;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGuiSp::button("Cancel"))
        {
            m_pending_shader = nullptr;
            m_pending_shader_name.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ShaderEditor::RequestShaderSelection(
    RHI_Shader* shader,
    const string& name
)
{
    if (shader == m_shader)
    {
        return;
    }

    if (m_source_dirty)
    {
        m_pending_shader = shader;
        m_pending_shader_name = name;
        m_open_unsaved_dialog = true;
        return;
    }

    SelectShader(shader, name);
}

void ShaderEditor::SelectShader(RHI_Shader* shader, const string& name)
{
    if (!shader || m_shader == shader)
    {
        return;
    }

    m_shader          = shader;
    m_shader_name     = name;
    m_index_displayed = -1;
    m_source_dirty    = false;
    m_shader->LoadFromDrive(m_shader->GetFilePath());
}

void ShaderEditor::ReloadShader()
{
    if (!m_shader || m_source_dirty)
    {
        return;
    }

    m_shader->LoadFromDrive(m_shader->GetFilePath());
    m_index_displayed = -1;
}

bool ShaderEditor::SaveAndCompile()
{
    if (!m_shader || m_index_displayed == -1)
    {
        return false;
    }

    const vector<string>& file_paths = m_shader->GetFilePaths();
    const vector<string>& sources = m_shader->GetSources();
    const uint32_t source_count = min(
        static_cast<uint32_t>(file_paths.size()),
        static_cast<uint32_t>(sources.size())
    );
    bool all_sources_saved = source_count != 0;
    for (uint32_t i = 0; i < source_count; i++)
    {
        ofstream out(file_paths[i], ios::binary | ios::trunc);
        if (!out)
        {
            SP_LOG_ERROR("Failed to open shader source for writing: %s", file_paths[i].c_str());
            all_sources_saved = false;
            continue;
        }
        out.write(sources[i].data(), static_cast<streamsize>(sources[i].size()));
        if (!out)
        {
            SP_LOG_ERROR("Failed to write shader source: %s", file_paths[i].c_str());
            all_sources_saved = false;
        }
    }

    if (!all_sources_saved)
    {
        return false;
    }

    const bool async = false;
    m_shader->Compile(
        m_shader->GetShaderStage(),
        m_shader->GetFilePath(),
        async
    );
    m_index_displayed = -1;
    m_source_dirty = false;
    return true;
}

void ShaderEditor::GetShaderInstances()
{
    auto shaders = Renderer::GetShaders();
    m_shaders.clear();
    for (const shared_ptr<RHI_Shader>& shader : shaders)
    {
        if (shader)
        {
            m_shaders.emplace_back(shader.get());
        }
    }

    sort(
        m_shaders.begin(),
        m_shaders.end(),
        [](RHI_Shader* a, RHI_Shader* b)
        {
            if (a->GetShaderStage() != b->GetShaderStage())
            {
                return
                    a->GetShaderStage() <
                    b->GetShaderStage();
            }

            return
                a->GetObjectName() <
                b->GetObjectName();
        }
    );

    if (
        m_shader &&
        find(
            m_shaders.begin(),
            m_shaders.end(),
            m_shader
        ) == m_shaders.end()
    )
    {
        m_shader = nullptr;
        m_shader_name = "N/A";
        m_index_displayed = -1;
        m_source_dirty = false;
    }

    if (
        m_pending_shader &&
        find(
            m_shaders.begin(),
            m_shaders.end(),
            m_pending_shader
        ) == m_shaders.end()
    )
    {
        m_pending_shader = nullptr;
        m_pending_shader_name.clear();
        m_open_unsaved_dialog = false;
    }
}
