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

//= INCLUDES =======================
#include <string>
#include <variant>
#include "../ImGui/Source/imgui.h"
#include "RHI/RHI_Texture.h"
#include "World/World.h"
#include "World/Components/Camera.h"
#include "Rendering/Renderer.h"
#include "Resource/ResourceCache.h"
#include "FileSystem/FileSystem.h"
#include "Threading/Threading.h"
#include "Input/Input.h"
#include "IconProvider.h"
//==================================

namespace ImGuiEx
{ 
	// Images & Image buttons
	inline bool ImageButton(Spartan::RHI_Texture* texture, const ImVec2& size)
	{
		return ImGui::ImageButton
		(
			static_cast<ImTextureID>(texture),
			size,
			ImVec2(0, 0),			// uv0
			ImVec2(1, 1),			// uv1
			-1,						// frame padding
			ImColor(0, 0, 0, 0),	// background
			ImVec4(1, 1, 1, 1)		// tint
		);
	}

	inline bool ImageButton(const Icon_Type icon, const float size)
	{
		return ImGui::ImageButton(
			static_cast<ImTextureID>(IconProvider::Get().GetTextureByType(icon)),
			ImVec2(size, size),
			ImVec2(0, 0),			// uv0
			ImVec2(1, 1),			// uv1
			-1,						// frame padding
			ImColor(0, 0, 0, 0),	// background
			ImVec4(1, 1, 1, 1)		// tint
		);
	}

	inline bool ImageButton(const char* id, const Icon_Type icon, const float size)
	{
		ImGui::PushID(id);
		const auto pressed = ImGui::ImageButton(
			static_cast<ImTextureID>(IconProvider::Get().GetTextureByType(icon)),
			ImVec2(size, size),
			ImVec2(0, 0),			// uv0
			ImVec2(1, 1),			// uv1
			-1,						// frame padding
			ImColor(0, 0, 0, 0),	// background
			ImVec4(1, 1, 1, 1)		// tint
		);
		ImGui::PopID();
		return pressed;
	}

	inline void Image(const Thumbnail& thumbnail, const float size)
	{
		ImGui::Image(
			static_cast<ImTextureID>(IconProvider::Get().GetTextureByThumbnail(thumbnail)),
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	inline void Image(Spartan::RHI_Texture* texture, const float size)
	{
		ImGui::Image(
			static_cast<ImTextureID>(texture),
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	inline void Image(Spartan::RHI_Texture* texture, const ImVec2& size, const ImColor& tint = ImColor(0, 0, 0, 0), const ImColor& border = ImColor(0, 0, 0, 0))
	{
		ImGui::Image(
			static_cast<ImTextureID>(texture),
			size,
			ImVec2(0, 0),
			ImVec2(1, 1),
			tint,
			border
		);
	}

	inline void Image(const Icon_Type icon, const float size)
	{
		ImGui::Image(
			static_cast<void*>(IconProvider::Get().GetTextureByType(icon)),
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	// Drag & Drop
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
	
	inline void CreateDragPayload(const DragDropPayload& payload)
	{
		ImGui::SetDragDropPayload(reinterpret_cast<const char*>(&payload.type), reinterpret_cast<const void*>(&payload), sizeof(payload), ImGuiCond_Once);
	}

	inline DragDropPayload* ReceiveDragPayload(DragPayloadType type)
	{
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload_imgui = ImGui::AcceptDragDropPayload(reinterpret_cast<const char*>(&type)))
			{
				return static_cast<DragDropPayload*>(payload_imgui->Data);
			}
			ImGui::EndDragDropTarget();
		}

		return nullptr;
	}
}

class EditorHelper
{
public:

	static EditorHelper& Get()
	{
		static EditorHelper instance;
		return instance;
	}

	void Initialize(Spartan::Context* context)
	{
		g_context			= context;
		g_resource_cache	= context->GetSubsystem<Spartan::ResourceCache>().get();
		g_world				= context->GetSubsystem<Spartan::World>().get();
		g_threading			= context->GetSubsystem<Spartan::Threading>().get();
		g_renderer			= context->GetSubsystem<Spartan::Renderer>().get();
		g_input				= context->GetSubsystem<Spartan::Input>().get();
	}

	void LoadModel(const std::string& file_path) const
	{
		auto resource_cache = g_resource_cache;

		// Load the model asynchronously
		g_threading->AddTask([resource_cache, file_path]()
		{
			resource_cache->Load<Spartan::Model>(file_path);
		});
	}

	void LoadScene(const std::string& file_path) const
	{
		auto world = g_world;

		// Load the scene asynchronously
		g_threading->AddTask([world, file_path]()
		{
			world->LoadFromFile(file_path);
		});
	}

	void SaveScene(const std::string& file_path) const
	{
		auto world = g_world;

		// Save the scene asynchronously
		g_threading->AddTask([world, file_path]()
		{
			world->SaveToFile(file_path);
		});
	}

	void PickEntity()
	{
		// Get camera
		auto camera = g_renderer->GetCamera();
		if (!camera)
			return;

		// Pick the world
		std::shared_ptr<Spartan::Entity> entity;
		camera->Pick(g_input->GetMousePosition(), entity);

		// Set the transform gizmo to the selected entity
		SetSelectedEntity(entity);

		// Fire callback
		g_on_entity_selected();
	}

	void SetSelectedEntity(const std::shared_ptr<Spartan::Entity>& entity)
	{
		// keep returned entity instead as the transform gizmo can decide to reject it
		g_selected_entity = g_renderer->SnapTransformGizmoTo(entity);
	}

	Spartan::Context*				g_context;
	Spartan::ResourceCache*			g_resource_cache;
	Spartan::World*					g_world;
	Spartan::Threading*				g_threading;
	Spartan::Renderer*				g_renderer;
	Spartan::Input*					g_input;
	std::weak_ptr<Spartan::Entity>	g_selected_entity;
	std::function<void()>			g_on_entity_selected;
};