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

//= INCLUDES =====================================
#include "../Resource/IResource.h"
#include "ResourceManager.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/RI/RI_Texture.h"
#include "../Rendering/Font.h"
#include "../Rendering/Animation.h"
#include "../Rendering/Model.h"
#include "../Rendering/Material.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Deferred/ShaderVariation.h"
//================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

template <typename T>
ResourceType IResource::DeduceResourceType() { return Resource_Unknown; }
// Explicit template instantiation
#define INSTANTIATE_ToResourceType(T, enumT) template<> ENGINE_CLASS ResourceType IResource::DeduceResourceType<T>() { return enumT; }
INSTANTIATE_ToResourceType(RI_Texture,			Resource_Texture)
INSTANTIATE_ToResourceType(AudioClip,		Resource_Audio)
INSTANTIATE_ToResourceType(Material,		Resource_Material)
INSTANTIATE_ToResourceType(ShaderVariation, Resource_Shader)
INSTANTIATE_ToResourceType(Mesh,			Resource_Mesh)
INSTANTIATE_ToResourceType(Model,			Resource_Model)
INSTANTIATE_ToResourceType(Animation,		Resource_Animation)
INSTANTIATE_ToResourceType(Font,			Resource_Font)

IResource::IResource(Context* context)
{
	m_context			= context;
	m_resourceManager	= m_context->GetSubsystem<ResourceManager>();	
}

std::weak_ptr<IResource> IResource::_Cache()
{
	auto resource = m_resourceManager->GetResourceByName(GetResourceName(), m_resourceType);
	if (resource.expired())
	{
		m_resourceManager->Add(GetSharedPtr());
		resource = m_resourceManager->GetResourceByName(GetResourceName(), m_resourceType);
	}

	return resource;
}

bool IResource::_IsCached()
{
	return m_resourceManager->ExistsByName(GetResourceName(), m_resourceType);
}
