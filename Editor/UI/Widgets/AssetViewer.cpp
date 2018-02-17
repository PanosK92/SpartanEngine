/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "AssetViewer.h"
#include "../ImGui/imgui.h"
#include "FileSystem/FileSystem.h"
#include "../EditorHelper.h"
#include "../FileDialog.h"
//================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static bool g_showFileDialogView = true;
static bool g_showFileDialogLoad = false;
static string g_fileDialogSelection_View;
static string g_fileDialogSelection_Load;

AssetViewer::AssetViewer()
{
	m_title = "Assets";
}

void AssetViewer::Initialize(Context* context)
{
	Widget::Initialize(context);
	m_fileDialogView	= make_unique<FileDialog>(m_context, false, FileDialog_All);
	m_fileDialogLoad	= make_unique<FileDialog>(m_context, true, FileDialog_Model, FileDialog_Load);
}

void AssetViewer::Update()
{	
	if (ImGui::Button("Import"))
	{
		g_showFileDialogLoad = true;
	}
	
	// VIEW
	if (m_fileDialogView->Show(&g_showFileDialogView, &g_fileDialogSelection_View))
	{
		
	}

	// IMPORT
	if (m_fileDialogLoad->Show(&g_showFileDialogLoad, &g_fileDialogSelection_Load))
	{
		// Model
		if (FileSystem::IsSupportedModelFile(g_fileDialogSelection_Load))
		{
			EditorHelper::Get().LoadModel(g_fileDialogSelection_Load);
			g_showFileDialogLoad = false;
		}
	}
}
