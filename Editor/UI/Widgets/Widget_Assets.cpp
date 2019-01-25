/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =================
#include "Widget_Assets.h"
#include "../FileDialog.h"
#include "Widget_Properties.h"
//============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace Widget_Assets_Statics
{
	static bool g_showFileDialogView = true;
	static bool g_showFileDialogLoad = false;
	static string g_doubleClickedPath_ImportDialog;
}

Widget_Assets::Widget_Assets(Context* context) : Widget(context)
{
	m_title = "Assets";
	m_fileDialogView = make_unique<FileDialog>(m_context, false, FileDialog_Type_Browser, FileDialog_Op_Load, FileDialog_Filter_All);
	m_fileDialogLoad = make_unique<FileDialog>(m_context, true, FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_Model);
	m_windowFlags |= ImGuiWindowFlags_NoScrollbar;

	// Just clicked, not selected (double clicked, end of dialog)
	m_fileDialogView->SetCallback_OnItemClicked([this](const string& str) { OnPathClicked(str); });
}

void Widget_Assets::Tick(float deltaTime)
{	
	if (ImGui::Button("Import"))
	{
		Widget_Assets_Statics::g_showFileDialogLoad = true;
	}

	ImGui::SameLine();
	
	// VIEW
	m_fileDialogView->Show(&Widget_Assets_Statics::g_showFileDialogView);

	// IMPORT
	if (m_fileDialogLoad->Show(&Widget_Assets_Statics::g_showFileDialogLoad, nullptr, &Widget_Assets_Statics::g_doubleClickedPath_ImportDialog))
	{
		// Model
		if (FileSystem::IsSupportedModelFile(Widget_Assets_Statics::g_doubleClickedPath_ImportDialog))
		{
			EditorHelper::Get().LoadModel(Widget_Assets_Statics::g_doubleClickedPath_ImportDialog);
			Widget_Assets_Statics::g_showFileDialogLoad = false;
		}
	}
}

void Widget_Assets::OnPathClicked(const std::string& path)
{
	if (FileSystem::IsEngineMaterialFile(path))
	{
		auto material = m_context->GetSubsystem<ResourceCache>()->Load<Material>(path);
		Widget_Properties::Inspect(material);
	}
}
