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
#include "Math/Vector4.h"
#include "Math/Vector2.h"
#include "RHI/RHI_Implementation.h"
#include "RHI/RHI_Texture.h"
#include "World/World.h"
#include "World/Components/Camera.h"
#include "Rendering/Renderer.h"
#include "Resource/ResourceCache.h"
#include "FileSystem/FileSystem.h"
#include "Threading/Threading.h"
#include "Input/Input.h"
#include "Core/Engine.h"
#include "IconProvider.h"
//==================================

namespace ImGuiEx
{ 
	// An icon shader resource pointer by thumbnail
	#define SHADER_RESOURCE_BY_THUMBNAIL(thumbnail)	IconProvider::Get().GetShaderResourceByThumbnail(thumbnail)

	// An thumbnail button by enum
	inline bool ImageButton(Icon_Type icon, float size)
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
	inline bool ImageButton(const char* id, Icon_Type icon, float size)
	{
		ImGui::PushID(id);
		bool pressed = ImGui::ImageButton(
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
	inline void Image(const Thumbnail& thumbnail, float size)
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
	inline void Image(void* shaderResource, float size)
	{
		ImGui::Image(
			shaderResource,
			ImVec2(size, size),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImColor(0, 0, 0, 0),	// tint
			ImColor(0, 0, 0, 0)		// border
		);
	}

	// A thumbnail image by enum
	inline void Image(Icon_Type icon, float size)
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

	void Initialize(Directus::Context* context)
	{
		g_context		= context;
		g_resourceCache	= context->GetSubsystem<Directus::ResourceCache>().get();
		g_world			= context->GetSubsystem<Directus::World>().get();
		g_threading		= context->GetSubsystem<Directus::Threading>().get();
		g_renderer		= context->GetSubsystem<Directus::Renderer>().get();
		g_input			= context->GetSubsystem<Directus::Input>().get();
	}

	std::shared_ptr<Directus::RHI_Texture> GetOrLoadTexture(const std::string& filePath, bool async = false)
	{
		if (Directus::FileSystem::IsDirectory(filePath))
			return nullptr;

		if (!Directus::FileSystem::IsSupportedImageFile(filePath) && !Directus::FileSystem::IsEngineTextureFile(filePath))
			return nullptr;

		// Compute some useful information
		auto path = Directus::FileSystem::GetRelativeFilePath(filePath);
		auto name = Directus::FileSystem::GetFileNameNoExtensionFromFilePath(path);

		// Check if this texture is already cached, if so return the cached one
		if (auto cached = g_resourceCache->GetByName<Directus::RHI_Texture>(name))
			return cached;

		// Since the texture is not cached, load it and returned a cached ref
		auto texture = std::make_shared<Directus::RHI_Texture>(g_context);
		texture->SetResourceName(name);
		texture->SetResourceFilePath(path);
		if (!async)
		{
			texture->LoadFromFile(path);
		}
		else
		{
			g_threading->AddTask([texture, filePath]()
			{
				texture->LoadFromFile(filePath);
			});
		}

		return texture;
	}

	void LoadModel(const std::string& filePath)
	{
		auto _resourceCache = g_resourceCache;

		// Load the model asynchronously
		g_threading->AddTask([_resourceCache, filePath]()
		{
			_resourceCache->Load<Directus::Model>(filePath);
		});
	}

	void LoadScene(const std::string& filePath)
	{
		auto _world = g_world;

		// Load the scene asynchronously
		g_threading->AddTask([_world, filePath]()
		{
			_world->LoadFromFile(filePath);
		});
	}

	void SaveScene(const std::string& filePath)
	{
		auto _world = g_world;

		// Save the scene asynchronously
		g_threading->AddTask([_world, filePath]()
		{
			_world->SaveToFile(filePath);
		});
	}

	void PickEntity()
	{
		// Get camera
		auto camera = g_renderer->GetCamera();
		if (!camera)
			return;

		// Pick the world
		std::shared_ptr<Directus::Entity> entity;
		camera->Pick(g_input->GetMousePosition(), entity);

		// Set the transform gizmo to the selected entity and keep returned entity instead (gizmo can decide to reject)
		g_selectedEntity = g_renderer->SnapTransformGizmoTo(entity);

		// Fire callback
		g_onEntitySelected();
	}

	Directus::Context*				g_context;
	Directus::ResourceCache*		g_resourceCache;
	Directus::World*				g_world;
	Directus::Threading*			g_threading;
	Directus::Renderer*				g_renderer;
	Directus::Input*				g_input;
	std::weak_ptr<Directus::Entity> g_selectedEntity;
	std::function<void()>			g_onEntitySelected;
};