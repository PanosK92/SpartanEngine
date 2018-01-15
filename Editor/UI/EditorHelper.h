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

//= INCLUDES ========================
#include <string>
#include "imgui/imgui.h"
#include "Math/Vector4.h"
#include "Math/Vector2.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceManager.h"
//===================================

static const int BUFFER_TEXT_DEFAULT = 255;
static const char* g_dragDrop_Texture = "Texture";

inline void SetCharArray(char* array, const std::string& value)
{
	if (value.length() > BUFFER_TEXT_DEFAULT)
		return;

	memset(&array[0], 0, BUFFER_TEXT_DEFAULT * sizeof(array[0]));
	copy(value.begin(), value.end(), array);
}

template <class T, class = typename std::enable_if<
	std::is_same<T, int>::value		||
	std::is_same<T, float>::value	||
	std::is_same<T, bool>::value	||
	std::is_same<T, double>::value
>::type>
void SetCharArray(char* array, T value) { SetCharArray(array, std::to_string(value)); }

inline ImVec4 ToImVec4(const Directus::Math::Vector4& vector)
{
	return ImVec4
	(
		vector.x,
		vector.y,
		vector.z,
		vector.w
	);
}

inline Directus::Math::Vector4 ToVector4(const ImVec4& vector)
{
	return Directus::Math::Vector4
	(
		vector.x,
		vector.y,
		vector.z,
		vector.w
	);
}

inline ImVec2 ToImVec2(const Directus::Math::Vector2& vector)
{
	return ImVec2{vector.x,vector.y};
}

inline Directus::Math::Vector2 ToVector2(const ImVec2& vector)
{
	return Directus::Math::Vector2(vector.x, vector.y);
}

inline std::weak_ptr<Directus::Texture> GetOrLoadTexture(const std::string& filePath, Directus::Context* context)
{
	auto resourceManager = context->GetSubsystem<Directus::ResourceManager>();
	auto texture = resourceManager->GetResourceByPath<Directus::Texture>(filePath);
	if (!texture.expired())
		return texture;

	texture = resourceManager->Load<Directus::Texture>(filePath);
	return texture;
}

inline void SendPayload(const char* type, const std::string& text)
{
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(type, &text[0], text.size(), ImGuiCond_Once);
		ImGui::EndDragDropSource();
	}
}

inline void GetPayload(const char* type, std::string* result)
{
	if (!result)
		return;

	result->clear();
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
		{
			void* ptr = const_cast<void*>(payload->Data);
			(*result) = (char*)ptr;
		}
		ImGui::EndDragDropTarget();
	}
}