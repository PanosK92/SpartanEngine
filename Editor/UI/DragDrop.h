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

//= INCLUDES ===========
#include "imgui/imgui.h"
//======================

struct DragDropPayload
{
	DragDropPayload()
	{
		type	= nullptr;
		payload = nullptr;
	}

	DragDropPayload(const char* type, const char* payload)
	{
		this->type		= type;
		this->payload	= payload;
	}
	const char* type;
	const char* payload;
};
static const char* g_dragDrop_Type_Texture = "Texture";

class DragDrop
{
public:

	static void SendPayload(const DragDropPayload& payload)
	{
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(payload.type, (void*)&payload, sizeof(payload), ImGuiCond_Once);
			ImGui::EndDragDropSource();
			LOG_INFO("Payload send");
		}
	}

	static void GetPayload(DragDropPayload* payload)
	{
		if (!payload)
			return;

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* imguiPayload = ImGui::AcceptDragDropPayload(payload->type))
			{
				void* ptr = const_cast<void*>(imguiPayload->Data);
				payload = (DragDropPayload*)ptr;
				LOG_INFO("Payload received");
			}
			ImGui::EndDragDropTarget();
		}
	}
};