/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "Material.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture.h"
#include "../World/World.h"
#include "../Core/ProgressTracker.h"
#include "../Core/ThreadPool.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        const char* material_property_to_char_ptr(MaterialProperty material_property)
        {
            switch (material_property)
            {
                case MaterialProperty::Optimized:            return "optimzied";
                case MaterialProperty::WorldSpaceHeight:     return "world_space_height";
                case MaterialProperty::Clearcoat:            return "clearcoat";
                case MaterialProperty::Clearcoat_Roughness:  return "clearcoat_roughness";
                case MaterialProperty::Anisotropic:          return "anisotropic";
                case MaterialProperty::AnisotropicRotation:  return "anisotropic_rotation";
                case MaterialProperty::Sheen:                return "sheen";
                case MaterialProperty::SheenTint:            return "sheen_tint";
                case MaterialProperty::ColorTint:            return "color_tint";
                case MaterialProperty::ColorR:               return "color_r";
                case MaterialProperty::ColorG:               return "color_g";
                case MaterialProperty::ColorB:               return "color_b";
                case MaterialProperty::ColorA:               return "color_a";
                case MaterialProperty::Ior:                  return "ior";
                case MaterialProperty::Roughness:            return "roughness";
                case MaterialProperty::Metalness:            return "metalness";
                case MaterialProperty::Normal:               return "normal";
                case MaterialProperty::Height:               return "height";
                case MaterialProperty::SubsurfaceScattering: return "subsurface_scattering";
                case MaterialProperty::TextureTilingX:       return "texture_tiling_x";
                case MaterialProperty::TextureTilingY:       return "texture_tiling_y";
                case MaterialProperty::TextureOffsetX:       return "texture_offset_x";
                case MaterialProperty::TextureOffsetY:       return "texture_offset_y";
                case MaterialProperty::TextureSlopeBased:    return "texture_slope_based";
                case MaterialProperty::VertexAnimateWind:    return "vertex_animate_wind";
                case MaterialProperty::VertexAnimateWater:   return "vertex_animate_water";
                case MaterialProperty::CullMode:             return "cull_mode";
                case MaterialProperty::Max:                  return "max";
                default:
                {
                    SP_ASSERT_MSG(false, "Unknown material property");
                    return nullptr;
                }
            }
        }
    }

    namespace texture_packing
    {
        void pack_occlusion_roughness_metalness_height(
            const vector<byte>& occlusion,
            const vector<byte>& roughness,
            const vector<byte>& metalness,
            const vector<byte>& height,
            const bool is_gltf,
            vector<byte>& output
        )
        {
            SP_ASSERT_MSG(
                occlusion.size() == roughness.size() &&
                roughness.size() == metalness.size() &&
                metalness.size() == height.size(),
                "The dimensions must be equal"
            );

            // gltf stores occlusion, roughness and metalness in the same texture, as r, g, b channels respectively
            for (size_t i = 0; i < occlusion.size(); i += 4)
            {
                output[i + 0] = occlusion[i];
                output[i + 1] = roughness[i + (is_gltf ? 1 : 0)];
                output[i + 2] = metalness[i + (is_gltf ? 2 : 0)];
                output[i + 3] = height[i];
            }
        }

        void merge_alpha_mask_into_color_alpha(vector<byte>& albedo, vector<byte>& mask)
        {
            SP_ASSERT_MSG(albedo.size() == mask.size(), "The dimensions must be equal");
        
            for (size_t i = 0; i < albedo.size(); i += 4)
            {
                float alpha_albedo   = static_cast<float>(albedo[i + 3]) / 255.0f; // channel a
                float alpha_mask     = static_cast<float>(mask[i]) / 255.0f;       // channel r
                float alpha_combined = min(alpha_albedo, alpha_mask);

                albedo[i + 3] = static_cast<byte>(alpha_combined * 255.0f);
            }
        }
    }

    Material::Material() : IResource(ResourceType::Material)
    {
        m_textures.fill(nullptr);
        m_properties.fill(0.0f);

        SetProperty(MaterialProperty::CullMode,         static_cast<float>(RHI_CullMode::Back));
        SetProperty(MaterialProperty::ColorR,           1.0f);
        SetProperty(MaterialProperty::ColorG,           1.0f);
        SetProperty(MaterialProperty::ColorB,           1.0f);
        SetProperty(MaterialProperty::ColorA,           1.0f);
        SetProperty(MaterialProperty::Roughness,        1.0f);
        SetProperty(MaterialProperty::TextureTilingX,   1.0f);
        SetProperty(MaterialProperty::TextureTilingY,   1.0f);
        SetProperty(MaterialProperty::WorldSpaceHeight, 1.0f);
        SetProperty(MaterialProperty::Ior,              Material::EnumToIor(MaterialIor::Air));
    }

    void Material::LoadFromFile(const std::string& file_path, bool async)
    {
        pugi::xml_document doc;
        if (!doc.load_file(file_path.c_str()))
        {
            SP_LOG_ERROR("Failed to load XML file");
            return;
        }

        SetResourceFilePath(file_path);

        pugi::xml_node node_material = doc.child("Material");

        // load properties
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); ++i)
        {
            const char* attribute_name = material_property_to_char_ptr(static_cast<MaterialProperty>(i));
            m_properties[i] = node_material.child(attribute_name).text().as_float();
        }

        // load textures
        uint32_t texture_count = node_material.child("textures").attribute("count").as_uint();
        for (uint32_t i = 0; i < texture_count; ++i)
        {
            string node_name            = "texture_" + to_string(i);
            pugi::xml_node node_texture = node_material.child("textures").child(node_name.c_str());

            MaterialTextureType tex_type = static_cast<MaterialTextureType>(node_texture.attribute("texture_type").as_uint());
            string tex_name              = node_texture.attribute("texture_name").as_string();
            string tex_path              = node_texture.attribute("texture_path").as_string();

            // If the texture happens to be loaded, get a reference to it
            auto texture = ResourceCache::GetByName<RHI_Texture>(tex_name);
            // If there is not texture (it's not loaded yet), load it
            if (!texture)
            {
                texture = ResourceCache::Load<RHI_Texture>(tex_path);
            }

            SetTexture(tex_type, texture);
        }

        m_object_size = sizeof(*this);
    }

    void Material::SaveToFile(const string& file_path)
    {
        SetResourceFilePath(file_path);

        pugi::xml_document doc;
        pugi::xml_node materialNode = doc.append_child("Material");

        // save properties
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); ++i)
        {
            const char* attributeName = material_property_to_char_ptr(static_cast<MaterialProperty>(i));
            materialNode.append_child(attributeName).text().set(m_properties[i]);
        }

        // save textures
        pugi::xml_node texturesNode = materialNode.append_child("textures");
        texturesNode.append_attribute("count").set_value(static_cast<uint32_t>(m_textures.size()));

        for (uint32_t i = 0; i < m_textures.size(); ++i)
        {
            string node_name = "texture_" + to_string(i);
            pugi::xml_node textureNode = texturesNode.append_child(node_name.c_str());

            textureNode.append_attribute("texture_type").set_value(i);
            textureNode.append_attribute("texture_name").set_value(m_textures[i] ? m_textures[i]->GetObjectName().c_str() : "");
            textureNode.append_attribute("texture_path").set_value(m_textures[i] ? m_textures[i]->GetResourceFilePathNative().c_str() : "");
        }

        doc.save_file(file_path.c_str());
    }

    void Material::SetTexture(const MaterialTextureType texture_type, RHI_Texture* texture, const uint8_t slot)
    {
        // validate slot range
        SP_ASSERT(slot < slots_per_texture_type);
    
        // calculate the actual array index based on texture type and slot
        uint32_t array_index = (static_cast<uint32_t>(texture_type) * slots_per_texture_type) + slot;

        if (texture)
        {
            m_textures[array_index] = texture;
        }
        else
        {
            m_textures[array_index] = nullptr;
        }

        // set the correct multiplier
        float multiplier = texture != nullptr;
        if (texture_type == MaterialTextureType::Roughness)
        {
            SetProperty(MaterialProperty::Roughness, multiplier);
        }
        else if (texture_type == MaterialTextureType::Metalness)
        {
            SetProperty(MaterialProperty::Metalness, multiplier);
        }
        else if (texture_type == MaterialTextureType::Normal)
        {
            SetProperty(MaterialProperty::Normal, multiplier);
        }
        else if (texture_type == MaterialTextureType::Height)
        {
            SetProperty(MaterialProperty::Height, multiplier);
        }

        SP_FIRE_EVENT(EventType::MaterialOnChanged);
    }

    void Material::SetTexture(const MaterialTextureType texture_type, shared_ptr<RHI_Texture> texture, const uint8_t slot)
    {
        SetTexture(texture_type, texture.get(), slot);
    }

    void Material::SetTexture(const MaterialTextureType texture_type, const string& file_path, const uint8_t slot, const uint32_t flags)
    {
        bool async = true;
        SetTexture(texture_type, ResourceCache::Load<RHI_Texture>(file_path, RHI_Texture_Srv | RHI_Texture_Compress | flags, async), slot);
    }
 
    bool Material::HasTextureOfType(const string& path) const
    {
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            if (texture->GetResourceFilePathNative() == path)
                return true;
        }

        return false;
    }

    bool Material::HasTextureOfType(const MaterialTextureType texture_type) const
    {
        for (uint32_t slot = 0; slot < slots_per_texture_type; slot++)
        {
            if (m_textures[static_cast<uint32_t>(texture_type) * slots_per_texture_type + slot] != nullptr)
                return true; 
        }
    
        return false;
    }

    string Material::GetTexturePathByType(const MaterialTextureType texture_type, const uint8_t slot)
    {
        if (!HasTextureOfType(texture_type))
            return "";

        return m_textures[static_cast<uint32_t>(texture_type)]->GetResourceFilePathNative();
    }

    vector<string> Material::GetTexturePaths()
    {
        vector<string> paths;
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            paths.emplace_back(texture->GetResourceFilePathNative());
        }

        return paths;
    }

    RHI_Texture* Material::GetTexture(const MaterialTextureType texture_type, const uint8_t slot)
    {
        SP_ASSERT(slot < slots_per_texture_type);
        return m_textures[(static_cast<uint32_t>(texture_type) * slots_per_texture_type) + slot];
    }

    void Material::PrepareForGpu()
    {
        SP_ASSERT_MSG(m_resource_state == ResourceState::Max, "Only unprepared materials can be prepared");
        m_resource_state = ResourceState::PreparingForGpu;

        RHI_Texture* texture_color      = GetTexture(MaterialTextureType::Color);
        RHI_Texture* texture_alpha_mask = GetTexture(MaterialTextureType::AlphaMask);
        RHI_Texture* texture_occlusion  = GetTexture(MaterialTextureType::Occlusion);
        RHI_Texture* texture_roughness  = GetTexture(MaterialTextureType::Roughness);
        RHI_Texture* texture_metalness  = GetTexture(MaterialTextureType::Metalness);
        RHI_Texture* texture_height     = GetTexture(MaterialTextureType::Height);

        // pack textures
        {
            // step 1: pack alpha mask into color alpha
            if (texture_alpha_mask)
            {
                if (!texture_color)
                {
                    SetTexture(MaterialTextureType::Color, texture_alpha_mask);
                }
                else
                {
                    if (!texture_color->IsCompressedFormat() && !texture_alpha_mask->IsCompressedFormat())
                    {
                        texture_packing::merge_alpha_mask_into_color_alpha(texture_color->GetMip(0, 0).bytes, texture_alpha_mask->GetMip(0, 0).bytes);
                    }
                }
            }
            
            // step 2: pack occlusion, roughness, metalness, and height into a single texture
            {
                // generate unique name by hashing texture IDs
                size_t hash_value = 0;
                if (texture_occlusion) hash_value ^= texture_occlusion->GetObjectId();
                if (texture_roughness) hash_value ^= texture_roughness->GetObjectId();
                if (texture_metalness) hash_value ^= texture_metalness->GetObjectId();
                if (texture_height)    hash_value ^= texture_height->GetObjectId();

                // fetch the packed texture that corresponds to the hash (in case it was already packed)
                const string tex_name                  = "texture_packed_" + to_string(hash_value);
                shared_ptr<RHI_Texture> texture_packed = ResourceCache::GetByName<RHI_Texture>(tex_name);

                if (!texture_packed)
                {
                    bool textures_are_compressed = (texture_occlusion  && texture_occlusion->IsCompressedFormat()) ||
                                                   (texture_roughness  && texture_roughness->IsCompressedFormat()) ||
                                                   (texture_metalness  && texture_metalness->IsCompressedFormat()) ||
                                                   (texture_height     && texture_height->IsCompressedFormat());
                
                    RHI_Texture* texture_reference = texture_color      ? texture_color      :
                                                     texture_alpha_mask ? texture_alpha_mask :
                                                     texture_occlusion  ? texture_occlusion  :
                                                     texture_roughness  ? texture_roughness  :
                                                     texture_metalness  ? texture_metalness  :
                                                     texture_height;
                
                    uint32_t width     = texture_reference ? texture_reference->GetWidth()    : 1;
                    uint32_t height    = texture_reference ? texture_reference->GetHeight()   : 1;
                    uint32_t depth     = texture_reference ? texture_reference->GetDepth()    : 1;
                    uint32_t mip_count = texture_reference ? texture_reference->GetMipCount() : 1;
                    
                    // create packed texture
                    texture_packed = make_shared<RHI_Texture>
                    (
                        RHI_Texture_Type::Type2D,
                        width,
                        height,
                        depth,
                        mip_count,
                        RHI_Format::R8G8B8A8_Unorm,
                        RHI_Texture_Srv | RHI_Texture_Compress | RHI_Texture_DontPrepareForGpu,
                        tex_name.c_str()
                    );
                    texture_packed->SetResourceFilePath(tex_name + ".png"); // that's a hack, need to fix the ResourceCache to rely on a hash, not names and paths
                    texture_packed->AllocateMip();
                    
                    // create some default data to replace missing textures
                    const size_t texture_size = width * height * 4;
                    vector<byte> texture_one(texture_size, static_cast<byte>(255));
                    vector<byte> texture_zero(texture_size, static_cast<byte>(0));
                    vector<byte> texture_half(texture_size, static_cast<byte>(127));
                    
                    // create packed data and fallback to default data when needed
                    texture_packing::pack_occlusion_roughness_metalness_height
                    (
                        (texture_occlusion && !texture_occlusion->GetMip(0, 0).bytes.empty()) ? texture_occlusion->GetMip(0, 0).bytes : texture_one,
                        (texture_roughness && !texture_roughness->GetMip(0, 0).bytes.empty()) ? texture_roughness->GetMip(0, 0).bytes : texture_one,
                        (texture_metalness && !texture_metalness->GetMip(0, 0).bytes.empty()) ? texture_metalness->GetMip(0, 0).bytes : texture_zero,
                        (texture_height    && !texture_height->GetMip(0, 0).bytes.empty())    ? texture_height->GetMip(0, 0).bytes    : texture_half,
                        GetProperty(MaterialProperty::Gltf) == 1.0f,
                        texture_packed->GetMip(0, 0).bytes
                    );
 
                    texture_packed = ResourceCache::Cache<RHI_Texture>(texture_packed);
                }

                SetTexture(MaterialTextureType::Packed, texture_packed);

                // step 3: textures that have been packed into others can now be downsampled to circa 128x128 so they can be displayed in the editor and take little memory
                {
                    if (texture_alpha_mask) texture_alpha_mask->SetFlag(RHI_Texture_Thumbnail);
                    if (texture_occlusion)  texture_occlusion->SetFlag(RHI_Texture_Thumbnail);
                    if (texture_roughness)  texture_roughness->SetFlag(RHI_Texture_Thumbnail);
                    if (texture_metalness)  texture_metalness->SetFlag(RHI_Texture_Thumbnail);
                    if (texture_height)     texture_height->SetFlag(RHI_Texture_Thumbnail);
                }
            }
        }

        // PrepareForGpu() generates mips, compresses and uploads to GPU, so we offload it to a thread
        ThreadPool::AddTask([this]()
        {
            // prepare all textures
            for (RHI_Texture* texture : m_textures)
            {
                if (texture && texture->GetResourceState() == ResourceState::Max)
                {
                    texture->SetFlag(RHI_Texture_DontPrepareForGpu, false);
                    texture->PrepareForGpu();
                }
            }

             // determine if the material is optimized
             bool is_optimized = GetTexture(MaterialTextureType::Packed) != nullptr;
             for (RHI_Texture* texture : m_textures)
             {
                 if (texture && texture->IsCompressedFormat())
                 {
                     is_optimized = true;
                     break;
                 }
             }
             SetProperty(MaterialProperty::Optimized, is_optimized ? 1.0f : 0.0f);

             m_resource_state = ResourceState::PreparedForGpu;
        });
    }

    uint32_t Material::GetUsedSlotCount() const
    {
        // array to track highest used slot for each texture type
        uint32_t max_used_slot[static_cast<size_t>(MaterialTextureType::Max)] = { 0 };
    
        // iterate through each texture type
        for (size_t type = 0; type < static_cast<size_t>(MaterialTextureType::Max); type++)
        {
            // check each slot for this type
            for (uint32_t slot = 0; slot < slots_per_texture_type; ++slot)
            {
                // calculate array index using the helper function
                uint32_t index = (static_cast<uint32_t>(type) * slots_per_texture_type) + slot;
                
                // if this slot has a texture, update the max used slot for this type
                if (m_textures[index])
                {
                    max_used_slot[type] = slot + 1; // +1 because we want count, not index
                }
            }
        }
    
        // return the maximum used slot count across all texture types (minimum of 1)
        return max<uint32_t>(*max_element(begin(max_used_slot), end(max_used_slot)), 1);
    }

    void Material::SetProperty(const MaterialProperty property_type, float value)
    {
        if (m_properties[static_cast<uint32_t>(property_type)] == value)
            return;

        if (property_type == MaterialProperty::ColorA)
        {
            // if an object switches from opaque to transparent or vice versa, make the world update so that the renderer
            // goes through the entities and makes the ones that use this material, render in the correct mode.
            float current_alpha = m_properties[static_cast<uint32_t>(property_type)];
            if ((current_alpha != 1.0f && value == 1.0f) || (current_alpha == 1.0f && value != 1.0f))
            {
                RHI_CullMode cull_mode = value < 1.0f ? RHI_CullMode::None : RHI_CullMode::Back;
                m_properties[static_cast<uint32_t>(MaterialProperty::CullMode)] = static_cast<float>(cull_mode);
                World::Resolve();
            }

            // transparent objects are typically see-through (low roughness) so use the alpha as the roughness multiplier.
            m_properties[static_cast<uint32_t>(MaterialProperty::Roughness)] = value * 0.5f;
        }

        if (property_type == MaterialProperty::Ior)
        {
            value = clamp(value, Material::EnumToIor(MaterialIor::Air), Material::EnumToIor(MaterialIor::Diamond));
        }

        m_properties[static_cast<uint32_t>(property_type)] = value;

        // if the world is loading, don't fire an event as we will spam the event system
        // also the renderer will check all the materials after loading anyway
        if (!ProgressTracker::GetProgress(ProgressType::World).IsProgressing())
        {
            SP_FIRE_EVENT(EventType::MaterialOnChanged);
        }
    }

    void Material::SetColor(const Color& color)
    {
        SetProperty(MaterialProperty::ColorR, color.r);
        SetProperty(MaterialProperty::ColorG, color.g);
        SetProperty(MaterialProperty::ColorB, color.b);
        SetProperty(MaterialProperty::ColorA, color.a);
    }

    bool Material::IsAlphaTested()
    {
        bool albedo_mask = false;
        if (RHI_Texture* texture = GetTexture(MaterialTextureType::Color))
        {
            albedo_mask = texture->IsSemiTransparent();
        }

        return HasTextureOfType(MaterialTextureType::AlphaMask) || albedo_mask;
    }

    float Material::EnumToIor(const MaterialIor ior)
    {
        switch (ior)
        {
            case MaterialIor::Air:      return 1.0f;
            case MaterialIor::Water:    return 1.33f;
            case MaterialIor::Eyes:     return 1.38f;
            case MaterialIor::Glass:    return 1.52f;
            case MaterialIor::Sapphire: return 1.76f;
            case MaterialIor::Diamond:  return 2.42f;
            default:                    return 1.0f;
        }
    }

    MaterialIor Material::IorToEnum(const float ior)
    {
        const float epsilon = 0.001f;

        if (std::abs(ior - 1.0f)  < epsilon) return MaterialIor::Air;
        if (std::abs(ior - 1.33f) < epsilon) return MaterialIor::Water;
        if (std::abs(ior - 1.38f) < epsilon) return MaterialIor::Eyes;
        if (std::abs(ior - 1.52f) < epsilon) return MaterialIor::Glass;
        if (std::abs(ior - 1.76f) < epsilon) return MaterialIor::Sapphire;
        if (std::abs(ior - 2.42f) < epsilon) return MaterialIor::Diamond;

        return MaterialIor::Air;
    }
}
