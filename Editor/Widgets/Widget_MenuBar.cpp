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

//= INCLUDES ===============
#include "Widget_MenuBar.h"
#include "../FileDialog.h"
#include "Core/Settings.h"
#include "Rendering\Model.h"
//==========================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace _Widget_MenuBar
{
	static bool g_showAboutWindow	= false;
	static bool g_fileDialogVisible	= false;
	static bool imgui_metrics		= false;
	static bool imgui_style			= false;
	static bool imgui_demo			= false;

	World* world					= nullptr;
	static string g_fileDialogSelection;
}

Widget_MenuBar::Widget_MenuBar(Context* context) : Widget(context)
{
	m_is_window				= false;
	m_fileDialog			= make_unique<FileDialog>(m_context, true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_Scene);
	_Widget_MenuBar::world	= m_context->GetSubsystem<World>().get();
}

void Widget_MenuBar::Tick()
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

	if (_Widget_MenuBar::imgui_metrics)	{ ImGui::ShowMetricsWindow(); }
	if (_Widget_MenuBar::imgui_style)	{ ImGui::Begin("Style Editor", nullptr, ImGuiWindowFlags_NoDocking); ImGui::ShowStyleEditor(); ImGui::End(); }
	if (_Widget_MenuBar::imgui_demo)	{ ImGui::ShowDemoWindow(&_Widget_MenuBar::imgui_demo); }
    ShowFileDialog();
    ShowAboutWindow();
}

void Widget_MenuBar::ShowFileDialog()
{
    if (_Widget_MenuBar::g_fileDialogVisible)
    {
        ImGui::SetNextWindowFocus();
    }

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
    if (!_Widget_MenuBar::g_showAboutWindow)
        return;

    ImGui::SetNextWindowFocus();
	ImGui::Begin("About", &_Widget_MenuBar::g_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

	ImGui::Text("Spartan %s", engine_version);
	ImGui::Text("Author: Panos Karabelas");
	ImGui::SameLine(600); ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);  if (ImGui::Button("GitHub"))
	{
		FileSystem::OpenDirectoryWindow("https://github.com/PanosK92/SpartanEngine");
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
		ImGui::SameLine(140); ImGui::Text(("v" + version).c_str());
		ImGui::SameLine(250); ImGui::PushID(url);  if (ImGui::Button("URL")) { FileSystem::OpenDirectoryWindow(url); } ImGui::PopID();
	};

#if defined(API_GRAPHICS_D3D11)
    const char* api_name = "DirectX";
    const char* api_link = "https://www.microsoft.com/en-us/download/details.aspx?id=17431";
#elif defined(API_GRAPHICS_VULKAN)
    const char* api_name = "Vulkan";
    const char* api_link = "https://www.khronos.org/vulkan/";
#endif

    auto& settings = m_context->GetSubsystem<Settings>();

	library(api_name,	    settings->m_versionGraphicsAPI,	api_link);
	library("AngelScript",	settings->m_versionAngelScript,	"https://www.angelcode.com/angelscript/");
	library("Assimp",		settings->m_versionAssimp,		"https://github.com/assimp/assimp");
	library("Bullet",		settings->m_versionBullet,		"https://github.com/bulletphysics/bullet3");
	library("FMOD",			settings->m_versionFMOD,		"https://www.fmod.com/");
	library("FreeImage",	settings->m_versionFreeImage,	"https://sourceforge.net/projects/freeimage/files/Source%20Distribution/");
	library("FreeType",		settings->m_versionFreeType,	"https://www.freetype.org/");
	library("ImGui",		settings->m_versionImGui,		"https://github.com/ocornut/imgui");
	library("PugiXML",		settings->m_versionPugiXML,		"https://github.com/zeux/pugixml");

	ImGui::End();
}
