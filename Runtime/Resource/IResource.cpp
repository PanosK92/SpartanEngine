/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ============================
#include "Spartan.h"
#include "IResource.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/Model.h"
#include "../Rendering/Font/Font.h"
#include "../Rendering/Animation.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_TextureCube.h"
//=======================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

IResource::IResource(Context* context, const ResourceType type)
{
    m_context        = context;
    m_resource_type    = type;
    m_load_state    = Idle;
}

template <typename T>
inline constexpr ResourceType IResource::TypeToEnum() { return ResourceType::Unknown; }

template<typename T>
inline constexpr void validate_resource_type() { static_assert(std::is_base_of<IResource, T>::value, "Provided type does not implement IResource"); }

// Explicit template instantiation
#define INSTANTIATE_TO_RESOURCE_TYPE(T, enumT) template<> SPARTAN_CLASS ResourceType IResource::TypeToEnum<T>() { validate_resource_type<T>(); return enumT; }

// To add a new resource to the engine, simply register it here
INSTANTIATE_TO_RESOURCE_TYPE(RHI_Texture,        ResourceType::Texture)
INSTANTIATE_TO_RESOURCE_TYPE(RHI_Texture2D,        ResourceType::Texture2d)
INSTANTIATE_TO_RESOURCE_TYPE(RHI_TextureCube,    ResourceType::TextureCube)
INSTANTIATE_TO_RESOURCE_TYPE(AudioClip,            ResourceType::Audio)
INSTANTIATE_TO_RESOURCE_TYPE(Material,            ResourceType::Material)
INSTANTIATE_TO_RESOURCE_TYPE(Model,                ResourceType::Model)
INSTANTIATE_TO_RESOURCE_TYPE(Animation,            ResourceType::Animation)
INSTANTIATE_TO_RESOURCE_TYPE(Font,                ResourceType::Font)
