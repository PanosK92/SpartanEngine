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

#pragma once

enum IconProvider_Icon
{
	Icon_Component_Options,
	Icon_Component_AudioListener,
	Icon_Component_AudioSource,
	Icon_Component_Camera,
	Icon_Component_Collider,
	Icon_Component_Light,
	Icon_Component_Material,
	Icon_Component_MeshCollider,
	Icon_Component_MeshFilter,
	Icon_Component_MeshRenderer,
	Icon_Component_RigidBody,
	Icon_Component_Script,
	Icon_Component_Transform,
	Icon_Console_Info,
	Icon_Console_Warning,
	Icon_Console_Error,
	Icon_File_Default,
	Icon_Folder,
	Icon_File_Audio,
	Icon_File_Scene,
	Icon_File_Model,
	Icon_Button_Play
};

namespace Directus { class Context; }

// An image
#define ICON_PROVIDER_IMAGE(icon_enum, size)	\
	ImGui::Image(								\
	IconProvider::GetIcon(icon_enum),			\
	ImVec2(size, size),							\
	ImVec2(0, 0),								\
	ImVec2(1, 1),								\
	ImColor(255, 255, 255, 255),				\
	ImColor(255, 255, 255, 0))					\

// An image button
#define ICON_PROVIDER_IMAGE_BUTTON(icon_enum, size) ImGui::ImageButton(ICON_PROVIDER(icon_enum), ImVec2(size, size))
// An image button with a specific ID
#define ICON_PROVIDER_IMAGE_BUTTON_ID(id, icon_enum, size) IconProvider::ImageButtonID(id, icon_enum, size)
// An icon shader resource pointer
#define ICON_PROVIDER(icon_enum) IconProvider::GetIcon(icon_enum)

class IconProvider
{
public:
	static void Initialize(Directus::Context* context);
	static void* GetIcon(IconProvider_Icon icon);

	static bool ImageButtonID(const char* id, IconProvider_Icon icon, float size);
};