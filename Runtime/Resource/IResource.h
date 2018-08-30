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
#include <memory>
#include "../Core/Context.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

namespace Directus
{
	class ResourceManager;
	
	enum ResourceType
	{
		Resource_Unknown,
		Resource_Texture,
		Resource_Audio,
		Resource_Material,
		Resource_Shader,
		Resource_Mesh,
		Resource_Model,
		Resource_Cubemap,
		Resource_Script, // not an actual resource, resource manager simply uses this to return standard resource path (must remove)
		Resource_Animation,
		Resource_Font
	};

	enum LoadState
	{
		LoadState_Idle,
		LoadState_Started,
		LoadState_Completed,
		LoadState_Failed
	};

	class ENGINE_CLASS IResource : public std::enable_shared_from_this<IResource>
	{
	public:
		IResource(Context* context);
		virtual ~IResource() {}

		template <typename T>
		void RegisterResource()
		{
			m_resourceType	= DeduceResourceType<T>();
			m_resourceID	= GENERATE_GUID;
			m_loadState		= LoadState_Idle;
		}

		//= PROPERTIES ===================================================================================================
		unsigned int GetResourceID() { return m_resourceID; }

		ResourceType GetResourceType()				{ return m_resourceType; }
		void SetResourceType(ResourceType type)		{ m_resourceType = type; }

		const char* GetResourceType_cstr() { return typeid(*this).name(); }

		const std::string& GetResourceName()			{ return m_resourceName; }
		void SetResourceName(const std::string& name)	{ m_resourceName = name; }

		const std::string& GetResourceFilePath()				{ return m_resourceFilePath; }
		void SetResourceFilePath(const std::string& filePath)	{ m_resourceFilePath = filePath; }

		bool HasFilePath() { return m_resourceFilePath != NOT_ASSIGNED; }

		std::string GetResourceFileName()	{ return FileSystem::GetFileNameNoExtensionFromFilePath(m_resourceFilePath); }
		std::string GetResourceDirectory()	{ return FileSystem::GetDirectoryFromFilePath(m_resourceFilePath); }
		//================================================================================================================

		//= CACHE =========================================================
		// Checks whether this resource is cached or not
		bool IsCached()
		{
			if (!m_context)
			{
				LOG_ERROR(std::string(GetResourceType_cstr()) + "::IsCached(): Context is null, can't execute function");
				return false;
			}

			return _IsCached();
		}

		// Caches the resource (if not cached) and returns a weak reference
		template <typename T>
		std::weak_ptr<T> Cache()
		{
			if (!m_context)
			{
				LOG_ERROR(std::string(GetResourceType_cstr()) + "::Cache(): Context is null, can't execute function");
				return std::weak_ptr<T>();
			}

			auto base = _Cache().lock();
			std::shared_ptr<T> derivedShared = std::static_pointer_cast<T>(base);
			std::weak_ptr<T> derivedWeak = std::weak_ptr<T>(derivedShared);

			return derivedWeak;
		}
		//=================================================================

		//= IO =================================================================
		virtual bool SaveToFile(const std::string& filePath)	{ return true; }
		virtual bool LoadFromFile(const std::string& filePath)	{ return true; }
		virtual unsigned int GetMemoryUsage()					{ return 0; }
		//======================================================================

		//= TYPE ================================
		template <typename T>
		static ResourceType DeduceResourceType();
		//=======================================

		//= PTR ==========================================
		auto GetSharedPtr() { return shared_from_this(); }
		//================================================

		LoadState GetLoadState()			{ return m_loadState; }
		void GetLoadState(LoadState state)	{ m_loadState = state; }

	protected:
		std::weak_ptr<IResource> _Cache();
		bool _IsCached();

		unsigned int m_resourceID			= NOT_ASSIGNED_HASH;
		std::string m_resourceName			= NOT_ASSIGNED;
		std::string m_resourceFilePath		= NOT_ASSIGNED;
		ResourceType m_resourceType			= Resource_Unknown;
		LoadState m_loadState				= LoadState_Idle;
		Context* m_context					= nullptr;
		ResourceManager* m_resourceManager	= nullptr;
	};
}
