/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =============================
#include "AssetBrowser.h"
#include "Properties.h"
#include "Rendering/Mesh.h"
#include "../WidgetsDeferred/FileDialog.h"
#include "Viewport.h"
//========================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
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
        if (mesh_import_dialog_is_visible)
        {
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    
            // Begin
            if (ImGui::Begin("Mesh import options", &mesh_import_dialog_is_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
            {
                mesh_import_dialog_checkbox(MeshFlags::ImportRemoveRedundantData,
                    "Remove redundant data",
                    "Join identical vertices, remove redundant materials, duplicate meshes, zeroed normals and invalid UVs.");
    
                mesh_import_dialog_checkbox(MeshFlags::ImportNormalizeScale,
                    "Normalize scale",
                    "Scale the mesh so that it's not bigger than a cubic unit."
                );
    
                mesh_import_dialog_checkbox(MeshFlags::ImportCombineMeshes,
                    "Combine meshes",
                    "Join some meshes, remove some nodes and pre-transform vertices."
                );
    
                mesh_import_dialog_checkbox(MeshFlags::ImportLights,
                    "Import lights",
                    "Some models might define lights, they can be imported as well."
                );

                mesh_import_dialog_checkbox(MeshFlags::OptimizeVertexCache,
                    "Optimize vertex cache (slower import)",
                    "Improve the GPU's post-transform cache hit rate, reducing the required vertex shader invocations"
                );

                mesh_import_dialog_checkbox(MeshFlags::OptimizeVertexFetch,
                    "Optimize vertex fetch (slower import)",
                    "Reorder vertices and changes indices to improve vertex fetch cache performance, reducing the bandwidth needed to fetch vertices"
                );

                mesh_import_dialog_checkbox(MeshFlags::OptimizeOverdraw,
                    "Optimize overdraw (slower import)",
                    "Minimize overdraw by reordering triangles, aiming to reduce pixel shader invocations"
                );
    
                // Ok button
                if (ImGuiSp::button_centered_on_line("Ok", 0.5f))
                {
                    EditorHelper::LoadMesh(mesh_import_file_path, mesh_import_dialog_flags);
                    mesh_import_dialog_is_visible = false;
                }
            }
    
            ImGui::End();
        }
    }
}

AssetBrowser::AssetBrowser(Editor* editor) : Widget(editor)
{
    m_title             = "Assets";
    file_dialog_view  = make_unique<FileDialog>(false, FileDialog_Type_Browser,       FileDialog_Op_Load, FileDialog_Filter_All);
    file_dialog_load  = make_unique<FileDialog>(true,  FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_Model);
    m_flags            |= ImGuiWindowFlags_NoScrollbar;

    // just clicked, not selected (double clicked, end of dialog)
    file_dialog_view->SetCallbackOnItemClicked([this](const string& str) { OnPathClicked(str); });
}

void AssetBrowser::OnTickVisible()
{    
    if (ImGuiSp::button("Import"))
    {
        show_file_dialog_load = true;
    }

    ImGui::SameLine();
    
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
    if (FileSystem::IsSupportedModelFile(mesh_import_file_path))
    {
        mesh_import_dialog_is_visible = true;
        mesh_import_dialog_flags      = Mesh::GetDefaultFlags();
        mesh_import_file_path         = file_path;
    }
}

void AssetBrowser::OnPathClicked(const string& path) const
{
    if (!FileSystem::IsFile(path))
        return;

    if (FileSystem::IsEngineMaterialFile(path))
    {
        const auto material = ResourceCache::Load<Material>(path);
        Properties::Inspect(material);
    }
}
