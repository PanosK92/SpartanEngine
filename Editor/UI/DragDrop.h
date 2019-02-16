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

//= INCLUDES =====================
#include "../ImGui/Source/imgui.h"
#include "EditorHelper.h"
#include "IconProvider.h"
#include <variant>
//================================

enum DragPayloadType
{
	DragPayload_Unknown,
	DragPayload_Texture,
	DragPayload_entity,
	DragPayload_Model,
	DragPayload_Audio,
	DragPayload_Script
};

struct DragDropPayload
{
	typedef std::variant<const char*, unsigned int> dataVariant;
	DragDropPayload(DragPayloadType type = DragPayload_Unknown, dataVariant data = nullptr)
	{
		this->type = type;
		this->data = data;
	}
	DragPayloadType type;
	dataVariant data;
};

static bool g_isDragging = false;

class DragDrop
{
public:

	static DragDrop& Get()
	{
		static DragDrop instance;
		return instance;
	}

	void DragPayload(const DragDropPayload& payload, void* thumbnailShaderResource = nullptr)
	{
		ImGui::SetDragDropPayload((const char*)&payload.type, (void*)&payload, sizeof(payload), ImGuiCond_Once);
		if (thumbnailShaderResource)
		{
			THUMBNAIL_IMAGE_BY_SHADER_RESOURCE(thumbnailShaderResource, 50);
		}
	}

	DragDropPayload* GetPayload(DragPayloadType type)
	{
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload((const char*)&type))
			{
				return (DragDropPayload*)imguiPayload->Data;
			}
			ImGui::EndDragDropTarget();
		}

		return nullptr;
	}
};