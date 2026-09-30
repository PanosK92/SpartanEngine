/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "AssetBrowser.h"
#include "Properties.h"
#include "geometry/Mesh.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../widgets/FileDialog.h"
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

    // mcp requests arrive on the bridge thread, the widget applies them and publishes its state on the main thread
    mutex mcp_mutex;
    optional<AssetBrowserRequest> mcp_request;
    string mcp_pending_select;
    int mcp_pending_select_frames = 0;
    AssetBrowserState mcp_state;

    void apply_mcp_request()
    {
        optional<AssetBrowserRequest> request;
        {
            lock_guard<mutex> lock(mcp_mutex);
            request.swap(mcp_request);
        }

        if (request)
        {
            if (request->path)
            {
                string path = *request->path;
                if (!FileSystem::IsDirectory(path))
                {
                    path = ResourceCache::GetProjectDirectory() + path;
                }
                if (FileSystem::IsDirectory(path))
                {
                    file_dialog_view->SetCurrentPath(path);
                    file_dialog_view->SetKindFilter(-1);
                }
            }
            if (request->view)
            {
                file_dialog_view->SetViewMode(_stricmp(request->view->c_str(), "list") == 0 ? View_List : View_Grid);
            }
            if (request->size)
            {
                file_dialog_view->SetItemSize(*request->size);
            }
            if (request->search)
            {
                file_dialog_view->SetSearch(*request->search);
            }
            if (request->kind)
            {
                int kind = -1;
                for (int i = 0; i < Kind_Count; i++)
                {
                    if (_stricmp(request->kind->c_str(), FileDialog::GetKindName(i)) == 0 || _stricmp(request->kind->c_str(), FileDialog::GetKindPlural(i)) == 0)
                    {
                        kind = i;
                    }
                }
                file_dialog_view->SetKindFilter(kind);
            }
            if (request->select)
            {
                mcp_pending_select        = *request->select;
                mcp_pending_select_frames = 120;
            }
        }

        // a new folder loads its items at the end of the frame, so the selection is retried until the item exists
        if (!mcp_pending_select.empty())
        {
            if (file_dialog_view->SelectItem(mcp_pending_select) || --mcp_pending_select_frames <= 0)
            {
                mcp_pending_select.clear();
            }
        }
    }

    void publish_mcp_state()
    {
        AssetBrowserState state;
        state.path     = file_dialog_view->GetCurrentPath();
        state.view     = file_dialog_view->GetViewMode() == View_List ? "list" : "grid";
        state.size     = file_dialog_view->GetItemSize();
        state.kind     = file_dialog_view->GetKindFilter() >= 0 ? FileDialog::GetKindName(file_dialog_view->GetKindFilter()) : "all";
        state.selected = file_dialog_view->GetSelectedLabel();
        state.visible  = file_dialog_view->GetVisibleLabels();

        lock_guard<mutex> lock(mcp_mutex);
        mcp_state = move(state);
    }

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
            ImGui::EditorUi::push_primary_button();
            if (ImGui::Button("Import", ImVec2(button_width, 0.0f)))
            {
                spartan::ThreadPool::AddTask([]()
                {
                    spartan::ResourceCache::Load<spartan::Mesh>(mesh_import_file_path, mesh_import_dialog_flags);
                });
                mesh_import_dialog_is_visible = false;
            }
            ImGui::EditorUi::pop_primary_button();

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
    m_dock            = WidgetDock::DownRight;
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
    apply_mcp_request();
    file_dialog_view->Show(&show_file_dialog_view, m_editor);
    publish_mcp_state();

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

void AssetBrowser::Request(const AssetBrowserRequest& request)
{
    lock_guard<mutex> lock(mcp_mutex);
    mcp_request = request;
}

AssetBrowserState AssetBrowser::GetState()
{
    lock_guard<mutex> lock(mcp_mutex);
    return mcp_state;
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
        Properties::InspectMaterial(material);
    }
}
