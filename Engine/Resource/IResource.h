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
		virtual ~IResource() = default;

		//= PROPERTIES =======================================================================================================================
		unsigned int ResourceGetId() const						{ return m_resource_id; }
		Resource_Type GetResourceType() const					{ return m_resource_type; }
		void SetResourceType(Resource_Type type)				{ m_resource_type = type; }
		const char* GetResourceTypeCstr() const					{ return typeid(*this).name(); }
		const std::string& GetResourceName() const				{ return m_resource_name; }
		void SetResourceName(const std::string& name)			{ m_resource_name = name; }
		const std::string& GetResourceFilePath() const			{ return m_resource_file_path; }
		void SetResourceFilePath(const std::string& file_path)	{ m_resource_file_path = file_path; }
		bool HasFilePath() const								{ return m_resource_file_path != NOT_ASSIGNED; }
		std::string GetResourceFileName() const					{ return FileSystem::GetFileNameNoExtensionFromFilePath(m_resource_file_path); }
		std::string GetResourceDirectory() const				{ return FileSystem::GetDirectoryFromFilePath(m_resource_file_path); }
		virtual unsigned int GetMemoryUsage()					{ return static_cast<unsigned int>(sizeof(*this)); }
		LoadState GetLoadState() const				{ return m_load_state; }
		void SetLoadState(const LoadState state)	{ m_load_state = state; }
		//====================================================================================================================================

		//= IO =================================================================
		virtual bool SaveToFile(const std::string& file_path)	{ return true; }
		virtual bool LoadFromFile(const std::string& file_path)	{ return true; }
		//======================================================================

		//= TYPE ===================================
		template <typename T>
		static constexpr Resource_Type TypeToEnum();
		//==========================================

		//= PTR ==========================================
		auto GetSharedPtr() { return shared_from_this(); }
		//================================================

	protected:
		unsigned int m_resource_id			= NOT_ASSIGNED_HASH;
		std::string m_resource_name			= NOT_ASSIGNED;
		std::string m_resource_file_path	= NOT_ASSIGNED;
		Resource_Type m_resource_type		= Resource_Unknown;
		LoadState m_load_state				= LoadState_Idle;
		Context* m_context					= nullptr;
	};
}
