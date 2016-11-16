/*
Copyright(c) 2016 Panos Karabelas

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
#include <vector>
#include <memory>
#include "IResource.h"
#include "../Core/Subsystem.h"
#include "../FileSystem/FileSystem.h"
#include "../Graphics/Texture.h"
#include "../Core/Context.h"
//===================================

namespace Directus
{
	namespace Resource
	{
		// dynamic_pointer_cast complains
		// so I made it shut it up like this.
		template<int>
		void dynamic_pointer_cast();

		class ResourceCache : public Subsystem
		{
		public:
			ResourceCache(Context* context);
			~ResourceCache();

			// Releases all the resources
			void Clear()
			{
				m_resources.clear();
				m_resources.shrink_to_fit();
			}

			// Loads a resource given the file path
			template <class T>
			std::weak_ptr<T> LoadResource(const std::string& filePath)
			{
				// Check if the resource is already loaded
				for (auto const& resource : m_resources)
					if (resource->GetFilePath() == filePath)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				std::shared_ptr<T> resource;
				// Texture
				if (FileSystem::IsSupportedImageFile(filePath))
				{
					resource = std::make_shared<Texture>();
					resource->LoadFromFile(filePath, g_context->GetSubsystem<Graphics>());
					m_resources.push_back(resource);
					return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
				}
				
				return std::weak_ptr<T>();
			}

			// Loads resources given the file paths from a vector
			void LoadResources(const std::vector<std::string>& filePaths)
			{
				for (const auto& filePath : filePaths)
					LoadResource<Texture>(filePath);
			}

			// Returns the file paths of all the resources
			std::vector<std::string> GetResourceFilePaths()
			{
				std::vector<std::string> filePaths;
				for (auto const& resource : m_resources)
					filePaths.push_back(resource->GetFilePath());

				return filePaths;
			}

			// Returns a resource by ID
			template <class T>
			std::weak_ptr<T> GetResourceByID(const std::string& ID)
			{
				for (auto const& resource : m_resources)
					if (resource->GetID() == ID)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				return std::weak_ptr<T>();
			}

			// Returns a resource by file path
			template <class T>
			std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
			{
				for (auto const& resource : m_resources)
					if (resource->GetFilePath() == filePath)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				return std::weak_ptr<T>();
			}

			void SaveResourceMetadata()
			{
				for (auto const& resource : m_resources)
					resource->SaveMetadata();
			}

		private:
			std::vector<std::shared_ptr<IResource>> m_resources;
		};
	}
}