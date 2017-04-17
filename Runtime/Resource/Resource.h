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

namespace Directus
{
	namespace Resource
	{
		enum ResourceType
		{
			Unknown,
			Texture,
			Audio,
			Material,
			Shader,
			Mesh,
			Cubemap
		};

		class Resource
		{
		public:
			virtual ~Resource() {}

			const std::string& GetResourceID() { return m_resourceID; }
			void SetResourceID(const std::string& ID) { m_resourceID = ID; }

			ResourceType GetResourceType() { return m_resourceType; }
			void SetResourceType(ResourceType type) { m_resourceType = type; }

			const std::string& GetResourceName() { return m_resourceName; }
			void SetResourceName(const std::string& name) { m_resourceName = name; }

			const std::string& GetResourceFilePath() { return m_resourceFilePath; }
			void SetResourceFilePath(const std::string& filePath) { m_resourceFilePath = filePath; }

			// Resource Save/Load
			virtual bool LoadFromFile(const std::string& filePath) = 0;

			// Metadata Save/Load
			virtual bool SaveMetadata() = 0;

		protected:
			Context* m_context = nullptr;

			std::string m_resourceID = DATA_NOT_ASSIGNED;
			ResourceType m_resourceType = Unknown;
			std::string m_resourceName = DATA_NOT_ASSIGNED;
			std::string m_resourceFilePath = DATA_NOT_ASSIGNED;
		};
	}
}