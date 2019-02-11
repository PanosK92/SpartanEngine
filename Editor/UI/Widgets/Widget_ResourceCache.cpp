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

#pragma once

//= INCLUDES ========================
#include "Widget_ResourceCache.h"
#include "Resource/ResourceCache.h"
#include "../../ImGui/Source/imgui.h"
//===================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

Widget_ResourceCache::Widget_ResourceCache(Context* context) : Widget(context)
{
	m_title			= "Resource Cache";
	m_windowFlags	= ImGuiWindowFlags_HorizontalScrollbar;
	m_isVisible		= false;
}

void Widget_ResourceCache::Tick(float deltaTime)
{
	auto resourceCache		= m_context->GetSubsystem<ResourceCache>();
	auto resources			= resourceCache->GetByType();
	auto totalMemoryUsage	= resourceCache->GetMemoryUsage() / 1000.0f / 1000.0f;

	ImGui::Text("Resource count: %d, Total memory usage: %d Mb", (int)resources.size(), (int)totalMemoryUsage);
	ImGui::Separator();
	ImGui::Columns(5, "##MenuBar::ShowResourceCacheColumns");
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
		ImGui::Text(resource->GetResourceType_cstr());					ImGui::NextColumn();

		// ID
		ImGui::Text(to_string(resource->Resource_GetID()).c_str());		ImGui::NextColumn();

		// Name
		ImGui::Text(resource->GetResourceName().c_str());				ImGui::NextColumn();

		// Path
		ImGui::Text(resource->GetResourceFilePath().c_str());			ImGui::NextColumn();

		// Memory
		auto memory = (unsigned int)(resource->GetMemoryUsage() / 1000.0f); // default in Kb
		if (memory <= 1024)
		{
			ImGui::Text((to_string(memory) + string(" Kb")).c_str());	ImGui::NextColumn();
		}
		else
		{
			memory = (unsigned int)(memory / 1000.0f); // turn into Mb
			ImGui::Text((to_string(memory) + string(" Mb")).c_str());	ImGui::NextColumn();
		}
	}
	ImGui::Columns(1);
}
