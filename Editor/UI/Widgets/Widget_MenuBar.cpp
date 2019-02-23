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

//= INCLUDES ====================
#include "Widget_MenuBar.h"
#include "../FileDialog.h"
#include "Core/Settings.h"
#include "Widget_ResourceCache.h"
#include "Widget_Profiler.h"
//===============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace _Widget_MenuBar
{
	static bool g_showAboutWindow	= false;
	static bool g_fileDialogVisible	= false;
	static bool imgui_metrics		= false;
	static bool imgui_style			= false;
	static bool imgui_demo			= false;

	ResourceCache* g_resourceCache	= nullptr;
	World* g_scene					= nullptr;
	static string g_fileDialogSelection;
}

Widget_MenuBar::Widget_MenuBar(Context* context) : Widget(context)
{
	m_isWindow = false;

	m_profiler		= make_unique<Widget_Profiler>(context);
	m_resourceCache = make_unique<Widget_ResourceCache>(context);
	m_fileDialog	= make_unique<FileDialog>(m_context, true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_Scene);

	_Widget_MenuBar::g_resourceCache	= m_context->GetSubsystem<ResourceCache>().get();
	_Widget_MenuBar::g_scene			= m_context->GetSubsystem<World>().get();
}

void Widget_MenuBar::Tick(float deltaTime)
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("World"))
		{
			if (ImGui::MenuItem("New"))
			{
				m_context->GetSubsystem<World>()->Unload();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Load"))
			{
				m_fileDialog->SetOperation(FileDialog_Op_Load);
				_Widget_MenuBar::g_fileDialogVisible = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save"))
			{
				m_fileDialog->SetOperation(FileDialog_Op_Save);
				_Widget_MenuBar::g_fileDialogVisible = true;
			}

			if (ImGui::MenuItem("Save As..."))
			{
				m_fileDialog->SetOperation(FileDialog_Op_Save);
				_Widget_MenuBar::g_fileDialogVisible = true;
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools"))
		{
			ImGui::MenuItem("Resource Cache Viewer", nullptr, &m_resourceCache->GetVisible());
			ImGui::MenuItem("Profiler", nullptr, &m_profiler->GetVisible());
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("ImGui Metrics",	nullptr, &_Widget_MenuBar::imgui_metrics);
			ImGui::MenuItem("ImGui Style",		nullptr, &_Widget_MenuBar::imgui_style);
			ImGui::MenuItem("ImGui Demo",		nullptr, &_Widget_MenuBar::imgui_demo);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			ImGui::MenuItem("About", nullptr, &_Widget_MenuBar::g_showAboutWindow);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (_Widget_MenuBar::imgui_metrics)			{ ImGui::ShowMetricsWindow(); }
	if (_Widget_MenuBar::imgui_style)			{ ImGui::Begin("Style Editor", nullptr, ImGuiWindowFlags_NoDocking); ImGui::ShowStyleEditor(); ImGui::End(); }
	if (_Widget_MenuBar::imgui_demo)			{ ImGui::ShowDemoWindow(&_Widget_MenuBar::imgui_demo); }
	if (_Widget_MenuBar::g_fileDialogVisible)	{ ImGui::SetNextWindowFocus(); ShowFileDialog(); }
	if (_Widget_MenuBar::g_showAboutWindow)		{ ImGui::SetNextWindowFocus(); ShowAboutWindow(); }

	if (m_resourceCache->GetVisible())
	{
		m_resourceCache->Begin();
		m_resourceCache->Tick(deltaTime);
		m_resourceCache->End();
	}

	if (m_profiler->GetVisible())
	{
		m_profiler->Begin();
		m_profiler->Tick(deltaTime);
		m_profiler->End();
	}
}

void Widget_MenuBar::ShowFileDialog()
{
	if (m_fileDialog->Show(&_Widget_MenuBar::g_fileDialogVisible, nullptr, &_Widget_MenuBar::g_fileDialogSelection))
	{
		// LOAD
		if (m_fileDialog->GetOperation() == FileDialog_Op_Open || m_fileDialog->GetOperation() == FileDialog_Op_Load)
		{
			// Scene
			if (FileSystem::IsEngineSceneFile(_Widget_MenuBar::g_fileDialogSelection))
			{
				EditorHelper::Get().LoadScene(_Widget_MenuBar::g_fileDialogSelection);
				_Widget_MenuBar::g_fileDialogVisible = false;
			}
		}
		// SAVE
		else if (m_fileDialog->GetOperation() == FileDialog_Op_Save)
		{
			// Scene
			if (m_fileDialog->GetFilter() == FileDialog_Filter_Scene)
			{
				EditorHelper::Get().SaveScene(_Widget_MenuBar::g_fileDialogSelection);
				_Widget_MenuBar::g_fileDialogVisible = false;
			}
		}
	}
}

void Widget_MenuBar::ShowAboutWindow()
{
	ImGui::Begin("About", &_Widget_MenuBar::g_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

	ImGui::Text("Directus3D %s", ENGINE_VERSION);
	ImGui::Text("Author: Panos Karabelas");
	ImGui::SameLine(600); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);  if (ImGui::Button("GitHub"))
	{
		FileSystem::OpenDirectoryWindow("https://github.com/PanosK92/Directus3D");
	}	

	ImGui::Separator();

	ImGui::BeginChildFrame(ImGui::GetID("about_license"), ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15.5f), ImGuiWindowFlags_NoMove);
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
	ImGui::EndChildFrame();

	ImGui::Separator();

	ImGui::Text("Third party libraries");
	const auto library = [](const char* name, const string& version, const char* url)
	{
		ImGui::BulletText(name);
		ImGui::SameLine(120); ImGui::Text(("v" + version).c_str());
		ImGui::SameLine(200); ImGui::PushID(url);  if (ImGui::Button("URL")) { FileSystem::OpenDirectoryWindow(url); } ImGui::PopID();
	};

	library("AngelScript",	Settings::Get().m_versionAngelScript,	"https://www.angelcode.com/angelscript/");
	library("Assimp",		Settings::Get().m_versionAssimp,		"https://github.com/assimp/assimp");
	library("Bullet",		Settings::Get().m_versionBullet,		"https://github.com/bulletphysics/bullet3");
	library("FMOD",			Settings::Get().m_versionFMOD,			"https://www.fmod.com/");
	library("FreeImage",	Settings::Get().m_versionFreeImage,		"https://sourceforge.net/projects/freeimage/files/Source%20Distribution/");
	library("FreeType",		Settings::Get().m_versionFreeType,		"https://www.freetype.org/");
	library("ImGui",		Settings::Get().m_versionImGui,			"https://github.com/ocornut/imgui");
	library("PugiXML",		Settings::Get().m_versionPugiXML,		"https://github.com/zeux/pugixml");

	ImGui::End();
}