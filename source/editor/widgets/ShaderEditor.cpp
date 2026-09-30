/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "ShaderEditor.h"
#include <fstream>
#include "rhi/RHI_Shader.h"
#include "resource/ResourceCache.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Properties.h"
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

    // the stage pills and the list groups share this order, 0 is every stage
    const int32_t stage_group_count = 6;

    int32_t stage_group(const RHI_Shader_Type stage)
    {
        switch (stage)
        {
        case RHI_Shader_Type::Vertex:        return 1;
        case RHI_Shader_Type::Pixel:         return 2;
        case RHI_Shader_Type::Compute:       return 3;
        case RHI_Shader_Type::RayGeneration:
        case RHI_Shader_Type::RayMiss:
        case RHI_Shader_Type::RayHit:        return 4;
        default:                             return 5;
        }
    }

    const char* stage_group_name(const int32_t group)
    {
        static const char* names[stage_group_count] = { "All", "Vertex", "Pixel", "Compute", "Ray tracing", "Hull and domain" };
        return names[clamp(group, 0, stage_group_count - 1)];
    }

    ImVec4 stage_group_tint(const int32_t group)
    {
        switch (group)
        {
        case 1:  return ImVec4(0.25f, 0.70f, 1.00f, 1.0f);
        case 2:  return ImVec4(1.00f, 0.52f, 0.42f, 1.0f);
        case 3:  return ImVec4(0.66f, 0.52f, 1.00f, 1.0f);
        case 4:  return ImVec4(0.30f, 0.85f, 0.85f, 1.0f);
        case 5:  return ImVec4(1.00f, 0.42f, 0.66f, 1.0f);
        default: return ImGui::Style::color_accent_1;
        }
    }

    bool stage_filter_matches(const RHI_Shader_Type stage, const int32_t filter)
    {
        return filter == 0 || stage_group(stage) == filter;
    }

    string shader_defines(RHI_Shader* shader)
    {
        string defines;
        for (const auto& define : shader->GetDefines())
        {
            if (define.second != "0")
            {
                defines += (defines.empty() ? "" : " ") + define.first;
            }
        }
        return defines;
    }

    string shader_display_name(RHI_Shader* shader)
    {
        const string defines = shader_defines(shader);
        return defines.empty() ? shader->GetObjectName() : shader->GetObjectName() + " " + defines;
    }

    // everything lives under data/shaders, the part before it is the same for every shader
    string readable_path(const string& path)
    {
        string result = path;
        replace(result.begin(), result.end(), '\\', '/');
        const size_t at = result.find("data/shaders/");
        return at == string::npos ? result : result.substr(at);
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
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

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
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
    const bool open = ImGui::BeginChild("##shader_editor_source", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (open)
    {
        if (m_shader)
        {
            const RHI_ShaderCompilationState state = m_shader->GetCompilationState();
            const int32_t group                    = stage_group(m_shader->GetShaderStage());

            // name, then where it lives, then whether it compiled, flush right so the eye finds it in the same place
            if (Editor::font_bold)
            {
                ImGui::PushFont(Editor::font_bold, 0.0f);
            }
            ImGui::TextUnformatted(m_shader->GetObjectName().c_str());
            if (Editor::font_bold)
            {
                ImGui::PopFont();
            }
            const string defines = shader_defines(m_shader);
            if (!defines.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImGui::Style::color_accent_1, "%s", defines.c_str());
            }
            ImGui::SameLine(0, ImGui::EditorUi::scaled(10.0f));
            ImGui::TextColored(stage_group_tint(group), "%s", shader_stage_name(m_shader->GetShaderStage()));
            ImGui::SameLine(0, ImGui::EditorUi::scaled(10.0f));
            ImGui::TextColored(ImGui::Style::color_text_faint, "%s", readable_path(m_shader->GetFilePath()).c_str());
            ImGuiSp::tooltip(m_shader->GetFilePath().c_str());

            const char* state_text = compilation_state_name(state);
            const float dot_space  = ImGui::EditorUi::scaled(14.0f);
            editor_ui::toolbar::align_right(ImGui::CalcTextSize(state_text).x + dot_space);
            const ImVec2 state_pos = ImGui::GetCursorScreenPos();
            ImGui::EditorUi::status_dot(ImGui::GetWindowDrawList(), ImVec2(state_pos.x + ImGui::EditorUi::scaled(4.0f), state_pos.y + ImGui::GetTextLineHeight() * 0.5f), ImGui::EditorUi::scaled(3.5f), compilation_state_color(state), state == RHI_ShaderCompilationState::Compiling);
            ImGui::SetCursorScreenPos(ImVec2(state_pos.x + dot_space, state_pos.y));
            ImGui::TextColored(compilation_state_color(state), "%s", state_text);
            ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

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
                            ImGui::GetTextLineHeightWithSpacing() + ImGui::EditorUi::scaled(4.0f);
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

                        // status bar, where the cursor is on the left and what is pending on the right
                        const TextEditor::Coordinates cursor = m_text_editor.GetCursorPosition();
                        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
                        ImGui::TextColored(ImGui::Style::color_text_muted, "Ln %d, Col %d", cursor.mLine + 1, cursor.mColumn + 1);
                        ImGui::SameLine(0, ImGui::EditorUi::scaled(16.0f));
                        ImGui::TextColored(ImGui::Style::color_text_faint, "%d lines   HLSL   %u of %u files", m_text_editor.GetTotalLines(), i + 1, source_count);
                        if (m_source_dirty)
                        {
                            const char* pending = "Unsaved changes, Ctrl+S saves and compiles";
                            editor_ui::toolbar::align_right(ImGui::CalcTextSize(pending).x + ImGui::EditorUi::scaled(14.0f));
                            editor_ui::layout::note(pending, ImGui::Style::color_warning);
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            editor_ui::empty_state("No shader selected", "Choose a shader from the library to view and edit its source");
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowShaderList(const float width, const float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
    const bool open = ImGui::BeginChild("##shader_editor_list", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    if (open)
    {
        auto passes = [this](RHI_Shader* shader)
        {
            if (m_failed_only && shader->GetCompilationState() != RHI_ShaderCompilationState::Failed)
            {
                return false;
            }
            return stage_filter_matches(shader->GetShaderStage(), m_stage_filter) && m_shader_filter.PassFilter(shader_filter_name(shader).c_str());
        };

        uint32_t filtered_count = 0;
        for (RHI_Shader* shader : m_shaders)
        {
            filtered_count += passes(shader) ? 1 : 0;
        }

        char count[32];
        snprintf(count, sizeof(count), "%u of %u", filtered_count, static_cast<uint32_t>(m_shaders.size()));
        editor_ui::toolbar::search("##shader_filter", "Search name, define or path", m_shader_filter, ImGui::GetContentRegionAvail().x, count, filtered_count == 0);
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

        if (ImGui::BeginChild("##shader_rows", ImVec2(0.0f, 0.0f)))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float row_height = ImGui::GetFrameHeight();
            const float pad        = ImGui::EditorUi::scaled(8.0f);
            const float dot_radius = ImGui::EditorUi::scaled(3.5f);
            int32_t last_group     = -1;

            for (RHI_Shader* shader : m_shaders)
            {
                if (!passes(shader))
                {
                    continue;
                }

                // the list is sorted by stage, a rule opens every stage so a long list reads as chapters
                const int32_t group = stage_group(shader->GetShaderStage());
                if (group != last_group)
                {
                    if (last_group != -1)
                    {
                        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(4.0f)));
                    }
                    ImGui::EditorUi::section_rule(stage_group_name(group));
                    last_group = group;
                }

                ImGui::PushID(shader);
                const RHI_ShaderCompilationState state = shader->GetCompilationState();
                const ImVec2 row_position = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##shader", m_shader == shader, ImGuiSelectableFlags_None, ImVec2(0.0f, row_height)))
                {
                    RequestShaderSelection(shader, shader_display_name(shader));
                }

                const float center_y = row_position.y + row_height * 0.5f;
                ImGui::EditorUi::status_dot(draw_list, ImVec2(row_position.x + pad * 0.5f + dot_radius, center_y), dot_radius, compilation_state_color(state), state == RHI_ShaderCompilationState::Compiling);

                // the name reads first, the defines that make this variant different trail it in the accent color
                const float text_y      = IM_ROUND(center_y - ImGui::GetTextLineHeight() * 0.5f);
                const float text_x      = row_position.x + pad + dot_radius * 2.0f + ImGui::EditorUi::scaled(4.0f);
                const float right       = row_position.x + ImGui::GetContentRegionAvail().x;
                const string& name      = shader->GetObjectName();
                const string defines    = shader_defines(shader);
                const float name_w      = ImGui::CalcTextSize(name.c_str()).x;
                draw_list->PushClipRect(ImVec2(text_x, row_position.y), ImVec2(right, row_position.y + row_height), true);
                draw_list->AddText(ImVec2(text_x, text_y), ImGui::EditorUi::color(ImGui::Style::color_text), name.c_str());
                if (!defines.empty())
                {
                    draw_list->AddText(ImVec2(text_x + name_w + ImGui::EditorUi::scaled(6.0f), text_y), ImGui::EditorUi::color(ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_accent_1, 0.45f)), defines.c_str());
                }
                draw_list->PopClipRect();

                const string tooltip = string(shader_stage_name(shader->GetShaderStage())) + " shader, " + compilation_state_name(state) + "\n" + shader_display_name(shader) + "\n" + readable_path(shader->GetFilePath());
                ImGuiSp::tooltip(tooltip.c_str());
                ImGui::PopID();
            }

            if (filtered_count == 0)
            {
                const bool filtered = !m_shaders.empty();
                if (editor_ui::empty_state(filtered ? "No shaders match" : "No shaders loaded", filtered ? "Nothing passes the search, stage and failure filters together." : "Shaders appear here once the renderer has compiled them.", filtered ? "Clear filters" : nullptr))
                {
                    m_shader_filter.Clear();
                    m_stage_filter = 0;
                    m_failed_only  = false;
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowControls()
{
    const bool has_source = m_shader && m_index_displayed != -1;
    const bool compiling  = m_shader && m_shader->GetCompilationState() == RHI_ShaderCompilationState::Compiling;
    const float gap       = ImGui::EditorUi::scaled(6.0f);

    // compiling is the one thing this window exists for, so it is the only filled button, amber while edits are unsaved
    ImGui::BeginDisabled(!has_source || compiling);
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_Tooltip);
    const bool compile = m_source_dirty ? editor_ui::attention_button("Save and compile", true) : editor_ui::primary_button(compiling ? "Compiling" : "Compile");
    ImGui::EndDisabled();
    ImGuiSp::tooltip(m_source_dirty ? "Write the edited sources to disk and compile, Ctrl+S" : "Compile the shader from its sources, Ctrl+S");
    if (compile)
    {
        SaveAndCompile();
    }

    ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!has_source || m_source_dirty);
    if (editor_ui::toolbar::ghost_button("Reload", m_source_dirty ? "Save or discard changes before reloading" : "Reload source files from disk"))
    {
        ReloadShader();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!has_source);
    if (editor_ui::toolbar::ghost_button("Open externally", "Open the active source file in the default editor"))
    {
        const vector<string>& paths = m_shader->GetFilePaths();
        if (m_index_displayed >= 0 && m_index_displayed < static_cast<int32_t>(paths.size()))
        {
            FileSystem::OpenUrl(paths[m_index_displayed]);
        }
    }
    ImGui::EndDisabled();

    // stage pills with their counts replace the combo, every stage is one click away and the counts describe the library
    uint32_t counts[stage_group_count] = {};
    uint32_t failed                    = 0;
    for (RHI_Shader* shader : m_shaders)
    {
        counts[0]++;
        counts[stage_group(shader->GetShaderStage())]++;
        failed += shader->GetCompilationState() == RHI_ShaderCompilationState::Failed ? 1 : 0;
    }
    if (failed == 0)
    {
        m_failed_only = false;
    }

    editor_ui::toolbar::divider();
    bool first = true;
    for (int32_t group = 0; group < stage_group_count; group++)
    {
        if (group > 0 && counts[group] == 0)
        {
            continue;
        }
        if (!first)
        {
            ImGui::SameLine(0, gap);
        }
        first = false;

        char label[64];
        snprintf(label, sizeof(label), "%s %u###stage_%d", stage_group_name(group), counts[group], group);
        if (editor_ui::toolbar::pill(label, m_stage_filter == group, stage_group_tint(group), group == 0 ? "Show every stage" : "Show only this stage"))
        {
            m_stage_filter = group;
        }
    }

    // library health on the right, a failure is a filter you can click, a healthy library is a quiet line
    if (failed > 0)
    {
        char label[48];
        snprintf(label, sizeof(label), "%u failed###failed", failed);
        editor_ui::toolbar::align_right(editor_ui::toolbar::pill_width(label, true));
        if (editor_ui::toolbar::pill(label, m_failed_only, ImGui::Style::color_error, m_failed_only ? "Show every shader again" : "Show only shaders that failed to compile", true, true))
        {
            m_failed_only = !m_failed_only;
        }
    }
    else if (!m_shaders.empty())
    {
        char label[48];
        snprintf(label, sizeof(label), "All %u compiled", counts[0]);
        const float dot_space = ImGui::EditorUi::scaled(14.0f);
        editor_ui::toolbar::align_right(ImGui::CalcTextSize(label).x + dot_space);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float center_y = pos.y + ImGui::GetFrameHeight() * 0.5f;
        ImGui::EditorUi::status_dot(ImGui::GetWindowDrawList(), ImVec2(pos.x + ImGui::EditorUi::scaled(4.0f), center_y), ImGui::EditorUi::scaled(3.5f), ImGui::Style::color_ok);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + dot_space, pos.y));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", label);
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
        async,
        m_shader->GetVertexType()
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
            if (stage_group(a->GetShaderStage()) != stage_group(b->GetShaderStage()))
            {
                return stage_group(a->GetShaderStage()) < stage_group(b->GetShaderStage());
            }

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
