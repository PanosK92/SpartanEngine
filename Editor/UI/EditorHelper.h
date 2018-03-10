/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../ImGui/imgui.h"
#include "Math/Vector4.h"
#include "Math/Vector2.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceManager.h"
#include "Core/Engine.h"
#include "Core/EventSystem.h"
#include "FileSystem/FileSystem.h"
#include "Threading/Threading.h"
#include "ProgressDialog.h"
#include "Scene/Scene.h"
//===================================

static const int BUFFER_TEXT_DEFAULT = 255;

// An icon shader resource pointer by thumbnail id
#define SHADER_RESOURCE(thumbnail)						ThumbnailProvider::Get().GetShaderResourceByThumbnail(thumbnail)
// An icon shader resource pointer by type 
#define SHADER_RESOURCE_BY_TYPE(type)					ThumbnailProvider::Get().GetShaderResourceByType(type)
// An thumbnail button by thumbnail
#define THUMBNAIL_BUTTON(thumbnail, size)				ImGui::ImageButton(SHADER_RESOURCE(thumbnail), ImVec2(size, size))
// An thumbnail button by enum
#define THUMBNAIL_BUTTON_BY_TYPE(type, size)			ImGui::ImageButton(SHADER_RESOURCE_BY_TYPE(type), ImVec2(size, size))
// An thumbnail button by enum, with a specific ID
#define THUMBNAIL_BUTTON_TYPE_UNIQUE_ID(id, type, size)	ThumbnailProvider::Get().ImageButton_enum_id(id, type, size)

// A thumbnail image
#define THUMBNAIL_IMAGE(thumbnail, size)								\
	ImGui::Image(														\
	ThumbnailProvider::Get().GetShaderResourceByThumbnail(thumbnail),	\
	ImVec2(size, size),													\
	ImVec2(0, 0),														\
	ImVec2(1, 1),														\
	ImColor(255, 255, 255, 255),										\
	ImColor(255, 255, 255, 0))											\

// A thumbnail image by shader resource
#define THUMBNAIL_IMAGE_BY_SHADER_RESOURCE(srv, size)	\
	ImGui::Image(										\
	srv,												\
	ImVec2(size, size),									\
	ImVec2(0, 0),										\
	ImVec2(1, 1),										\
	ImColor(255, 255, 255, 255),						\
	ImColor(255, 255, 255, 0))							\

// A thumbnail image by enum
#define THUMBNAIL_IMAGE_BY_ENUM(type, size)	\
	ImGui::Image(							\
	SHADER_RESOURCE_BY_TYPE(type),			\
	ImVec2(size, size),						\
	ImVec2(0, 0),							\
	ImVec2(1, 1),							\
	ImColor(255, 255, 255, 255),			\
	ImColor(255, 255, 255, 0))				\

