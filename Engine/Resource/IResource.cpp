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

//= INCLUDES =====================================
#include "IResource.h"
#include "../Rendering/Deferred/ShaderVariation.h"
#include "../Rendering/Animation.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/Mesh.h"
#include "../Rendering/Model.h"
#include "../Rendering/Font/Font.h"
//================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

template <typename T>
Resource_Type IResource::DeduceResourceType() { return Resource_Unknown; }
#define INSTANTIATE_ToResourceType(T, enumT) template<> ENGINE_CLASS Resource_Type IResource::DeduceResourceType<T>() { return enumT; }
// Explicit template instantiation
INSTANTIATE_ToResourceType(RHI_Texture,		Resource_Texture)
INSTANTIATE_ToResourceType(AudioClip,		Resource_Audio)
INSTANTIATE_ToResourceType(Material,		Resource_Material)
INSTANTIATE_ToResourceType(ShaderVariation, Resource_Shader)
INSTANTIATE_ToResourceType(Mesh,			Resource_Mesh)
INSTANTIATE_ToResourceType(Model,			Resource_Model)
INSTANTIATE_ToResourceType(Animation,		Resource_Animation)
INSTANTIATE_ToResourceType(Font,			Resource_Font)

IResource::IResource(Context* context, Resource_Type type)
{
	m_context			= context;
	m_resourceType		= type;
	m_resourceID		= GENERATE_GUID;
	m_loadState			= LoadState_Idle;
}