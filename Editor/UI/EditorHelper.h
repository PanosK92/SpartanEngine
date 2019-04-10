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
	// An icon shader resource pointer by thumbnail
	#define SHADER_RESOURCE_BY_THUMBNAIL(thumbnail)	IconProvider::Get().GetShaderResourceByThumbnail(thumbnail)

	// An thumbnail button by enum
	inline bool ImageButton(const Icon_Type icon, const float size)
	{
		return ImGui::ImageButton(
			IconProvider::Get().GetShaderResourceByType(icon), 
			ImVec2(size, size),
			ImVec2(0, 0),			// uv0
			ImVec2(1, 1),			// uv1
			-1,						// frame padding
			ImColor(0, 0, 0, 0),	// background
			ImVec4(1, 1, 1, 1)		// tint
		);
	}

	// An thumbnail button by enum, with a specific ID
	inline bool ImageButton(const char* id, const Icon_Type icon, const float size)
	{
		ImGui::PushID(id);
		const auto pressed = ImGui::ImageButton(
			IconProvider::Get().GetShaderResourceByType(icon), 
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

	// A thumbnail image
	inline void Image(const Thumbnail& thumbnail, const float size)
	{
		ImGui::Image(
			IconProvider::Get().GetShaderResourceByThumbnail(thumbnail),
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	// A thumbnail image by shader resource
	inline void Image(void* shader_resource, const float size)
	{
		ImGui::Image(
			shader_resource,
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	// A thumbnail image by enum
	inline void Image(const Icon_Type icon, const float size)
	{
		ImGui::Image(
			IconProvider::Get().GetShaderResourceByType(icon),
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
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
		g_context		= context;
		g_resource_cache	= context->GetSubsystem<Spartan::ResourceCache>().get();
		g_world			= context->GetSubsystem<Spartan::World>().get();
		g_threading		= context->GetSubsystem<Spartan::Threading>().get();
		g_renderer		= context->GetSubsystem<Spartan::Renderer>().get();
		g_input			= context->GetSubsystem<Spartan::Input>().get();
	}

	std::shared_ptr<Spartan::RHI_Texture> GetOrLoadTexture(const std::string& file_path, const bool async = false)
	{
		if (Spartan::FileSystem::IsDirectory(file_path))
			return nullptr;

		if (!Spartan::FileSystem::IsSupportedImageFile(file_path) && !Spartan::FileSystem::IsEngineTextureFile(file_path))
			return nullptr;

		// Compute some useful information
		const auto path = Spartan::FileSystem::GetRelativeFilePath(file_path);
		const auto name = Spartan::FileSystem::GetFileNameNoExtensionFromFilePath(path);

		// Check if this texture is already cached, if so return the cached one
		if (auto cached = g_resource_cache->GetByName<Spartan::RHI_Texture>(name))
			return cached;

		// Since the texture is not cached, load it and returned a cached ref
		auto texture = std::make_shared<Spartan::RHI_Texture>(g_context);
		texture->SetResourceName(name);
		texture->SetResourceFilePath(path);
		if (!async)
		{
			texture->LoadFromFile(path);
		}
		else
		{
			g_threading->AddTask([texture, file_path]()
			{
				texture->LoadFromFile(file_path);
			});
		}

		return texture;
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

		// Set the transform gizmo to the selected entity and keep returned entity instead (gizmo can decide to reject)
		g_selected_entity = g_renderer->SnapTransformGizmoTo(entity);

		// Fire callback
		g_on_entity_selected();
	}

	Spartan::Context*				g_context;
	Spartan::ResourceCache*		g_resource_cache;
	Spartan::World*				g_world;
	Spartan::Threading*			g_threading;
	Spartan::Renderer*				g_renderer;
	Spartan::Input*				g_input;
	std::weak_ptr<Spartan::Entity> g_selected_entity;
	std::function<void()>			g_on_entity_selected;
};