class EditorHelper
{
public:
	EditorHelper()
	{
		m_context			= nullptr;	
		m_engine			= nullptr;
		m_resourceManager	= nullptr;
		m_scene				= nullptr;
		m_isLoading			= false;

		SUBSCRIBE_TO_EVENT(EVENT_MODEL_LOADED,	EVENT_HANDLER(ProgressDialog_Hide));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_SAVED,	EVENT_HANDLER(ProgressDialog_Hide));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_LOADED,	EVENT_HANDLER(ProgressDialog_Hide));
	}
	~EditorHelper(){}

	static EditorHelper& Get()
	{
		static EditorHelper instance;
		return instance;
	}

	void Initialize(Directus::Context* context)
	{
		m_context			= context;
		m_engine			= m_context->GetSubsystem<Directus::Engine>();
		m_resourceManager	= m_context->GetSubsystem<Directus::ResourceManager>();
		m_scene				= m_context->GetSubsystem<Directus::Scene>();
	}

	void Update()
	{
		if (m_showProgressDialog)
		{
			ProgressDialog_Show();
		}
	}

	std::weak_ptr<Directus::Texture> GetOrLoadTexture(const std::string& filePath, bool async = false)
	{
		// Validate file path
		if (Directus::FileSystem::IsDirectory(filePath))
			return std::weak_ptr<Directus::Texture>();
		if (!Directus::FileSystem::IsSupportedImageFile(filePath) && !Directus::FileSystem::IsEngineTextureFile(filePath))
			return std::weak_ptr<Directus::Texture>();

		// Compute some useful information
		auto path = Directus::FileSystem::GetRelativeFilePath(filePath);
		auto name = Directus::FileSystem::GetFileNameNoExtensionFromFilePath(path);

		// Check if this texture is already cached, if so return the cached one
		auto resourceManager = m_context->GetSubsystem<Directus::ResourceManager>();	
		if (auto cached = resourceManager->GetResourceByName<Directus::Texture>(name).lock())
		{			
			return cached;
		}

		// Since the texture is not cached, load it and returned a cached ref
		auto texture = std::make_shared<Directus::Texture>(m_context);
		texture->SetResourceName(name);
		texture->SetResourceFilePath(path);
		if (!async)
		{
			texture->LoadFromFile(path);
		}
		else
		{
			m_context->GetSubsystem<Directus::Threading>()->AddTask([texture, filePath]()
			{
				texture->LoadFromFile(filePath);
			});
		}

		return texture->Cache<Directus::Texture>();
	}

	void LoadModel(const std::string& filePath)
	{
		// Make the engine stop
		SetEngineUpdate(false);
		SetEngineLoading(true);
		
		// Load the model asynchronously
		m_context->GetSubsystem<Directus::Threading>()->AddTask([this, filePath]()
		{
			m_resourceManager->Load<Directus::Model>(filePath);
		});
		m_showProgressDialog = true;
		m_isLoadingModel = true;
	}

	void LoadScene(const std::string& filePath)
	{
		// Make the engine stop
		SetEngineUpdate(false);
		SetEngineLoading(true);

		// Load the scene asynchronously
		m_context->GetSubsystem<Directus::Threading>()->AddTask([this, filePath]()
		{
			m_scene->LoadFromFile(filePath);
		});		
		m_showProgressDialog = true;
		m_isLoadingModel = false;
	}

	void SaveScene(const std::string& filePath)
	{
		m_context->GetSubsystem<Directus::Threading>()->AddTask([this, filePath]()
		{
			m_scene->SaveToFile(filePath);
		});		
		m_showProgressDialog = true;
		m_isLoadingModel = false;
	}

	// Whether the engine should update & render or not
	void SetEngineUpdate(bool update)
	{
		auto flags = m_engine->EngineMode_GetAll();
		flags = update ? flags | Directus::Engine_Update : flags & ~Directus::Engine_Update;
		flags = update ? flags | Directus::Engine_Render : flags & ~Directus::Engine_Render;
		m_engine->EngineMode_SetAll(flags);
	}
	// LOADING (Whether any editor system caused the engine to load something
	void SetEngineLoading(bool loading) { m_isLoading = loading; }
	bool GetEngineLoading() { return m_isLoading; }

	static void SetCharArray(char* array, const std::string& value)
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
	static void SetCharArray(char* array, T value) { SetCharArray(array, std::to_string(value)); }

	//= CONVERSIONS ===================================================================================================
	static ImVec4 ToImVec4(const Directus::Math::Vector4& v)	{ return ImVec4(v.x, v.y, v.z, v.w); }
	static Directus::Math::Vector4 ToVector4(const ImVec4& v)	{ return Directus::Math::Vector4(v.x, v.y, v.z, v.w); }
	static ImVec2 ToImVec2(const Directus::Math::Vector2& v)	{ return ImVec2{ v.x,v.y }; }
	static Directus::Math::Vector2 ToVector2(const ImVec2& v)	{ return Directus::Math::Vector2{ v.x,v.y }; }
	//=================================================================================================================

private:
	void ProgressDialog_Show()
	{
		// Tick	
		ProgressDialog::Get().SetIsVisible(m_showProgressDialog);
		ProgressDialog::Get().Update();

		// Show progress
		if (m_isLoadingModel)
		{
			auto importer = m_resourceManager->GetModelImporter().lock();
			ProgressDialog::Get().SetProgress(importer->GetProgress());
			ProgressDialog::Get().SetProgressStatus(importer->GetProgressStatus());
		}
		else
		{
			ProgressDialog::Get().SetProgress(m_scene->GetProgress());
			ProgressDialog::Get().SetProgressStatus(m_scene->GetProgressStatus());
		}
	}

	void ProgressDialog_Hide()
	{
		m_showProgressDialog = false;
		ProgressDialog::Get().SetIsVisible(false);
		SetEngineUpdate(true);
		SetEngineLoading(false);
	}

	Directus::Context* m_context;
	Directus::Engine* m_engine;
	Directus::ResourceManager* m_resourceManager;
	Directus::Scene* m_scene;
	bool m_isLoading;
	bool m_showProgressDialog = false;
	bool m_isLoadingModel = false;
};