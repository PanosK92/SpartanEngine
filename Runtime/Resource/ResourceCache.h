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

//= INCLUDES =========
#include <vector>
#include <memory>
#include "Resource.h"
//====================

namespace Directus
{
	namespace Resource
	{
		class ResourceCache
		{
		public:
			ResourceCache() {}
			~ResourceCache() { Unload(); }

			// Unloads all resources
			void Unload()
			{
				m_resources.clear();
				m_resources.shrink_to_fit();
			}

			// Adds a resource
			void Add(std::shared_ptr<Resource> resource)
			{
				if (!resource)
					return;

				m_resources.push_back(resource);
			}

			// Returns the file paths of all the resources
			std::vector<std::string> GetResourceFilePaths()
			{
				std::vector<std::string> filePaths;
				for (const auto& resource : m_resources)
					filePaths.push_back(resource->GetResourceFilePath());

				return filePaths;
			}

			// Returns a resource by ID
			std::shared_ptr<Resource> GetByID(const std::string& ID)
			{
				for (const auto& resource : m_resources)
					if (resource->GetResourceID() == ID)
						return resource;

				return std::shared_ptr<Resource>();
			}

			// Returns a resource by file path
			std::shared_ptr<Resource> GetByPath(const std::string& filePath)
			{
				for (const auto& resource : m_resources)
					if (resource->GetResourceFilePath() == filePath)
						return resource;

				return std::shared_ptr<Resource>();
			}

			// Makes the resources save their metadata
			void SaveResourceMetadata()
			{
				for (const auto& resource : m_resources)
					resource->SaveMetadata();
			}

			// Returns all the resources
			std::vector<std::shared_ptr<Resource>> GetAll()
			{
				return m_resources;
			}

			// Checks whether a resource is already in the cache
			bool Cached(const std::string& filePath)
			{
				if (filePath.empty())
					return false;

				for (const auto& resource : m_resources)
					if (resource->GetResourceFilePath() == filePath)
						return true;

				return false;
			}

			// Checks whether a resource is already in the cache
			bool Cached(std::shared_ptr<Resource> resourceIn)
			{
				if (!resourceIn)
					return false;

				for (const auto& resource : m_resources)
					if (resource->GetResourceID() == resourceIn->GetResourceID())
						return true;

				return false;
			}

		private:
			std::vector<std::shared_ptr<Resource>> m_resources;
		};
	}
}