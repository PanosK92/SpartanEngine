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

//= INCLUDES ==================
#include "ResourceManager.h"
#include "../Core/GameObject.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ResourceManager::ResourceManager(Context* context) : Subsystem(context)
	{
		m_resourceCache = nullptr;
		m_modelImporter = nullptr;
	}

	bool ResourceManager::Initialize()
	{
		// Cache
		m_resourceCache = make_unique<ResourceCache>();

		// Importers
		m_imageImporter = make_shared<ImageImporter>();
		m_modelImporter = make_shared<ModelImporter>();
		m_modelImporter->Initialize(m_context);
		
		// Add engine standard resource directories
		AddResourceDirectory(Texture_Resource, "Standard Assets//Textures//");
		AddResourceDirectory(Material_Resource, "Standard Assets//Fonts//");
		AddResourceDirectory(Shader_Resource, "Standard Assets//Shaders//");
		AddResourceDirectory(Cubemap_Resource, "Standard Assets//Cubemaps//");
		AddResourceDirectory(Script_Resource, "Standard Assets//Scripts//");
		AddResourceDirectory(Model_Resource, "Standard Assets//Models//");
		AddResourceDirectory(Material_Resource, "Standard Assets//Materials//");

		return true;
	}

	void ResourceManager::AddResourceDirectory(ResourceType type, const string& directory)
	{
		m_resourceDirectories[type] = directory;
	}

	string ResourceManager::GetResourceDirectory(ResourceType type)
	{
		for (auto& directory : m_resourceDirectories)
		{
			if (directory.first == type)
				return directory.second;
		}

		return DATA_NOT_ASSIGNED;
	}
}