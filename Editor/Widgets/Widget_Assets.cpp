/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Widget_Assets.h"
#include "Widget_Properties.h"
#include "Rendering/Model.h"
#include "../WidgetsDeferred/FileDialog.h"
//========================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace Widget_Assets_Statics
{
    static bool g_show_file_dialog_view = true;
    static bool g_show_file_dialog_load = false;
    static string g_double_clicked_path_import_dialog;
}

Widget_Assets::Widget_Assets(Editor* editor) : Widget(editor)
{
    m_title             = "Assets";
    m_fileDialogView    = make_unique<FileDialog>(m_context, false, FileDialog_Type_Browser,        FileDialog_Op_Load, FileDialog_Filter_All);
    m_fileDialogLoad    = make_unique<FileDialog>(m_context, true,  FileDialog_Type_FileSelection,  FileDialog_Op_Load, FileDialog_Filter_Model);
    m_flags             |= ImGuiWindowFlags_NoScrollbar;

    // Just clicked, not selected (double clicked, end of dialog)
    m_fileDialogView->SetCallbackOnItemClicked([this](const string& str) { OnPathClicked(str); });
}

void Widget_Assets::Tick()
{    
    if (ImGui::Button("Import"))
    {
        Widget_Assets_Statics::g_show_file_dialog_load = true;
    }

    ImGui::SameLine();
    
    // VIEW
    m_fileDialogView->Show(&Widget_Assets_Statics::g_show_file_dialog_view);

    // IMPORT
    if (m_fileDialogLoad->Show(&Widget_Assets_Statics::g_show_file_dialog_load, nullptr, &Widget_Assets_Statics::g_double_clicked_path_import_dialog))
    {
        // Model
        if (FileSystem::IsSupportedModelFile(Widget_Assets_Statics::g_double_clicked_path_import_dialog))
        {
            EditorHelper::Get().LoadModel(Widget_Assets_Statics::g_double_clicked_path_import_dialog);
            Widget_Assets_Statics::g_show_file_dialog_load = false;
        }
    }
}

void Widget_Assets::OnPathClicked(const std::string& path) const
{
    if (!FileSystem::IsFile(path))
        return;

    if (FileSystem::IsEngineMaterialFile(path))
    {
        const auto material = m_context->GetSubsystem<ResourceCache>()->Load<Material>(path);
        Widget_Properties::Inspect(material);
    }
}
