/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ================
#include "IconProvider.h"
#include <map>
#include "Graphics/Texture.h"
#include "Logging/Log.h"
#include "imgui/imgui.h"
//===========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static map<IconProvider_Icon, unique_ptr<Texture>> g_icons;

void IconProvider::Initialize(Context* context)
{
	auto LoadIcon = [context](IconProvider_Icon icon, char* filePath)
	{
		g_icons[icon] = make_unique<Texture>(context);
		if (!g_icons[icon]->LoadFromFile(filePath))
		{
			LOG_ERROR("IconProvider: Failed to load " + string(filePath));
		}
	};

	LoadIcon(Icon_Component_Options,		"Standard Assets\\Editor\\component_ComponentOptions.png");
	LoadIcon(Icon_Component_AudioListener,	"Standard Assets\\Editor\\component_AudioListener.png");
	LoadIcon(Icon_Component_AudioSource,	"Standard Assets\\Editor\\component_AudioSource.png");
	LoadIcon(Icon_Component_Camera,			"Standard Assets\\Editor\\component_Camera.png");
	LoadIcon(Icon_Component_Collider,		"Standard Assets\\Editor\\component_Collider.png");
	LoadIcon(Icon_Component_Light,			"Standard Assets\\Editor\\component_Light.png");
	LoadIcon(Icon_Component_Material,		"Standard Assets\\Editor\\component_Material.png");
	LoadIcon(Icon_Component_MeshCollider,	"Standard Assets\\Editor\\component_MeshCollider.png");
	LoadIcon(Icon_Component_MeshFilter,		"Standard Assets\\Editor\\component_MeshFilter.png");
	LoadIcon(Icon_Component_MeshRenderer,	"Standard Assets\\Editor\\component_MeshRenderer.png");
	LoadIcon(Icon_Component_RigidBody,		"Standard Assets\\Editor\\component_RigidBody.png");
	LoadIcon(Icon_Component_Script,			"Standard Assets\\Editor\\component_Script.png");
	LoadIcon(Icon_Component_Transform,		"Standard Assets\\Editor\\component_Transform.png");
	LoadIcon(Icon_Console_Info,				"Standard Assets\\Editor\\console_info.png");
	LoadIcon(Icon_Console_Warning,			"Standard Assets\\Editor\\console_warning.png");
	LoadIcon(Icon_Console_Error,			"Standard Assets\\Editor\\console_error.png");
	LoadIcon(Icon_File_Default,				"Standard Assets\\Editor\\file.png");
	LoadIcon(Icon_Folder,					"Standard Assets\\Editor\\folder.png");
	LoadIcon(Icon_File_Audio,				"Standard Assets\\Editor\\audio.png");
	LoadIcon(Icon_File_Model,				"Standard Assets\\Editor\\model.png");
	LoadIcon(Icon_File_Scene,				"Standard Assets\\Editor\\scene.png");
	LoadIcon(Icon_Button_Play,				"Standard Assets\\Editor\\play.png");
}

void* IconProvider::GetIcon(IconProvider_Icon icon)
{
	if (g_icons.find(icon) == g_icons.end())
		return nullptr;

	return (void*)g_icons[icon]->GetShaderResource();
}

bool IconProvider::ImageButtonID(const char* id, IconProvider_Icon icon, float size)
{
	ImGui::PushID(id);
	bool pressed = ImGui::ImageButton(GetIcon(icon),ImVec2(size, size));
	ImGui::PopID();

	return pressed;
}
