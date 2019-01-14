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
#include <memory>
#include "../Core/Context.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

namespace Directus
{
	enum Resource_Type
	{
		Resource_Unknown,
		Resource_Texture,
		Resource_Audio,
		Resource_Material,	
		Resource_Mesh,
		Resource_Model,
		Resource_Cubemap,	
		Resource_Animation,
		Resource_Font,
		Resource_Shader, // not an actual resource, just a memory resource, enum is here just so we can get a standard path
		Resource_Script	 // not an actual resource, just a memory resource, enum is here just so we can get a standard path
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
		IResource(Context* context, Resource_Type type);
		virtual ~IResource() {}

		//= PROPERTIES ===================================================================================================
		unsigned int Resource_GetID() { return m_resourceID; }

		Resource_Type GetResourceType()				{ return m_resourceType; }
		void SetResourceType(Resource_Type type)	{ m_resourceType = type; }

		const char* GetResourceType_cstr() { return typeid(*this).name(); }

		const std::string& GetResourceName()			{ return m_resourceName; }
		void SetResourceName(const std::string& name)	{ m_resourceName = name; }

		const std::string& GetResourceFilePath()				{ return m_resourceFilePath; }
		void SetResourceFilePath(const std::string& filePath)	{ m_resourceFilePath = filePath; }

		bool HasFilePath() { return m_resourceFilePath != NOT_ASSIGNED; }

		std::string GetResourceFileName()	{ return FileSystem::GetFileNameNoExtensionFromFilePath(m_resourceFilePath); }
		std::string GetResourceDirectory()	{ return FileSystem::GetDirectoryFromFilePath(m_resourceFilePath); }
		//================================================================================================================

		//= IO =================================================================
		virtual bool SaveToFile(const std::string& filePath)	{ return true; }
		virtual bool LoadFromFile(const std::string& filePath)	{ return true; }
		virtual unsigned int GetMemoryUsage()					{ return 0; }
		//======================================================================

		//= TYPE ================================
		template <typename T>
		static Resource_Type DeduceResourceType();
		//=======================================

		//= PTR ==========================================
		auto GetSharedPtr() { return shared_from_this(); }
		//================================================

		LoadState GetLoadState()			{ return m_loadState; }
		void SetLoadState(LoadState state)	{ m_loadState = state; }

	protected:
		unsigned int m_resourceID			= NOT_ASSIGNED_HASH;
		std::string m_resourceName			= NOT_ASSIGNED;
		std::string m_resourceFilePath		= NOT_ASSIGNED;
		Resource_Type m_resourceType		= Resource_Unknown;
		LoadState m_loadState				= LoadState_Idle;
		Context* m_context					= nullptr;
	};
}
