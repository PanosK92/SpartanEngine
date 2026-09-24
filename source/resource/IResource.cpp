/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "IResource.h"
#include "../rhi/RHI_Texture.h"
#include "../font/Font.h"
#include "../geometry/Mesh.h"
#include "../rendering/Material.h"
//=================================

//= NAMESPACES ==========
using namespace std;
using namespace spartan;
//=======================

IResource::IResource(const ResourceType type)
{
    m_resource_type = type;
}

template <typename T>
ResourceType IResource::TypeToEnum() { return ResourceType::Unknown; }

template<typename T>
inline constexpr void validate_resource_type() { static_assert(std::is_base_of<IResource, T>::value, "Provided type does not implement IResource"); }

// Explicit template instantiation
#define INSTANTIATE_TO_RESOURCE_TYPE(T, enumT) template<>  ResourceType IResource::TypeToEnum<T>() { validate_resource_type<T>(); return enumT; }

// To add a new resource to the engine, simply register it here
INSTANTIATE_TO_RESOURCE_TYPE(RHI_Texture, ResourceType::Texture)
INSTANTIATE_TO_RESOURCE_TYPE(Material,    ResourceType::Material)
INSTANTIATE_TO_RESOURCE_TYPE(Font,        ResourceType::Font)
INSTANTIATE_TO_RESOURCE_TYPE(Mesh,        ResourceType::Mesh)
