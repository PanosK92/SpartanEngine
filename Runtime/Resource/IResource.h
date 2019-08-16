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
#include "../Core/Spartan_Object.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

namespace Spartan
{
	enum Resource_Type
	{
		Resource_Unknown,
		Resource_Texture,
		Resource_Texture2d,
		Resource_TextureCube,
		Resource_Audio,
		Resource_Material,	
		Resource_Mesh,
		Resource_Model,
		Resource_Cubemap,	
		Resource_Animation,
		Resource_Font,
		Resource_Shader
	};

	enum LoadState
	{
		LoadState_Idle,
		LoadState_Started,
		LoadState_Completed,
		LoadState_Failed
	};

	class SPARTAN_CLASS IResource : public Spartan_Object
	{
	public:
		IResource(Context* context, Resource_Type type);
		virtual ~IResource() = default;

		//= PROPERTIES =========================================================================================================================
		Resource_Type GetResourceType() const					{ return m_resource_type; }
		const char* GetResourceTypeCstr() const					{ return typeid(*this).name(); }
		const std::string& GetResourceName() const				{ return m_resource_name; }
		void SetResourceName(const std::string& name)			{ m_resource_name = name; }
		const std::string& GetResourceFilePath() const			{ return m_resource_file_path; }
		void SetResourceFilePath(const std::string& file_path)  { m_resource_file_path = file_path; }
		bool HasFilePath() const								{ return !m_resource_file_path.empty(); }
		std::string GetResourceFileName() const					{ return FileSystem::GetFileNameNoExtensionFromFilePath(m_resource_file_path); }
		std::string GetResourceDirectory() const				{ return FileSystem::GetDirectoryFromFilePath(m_resource_file_path); }
		virtual uint32_t GetMemoryUsage()						{ return static_cast<uint32_t>(sizeof(*this)); }
		LoadState GetLoadState() const							{ return m_load_state; }
		//======================================================================================================================================

		//= IO =================================================================
		virtual bool SaveToFile(const std::string& file_path)	{ return true; }
		virtual bool LoadFromFile(const std::string& file_path)	{ return true; }
		//======================================================================

		//= TYPE ===================================
		template <typename T>
		static constexpr Resource_Type TypeToEnum();
		//==========================================

	protected:
		Resource_Type m_resource_type	= Resource_Unknown;
		LoadState m_load_state			= LoadState_Idle;
		Context* m_context				= nullptr;

	private:
		std::string m_resource_name;
		std::string m_resource_file_path;
	};
}
