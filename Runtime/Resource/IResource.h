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
#include "../Core/Context.h"
#include "../FileSystem/FileSystem.h"
#include "../Core/GUIDGenerator.h"
#include <memory>
//===================================

namespace Directus
{
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

	enum AsyncState
	{
		Async_Idle,
		Async_Started,
		Async_Completed,
		Async_Failed
	};

	class ENGINE_CLASS IResource : public std::enable_shared_from_this<IResource>
	{
	public:
		IResource(Context* context)
		{
			m_context = context;
		}
		virtual ~IResource() {}

		template <typename T>
		void RegisterResource();

		//= PROPERTIES =========================================================================
		unsigned int GetResourceID() { return m_resourceID; }
	
		ResourceType GetResourceType() { return m_resourceType; }
		void SetResourceType(ResourceType type) { m_resourceType = type; }

		const std::string& GetResourceName() { return m_resourceName; }
		void SetResourceName(const std::string& name) { m_resourceName = name; }

		const std::string& GetResourceFilePath() { return m_resourceFilePath; }
		void SetResourceFilePath(const std::string& filePath) { m_resourceFilePath = filePath; }

		bool HasFilePath() { return m_resourceFilePath != NOT_ASSIGNED;}

		std::string GetResourceFileName();
		std::string GetResourceDirectory();
		//======================================================================================

		//= CACHE ================================================================
		// Checks whether the resource is cached or not
		template <typename T>
		bool IsCached();

		// Adds the resource into the resource cache and returns a cache reference
		// In case the resource is already cached, it returns the existing one
		template <typename T>
		std::weak_ptr<T> Cache();
		//========================================================================

		//= IO ================================================================
		virtual bool SaveToFile(const std::string& filePath) { return true; }
		virtual bool LoadFromFile(const std::string& filePath) { return true; }
		virtual unsigned int GetMemory() { return 0; }
		//=====================================================================

		//= TYPE ================================
		std::string GetResourceTypeStr();

		ResourceType DeduceResourceType();
		template <typename T>
		static ResourceType DeduceResourceType();
		//=======================================

		//= PTR ==========================================
		auto GetSharedPtr() { return shared_from_this(); }
		//================================================

		AsyncState GetAsyncState() { return m_asyncState; }
		void SetAsyncState(AsyncState state) { m_asyncState = state; }

	protected:	
		unsigned int m_resourceID		= NOT_ASSIGNED_HASH;
		std::string m_resourceName		= NOT_ASSIGNED;
		std::string m_resourceFilePath	= NOT_ASSIGNED;
		ResourceType m_resourceType		= Resource_Unknown;
		AsyncState m_asyncState			= Async_Idle;
		Context* m_context				= nullptr;
	};
}
