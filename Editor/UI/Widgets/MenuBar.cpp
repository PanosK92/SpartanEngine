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

//= INCLUDES ==============
#include "MenuBar.h"
#include "../ImGui/imgui.h"
#include "../FileDialog.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
//=========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static bool g_showAboutWindow		= false;
static bool g_showMetricsWindow		= false;
static bool g_showStyleEditor		= false;
static bool g_fileDialogVisible		= false;
static bool g_showResourceCache		= false;
static string g_fileDialogSelection;
ResourceManager* g_resourceManager	= nullptr;
Scene* g_scene						= nullptr;

MenuBar::MenuBar()
{
	m_isWindow = false;
}

void MenuBar::Initialize(Context* context)
{
	Widget::Initialize(context);
	g_resourceManager = m_context->GetSubsystem<ResourceManager>();
	g_scene = m_context->GetSubsystem<Scene>();
	m_fileDialog = make_unique<FileDialog>(m_context, true, FileDialog_Scene);
}

void MenuBar::Update()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Scene"))
		{
			if (ImGui::MenuItem("New"))
			{
				m_context->GetSubsystem<Scene>()->Clear();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Load"))
			{
				m_fileDialog->SetStyle(FileDialog_Load);
				g_fileDialogVisible = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save"))
			{
				m_fileDialog->SetStyle(FileDialog_Save);
				g_fileDialogVisible = true;
			}

			if (ImGui::MenuItem("Save As..."))
			{
				m_fileDialog->SetStyle(FileDialog_Save);
				g_fileDialogVisible = true;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Editor"))
		{
			ImGui::MenuItem("Metrics", nullptr, &g_showMetricsWindow);
			ImGui::MenuItem("Style", nullptr, &g_showStyleEditor);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Debug"))
		{
			ImGui::MenuItem("Resource Cache Viewer", nullptr, &g_showResourceCache);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::MenuItem("About", nullptr, &g_showAboutWindow);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (g_showMetricsWindow)
	{
		ImGui::ShowMetricsWindow();
	}
	if (g_showStyleEditor)
	{
		ImGui::ShowStyleEditor();
	}

	ShowFileDialog();
	ShowAboutWindow();
	ShowResourceCache();
}

void MenuBar::ShowFileDialog()
{
	if (!g_fileDialogVisible)
		return;

	if (!m_fileDialog->Show(&g_fileDialogVisible, &g_fileDialogSelection))
		return;

	// LOAD
	if (m_fileDialog->GetStyle() == FileDialog_Open || m_fileDialog->GetStyle() == FileDialog_Load)
	{
		// Scene
		if (FileSystem::IsEngineSceneFile(g_fileDialogSelection))
		{
			EditorHelper::Get().LoadScene(g_fileDialogSelection);
			g_fileDialogVisible = false;
		}
	}
	// SAVE
	else if (m_fileDialog->GetStyle() == FileDialog_Save)
	{
		// Scene
		if (m_fileDialog->GetFilter() == FileDialog_Scene)
		{
			EditorHelper::Get().SaveScene(g_fileDialogSelection);
			g_fileDialogVisible = false;
		}
	}
}

void MenuBar::ShowAboutWindow()
{
	if (!g_showAboutWindow)
		return;

	ImGui::Begin("About", &g_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

	ImGui::Text("Directus3D %s", ENGINE_VERSION);
	ImGui::Text("Author: Panos Karabelas");
	ImGui::SameLine(600); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);  if (ImGui::Button("GitHub"))
	{
		FileSystem::OpenDirectoryWindow("https://github.com/PanosK92/Directus3D");
	}	

	ImGui::Separator();

	ImGui::Text("MIT License");
	ImGui::Text("Permission is hereby granted, free of charge, to any person obtaining a copy");
	ImGui::Text("of this software and associated documentation files(the \"Software\"), to deal");
	ImGui::Text("in the Software without restriction, including without limitation the rights");
	ImGui::Text("to use, copy, modify, merge, publish, distribute, sublicense, and / or sell");
	ImGui::Text("copies of the Software, and to permit persons to whom the Software is furnished");
	ImGui::Text("to do so, subject to the following conditions :");
	ImGui::Text("The above copyright notice and this permission notice shall be included in");
	ImGui::Text("all copies or substantial portions of the Software.");
	ImGui::Text("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR");
	ImGui::Text("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS");
	ImGui::Text("FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR");
	ImGui::Text("COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER");
	ImGui::Text("IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN");
	ImGui::Text("CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.");

	ImGui::Separator();

	ImGui::Text("Third party libraries");
	static float posX = 120;
	ImGui::BulletText("AngelScript");	ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionAngelScript).c_str());
	ImGui::BulletText("Assimp");		ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionAssimp).c_str());
	ImGui::BulletText("Bullet");		ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionBullet).c_str());
	ImGui::BulletText("FMOD ");			ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionFMOD).c_str());
	ImGui::BulletText("FreeImage");		ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionFreeImage).c_str());
	ImGui::BulletText("FreeType");		ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionFreeType).c_str());
	ImGui::BulletText("ImGui");			ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionImGui).c_str());
	ImGui::BulletText("PugiXML");		ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionPugiXML).c_str());
	ImGui::BulletText("SDL");			ImGui::SameLine(posX); ImGui::Text(("v" + Settings::g_versionSDL).c_str());

	ImGui::End();
}

void MenuBar::ShowResourceCache()
{
	if (!g_showResourceCache)
		return;

	auto resources = m_context->GetSubsystem<ResourceManager>()->GetResourceAll();
	auto totalMemoryUsage =  m_context->GetSubsystem<ResourceManager>()->GetMemoryUsage() / 1000.0f / 1000.0f;

	ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin("Resource Cache Viewer", &g_showResourceCache, ImGuiWindowFlags_HorizontalScrollbar);

	ImGui::Text("Resource count: %d, Total memory usage: %d Mb", (int)resources.size(), (int)totalMemoryUsage);
	ImGui::Separator();
	ImGui::Columns(5, "##ResourceCacheViewer");
	ImGui::Text("Type"); ImGui::NextColumn();
	ImGui::Text("ID"); ImGui::NextColumn();
	ImGui::Text("Name"); ImGui::NextColumn();
	ImGui::Text("Path"); ImGui::NextColumn();
	ImGui::Text("Size"); ImGui::NextColumn();
	ImGui::Separator();
	for (const auto& resource : resources)
	{
		if (!resource)
			continue;

		// Type
		ImGui::Text(resource->GetResourceTypeStr().c_str());			ImGui::NextColumn();

		// ID
		ImGui::Text(to_string(resource->GetResourceID()).c_str());		ImGui::NextColumn();

		// Name
		ImGui::Text(resource->GetResourceName().c_str());				ImGui::NextColumn();

		// Path
		ImGui::Text(resource->GetResourceFilePath().c_str());			ImGui::NextColumn();

		// Memory
		unsigned int memory = resource->GetMemory() / 1000.0f; // default in Kb
		if (memory <= 1024)
		{
			ImGui::Text((to_string(memory) + string(" Kb")).c_str());	ImGui::NextColumn();
		}
		else
		{
			memory = memory / 1000.0f; // turn into Mb
			ImGui::Text((to_string(memory) + string(" Mb")).c_str());	ImGui::NextColumn();
		}		
	}
	ImGui::Columns(1);

	ImGui::End();
}