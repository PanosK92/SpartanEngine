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

//= INCLUDES =====================
#include "pch.h"
#include "AssetBrowser.h"
#include "Properties.h"
#include "Geometry/Mesh.h"
#include "../Widgets/FileDialog.h"
#include "Viewport.h"
//================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace
{
    bool show_file_dialog_view         = true;
    bool show_file_dialog_load         = false;
    bool mesh_import_dialog_is_visible = false;
    uint32_t mesh_import_dialog_flags  = 0;
    string mesh_import_file_path;
    unique_ptr<FileDialog> file_dialog_view;
    unique_ptr<FileDialog> file_dialog_load;

    static void mesh_import_dialog_checkbox(const MeshFlags option, const char* label, const char* tooltip = nullptr)
    {
        bool enabled = mesh_import_dialog_flags & static_cast<uint32_t>(option);
    
        if (ImGui::Checkbox(label, &enabled))
        {
            if (enabled)
            {
                mesh_import_dialog_flags |= static_cast<uint32_t>(option);
            }
            else
            {
                mesh_import_dialog_flags &= ~static_cast<uint32_t>(option);
            }
        }
    
        if (tooltip != nullptr)
        {
            ImGuiSp::tooltip(tooltip);
        }
    }
    
    static void mesh_import_dialog(Editor* editor)
    {
        if (!mesh_import_dialog_is_visible)
        {
            return;
        }

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(440.0f, 0.0f), ImVec2(440.0f, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

        if (ImGui::Begin("Import mesh", &mesh_import_dialog_is_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            const string file_name = FileSystem::GetFileNameFromFilePath(mesh_import_file_path);
            ImGui::TextUnformatted("Import settings");
            ImGui::TextDisabled("%s", file_name.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            mesh_import_dialog_checkbox(MeshFlags::ImportRemoveRedundantData, "Clean redundant data", "Join identical vertices and remove invalid or duplicate data");
            mesh_import_dialog_checkbox(MeshFlags::PostProcessNormalizeScale, "Normalize scale", "Fit the imported mesh within one cubic unit");
            mesh_import_dialog_checkbox(MeshFlags::ImportCombineMeshes, "Combine compatible meshes", "Reduce hierarchy complexity by joining compatible meshes");
            mesh_import_dialog_checkbox(MeshFlags::ImportLights, "Import embedded lights", "Create lights defined by the source file");
            mesh_import_dialog_checkbox(MeshFlags::PostProcessOptimize, "Optimize geometry", "Improve vertex cache use and reduce overdraw");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float button_width = 100.0f;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - button_width * 2.0f - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f)))
            {
                mesh_import_dialog_is_visible = false;
            }

            ImGui::SameLine();
            const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.40f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.58f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.72f));
            if (ImGui::Button("Import", ImVec2(button_width, 0.0f)))
            {
                spartan::ThreadPool::AddTask([]()
                {
                    spartan::ResourceCache::Load<spartan::Mesh>(mesh_import_file_path, mesh_import_dialog_flags);
                });
                mesh_import_dialog_is_visible = false;
            }
            ImGui::PopStyleColor(3);

            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                mesh_import_dialog_is_visible = false;
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

AssetBrowser::AssetBrowser(Editor* editor) : Widget(editor)
{
    m_title           = "Assets";
    file_dialog_view  = make_unique<FileDialog>(false, FileDialog_Type_Browser,       FileDialog_Op_Load, FileDialog_Filter_All);
    file_dialog_load  = make_unique<FileDialog>(true,  FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_Model);
    m_flags          |= ImGuiWindowFlags_NoScrollbar;

    // just clicked, not selected (double clicked, end of dialog)
    file_dialog_view->SetCallbackOnItemClicked([this](const string& str) { OnPathClicked(str); });
    file_dialog_view->SetToolbarAction("Import Model", []() { show_file_dialog_load = true; });
}

void AssetBrowser::OnTickVisible()
{
    // view
    file_dialog_view->Show(&show_file_dialog_view, m_editor);

    // show load file dialog, true if a selection is made
    if (file_dialog_load->Show(&show_file_dialog_load, m_editor, nullptr, &mesh_import_file_path))
    {
        show_file_dialog_load = false;
        ShowMeshImportDialog(mesh_import_file_path);
    }

    mesh_import_dialog(m_editor);
}

void AssetBrowser::ShowMeshImportDialog(const string& file_path)
{
    if (FileSystem::IsSupportedModelFile(file_path))
    {
        mesh_import_dialog_is_visible = true;
        mesh_import_dialog_flags      = Mesh::GetDefaultFlags();
        mesh_import_file_path         = file_path;
    }
}

void AssetBrowser::OnPathClicked(const string& path) const
{
    if (!FileSystem::IsFile(path))
    {
        return;
    }

    if (FileSystem::IsEngineMaterialFile(path))
    {
        const auto material = ResourceCache::Load<Material>(path);
        Properties::Inspect(material);
    }
}
