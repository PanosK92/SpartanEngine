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

//= INCLUDES ========================
#include "../Core/Context.h"
#include "../FileSystem/FileSystem.h"
//===================================

#define RESOURCE_SAVE "SaveToExisting"

namespace Directus
{
	enum ResourceType
	{
		Unknown_Resource,
		Texture_Resource,
		Audio_Resource,
		Material_Resource,
		Shader_Resource,
		Model_Resource,
		Cubemap_Resource,
		Script_Resource
	};

	class DLL_API Resource
	{
	public:
		virtual ~Resource() {}

		std::string& GetResourceID() { return m_resourceID; }
		void SetResourceID(const std::string& ID) { m_resourceID = ID; }

		ResourceType GetResourceType() { return m_resourceType; }
		void SetResourceType(ResourceType type) { m_resourceType = type; }

		std::string& GetResourceName() { return m_resourceName; }
		void SetResourceName(const std::string& name) { m_resourceName = name; }

		std::string& GetResourceFilePath() { return m_resourceFilePath; }
		void SetResourceFilePath(const std::string& filePath) { m_resourceFilePath = filePath; }

		std::string GetResourceFileName() { return FileSystem::GetFileNameNoExtensionFromFilePath(m_resourceFilePath); }
		std::string GetResourceDirectory() { return FileSystem::GetDirectoryFromFilePath(m_resourceFilePath); }

		// Resource Save/Load
		virtual bool SaveToFile(const std::string& filePath) = 0;
		virtual bool LoadFromFile(const std::string& filePath) = 0;

	protected:
		std::string m_resourceID = DATA_NOT_ASSIGNED;	
		std::string m_resourceName = DATA_NOT_ASSIGNED;
		std::string m_resourceFilePath = DATA_NOT_ASSIGNED;
		ResourceType m_resourceType = Unknown_Resource;
		Context* m_context = nullptr;
	};
}