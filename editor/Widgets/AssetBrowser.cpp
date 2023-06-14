/*
Copyright(c) 2016-2023 Panos Karabelas

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
//========================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
//======================

namespace
{
    static bool show_file_dialog_view         = true;
    static bool show_file_dialog_load         = false;
    static bool mesh_import_dialog_is_visible = false;
    static uint32_t mesh_import_dialog_flags  = 0;
    static string mesh_import_file_path;
}

static void mesh_import_dialog_checkbox(const MeshProcessingOptions option, const char* label, const char* tooltip = nullptr)
{
    bool enabled = (mesh_import_dialog_flags & (1U << static_cast<uint32_t>(option))) != 0;

    if (ImGui::Checkbox(label, &enabled))
    {
        if (enabled)
        {
            mesh_import_dialog_flags |= (1U << static_cast<uint32_t>(option));
        }
        else
        {
            mesh_import_dialog_flags &= ~(1U << static_cast<uint32_t>(option));
        }
    }

    if (tooltip != nullptr)
    {
        ImGuiSp::tooltip(tooltip);
    }
}

static void mesh_import_dialog()
{
    if (mesh_import_dialog_is_visible)
    {
        // Set window position
        ImVec2 position     = ImVec2(Spartan::Display::GetWidth() * 0.5f, Spartan::Display::GetHeight() * 0.5f);
        ImVec2 pivot_center = ImVec2(0.5f, 0.5f);
        ImGui::SetNextWindowPos(position, ImGuiCond_Appearing, pivot_center);

        // Begin
        if (ImGui::Begin("Mesh import options", &mesh_import_dialog_is_visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
        {
            mesh_import_dialog_checkbox(MeshProcessingOptions::RemoveRedundantData,
                "Remove redundant data",
                "Joins identical vertices, removes redundant materials, duplicate meshes, zeroed normals and invalid UVs.");

            mesh_import_dialog_checkbox(MeshProcessingOptions::NormalizeScale,
                "Normalize scale",
                "Scales the mesh so that it's not bigger than a cubic unit."
            );

            mesh_import_dialog_checkbox(MeshProcessingOptions::CombineMeshes,
                "Combine meshes",
                "Joins some meshes, removes some nodes and pretransforms vertices.");

            mesh_import_dialog_checkbox(MeshProcessingOptions::ImportLights, "Import lights");

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

AssetBrowser::AssetBrowser(Editor* editor) : Widget(editor)
{
    m_title            = "Assets";
    m_file_dialog_view = make_unique<FileDialog>(false, FileDialog_Type_Browser,       FileDialog_Op_Load, FileDialog_Filter_All);
    n_file_dialog_load = make_unique<FileDialog>(true,  FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_Model);
    m_flags           |= ImGuiWindowFlags_NoScrollbar;

    // Just clicked, not selected (double clicked, end of dialog)
    m_file_dialog_view->SetCallbackOnItemClicked([this](const string& str) { OnPathClicked(str); });
}

void AssetBrowser::TickVisible()
{    
    if (ImGuiSp::button("Import"))
    {
        show_file_dialog_load = true;
    }

    ImGui::SameLine();
    
    // View
    m_file_dialog_view->Show(&show_file_dialog_view);

    // Show load file dialog. True if a selection is made
    if (n_file_dialog_load->Show(&show_file_dialog_load, nullptr, &mesh_import_file_path))
    {
        show_file_dialog_load = false;
        ShowMeshImportDialog(mesh_import_file_path);
    }

    mesh_import_dialog();
}

void AssetBrowser::ShowMeshImportDialog(const std::string& file_path)
{
    if (FileSystem::IsSupportedModelFile(mesh_import_file_path))
    {
        mesh_import_dialog_is_visible = true;
        mesh_import_dialog_flags      = Mesh::GetDefaultFlags();
        mesh_import_file_path         = file_path;
    }
}

void AssetBrowser::OnPathClicked(const std::string& path) const
{
    if (!FileSystem::IsFile(path))
        return;

    if (FileSystem::IsEngineMaterialFile(path))
    {
        const auto material = ResourceCache::Load<Material>(path);
        Properties::Inspect(material);
    }
}
