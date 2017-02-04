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

//= INCLUDES =================
#include <memory>
#include "../Core/SubSystem.h"
#include "ResourceCache.h"
//============================

class ResourceManager : public Subsystem
{
public:
	ResourceManager(Context* context);
	~ResourceManager() { Unload(); }

	// Unloads all resources
	void Unload() { m_resourceCache->Clear(); }

	template <class T>
	std::weak_ptr<T> Load(const std::string& filePath)
	{
		return m_resourceCache->Load<T>(filePath);
	}

	template <class T>
	std::weak_ptr<T> Add(std::shared_ptr<T> resource)
	{
		return m_resourceCache->Add<T>(resource);
	}

	template <class T>
	std::weak_ptr<T> GetResourceByID(const std::string& ID)
	{
		return m_resourceCache->GetByID<T>(ID);
	}

	template <class T>
	std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
	{
		return m_resourceCache->GetByPath<T>(filePath);
	}

	template <class T>
	std::vector<std::weak_ptr<T>> GetAllByType()
	{
		return m_resourceCache->GetAllByType<T>();
	}

	//= TEMPORARY =======================================
	void NormalizeModelScale(GameObject* rootGameObject);
	//===================================================

private:
	std::unique_ptr<Directus::Resource::ResourceCache> m_resourceCache;

	//= TEMPORARY =================================================================================================
	std::vector<std::weak_ptr<Mesh>> GetModelMeshesByModelName(const std::string& rootGameObjectID);
	float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
	void SetModelScale(const std::string& rootGameObjectID, float scale);
	static std::weak_ptr<Mesh> GetLargestBoundingBox(const std::vector<std::weak_ptr<Mesh>>& meshes);
	//=============================================================================================================
};
