/*
Copyright(c) 2015-2025 Panos Karabelas

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
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        const char* material_property_to_char_ptr(MaterialProperty material_property)
        {
            switch (material_property)
            {
                // System / meta
                case MaterialProperty::Optimized:                  return "optimized";
                case MaterialProperty::Gltf:                       return "gltf";
        
                // World / geometry context
                case MaterialProperty::WorldHeight:                return "world_space_height";
                case MaterialProperty::WorldWidth:                 return "world_space_width";
                case MaterialProperty::WorldSpaceUv:               return "world_space_uv";
                case MaterialProperty::Tessellation:               return "tessellation";
        
                // Core PBR
                case MaterialProperty::ColorR:                     return "color_r";
                case MaterialProperty::ColorG:                     return "color_g";
                case MaterialProperty::ColorB:                     return "color_b";
                case MaterialProperty::ColorA:                     return "color_a";
                case MaterialProperty::Roughness:                  return "roughness";
                case MaterialProperty::Metalness:                  return "metalness";
                case MaterialProperty::Normal:                     return "normal";
                case MaterialProperty::Height:                     return "height";
        
                // Extended PBR
                case MaterialProperty::Clearcoat:                  return "clearcoat";
                case MaterialProperty::Clearcoat_Roughness:        return "clearcoat_roughness";
                case MaterialProperty::Anisotropic:                return "anisotropic";
                case MaterialProperty::AnisotropicRotation:        return "anisotropic_rotation";
                case MaterialProperty::Sheen:                      return "sheen";
                case MaterialProperty::SubsurfaceScattering:       return "subsurface_scattering";
                case MaterialProperty::NormalFromAlbedo:           return "normal_from_albedo";
                case MaterialProperty::EmissiveFromAlbedo:         return "emissive_from_albedo";
        
                // Texture transforms
                case MaterialProperty::TextureTilingX:             return "texture_tiling_x";
                case MaterialProperty::TextureTilingY:             return "texture_tiling_y";
                case MaterialProperty::TextureOffsetX:             return "texture_offset_x";
                case MaterialProperty::TextureOffsetY:             return "texture_offset_y";
        
                // Special effects
                case MaterialProperty::IsTerrain:                  return "texture_slope_based";
                case MaterialProperty::IsGrassBlade:               return "is_grass_blade";
                case MaterialProperty::WindAnimation:              return "wind_animation";
                case MaterialProperty::ColorVariationFromInstance: return "color_variation_from_instance";
                case MaterialProperty::IsWater:                    return "vertex_animate_water";
        
                // Render settings
                case MaterialProperty::CullMode:                   return "cull_mode";
        
                // Sentinel
                case MaterialProperty::Max:                        return "max";
        
                default:
                {
                    SP_ASSERT_MSG(false, "Unknown material property");
                    return nullptr;
                }
            }
        }

        float jonswap_alpha(float fetch, float windSpeed) { return 0.076f * pow(9.81f * fetch / windSpeed / windSpeed, -0.22f); }
        float jonswap_peak_frequency(float fetch, float windSpeed) {
            float g = 9.81f;
            float dimensionlessFetch = g * fetch / (windSpeed * windSpeed);
            return 22.0f * (g / windSpeed) * pow(dimensionlessFetch, -0.33f);
        }
    }

    namespace texture_processing
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

            // just like gltf: occlusion, roughness and metalness as r, g, b channels respectively
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

        void generate_normal_from_albedo(const vector<byte>& albedo_data, vector<byte>& normal_data, uint32_t width, uint32_t height, bool flip_y = true, float intensity = 4.0f)
        {
            // validate inputs
            SP_ASSERT_MSG(albedo_data.size() == width * height * 4, "Invalid albedo data size");
            SP_ASSERT_MSG(intensity > 0.0f, "Intensity must be positive");
            normal_data.resize(width * height * 4);
        
            // 5x5 sobel kernels for x and y gradients
            const int sobel_x[5][5] =
            {
                {-2, -1,  0,  1,  2},
                {-3, -2,  0,  2,  3},
                {-4, -3,  0,  3,  4},
                {-3, -2,  0,  2,  3},
                {-2, -1,  0,  1,  2}
            };
            const int sobel_y[5][5] =
            {
                {-2, -3, -4, -3, -2},
                {-1, -2, -3, -2, -1},
                { 0,  0,  0,  0,  0},
                { 1,  2,  3,  2,  1},
                { 2,  3,  4,  3,  2}
            };
        
            // function to get perceptual luminance (grayscale)
            auto get_grayscale = [&](uint32_t px, uint32_t py) -> float
            {
                uint32_t index = (py * width + px) * 4;
                float r = static_cast<float>(to_integer<uint8_t>(albedo_data[index + 0])) / 255.0f;
                float g = static_cast<float>(to_integer<uint8_t>(albedo_data[index + 1])) / 255.0f;
                float b = static_cast<float>(to_integer<uint8_t>(albedo_data[index + 2])) / 255.0f;
                // perceptual luminance (itu-r bt.709)
                return 0.2126f * r + 0.7152f * g + 0.0722f * b;
            };
        
            // temporary buffer for normal map before post-processing
            vector<Vector3> temp_normals(width * height);
        
            // compute gradients and normals
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    float gx = 0.0f, gy = 0.0f;
        
                    // apply 5x5 sobel kernels
                    for (int j = -2; j <= 2; ++j)
                    {
                        for (int i = -2; i <= 2; ++i)
                        {
                            int px = (x + i + width) % width;
                            int py = (y + j + height) % height;
                            float value = get_grayscale(px, py);
                            gx += value * sobel_x[j + 2][i + 2];
                            gy += value * sobel_y[j + 2][i + 2];
                        }
                    }
        
                    // normalize gradient magnitude and apply intensity
                    float scale = 1.0f / 128.0f; // adjusted for 5x5 kernel
                    gx *= scale * intensity;
                    gy *= scale * intensity;
        
                    // compute normal (z = 1 for surface facing up)
                    Vector3 normal(gx, flip_y ? -gy : gy, 1.0f);
                    normal.Normalize();
        
                    // store in temporary buffer
                    temp_normals[y * width + x] = normal;
                }
            }
        
            // 3x3 gaussian kernel for smoothing
            const float gaussian[3][3] =
            {
                {1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f},
                {2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f},
                {1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f}
            };
        
            // apply gaussian blur and store final normals
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    Vector3 blurred_normal(0.0f, 0.0f, 0.0f);
        
                    // apply gaussian blur
                    for (int j = -1; j <= 1; ++j)
                    {
                        for (int i = -1; i <= 1; ++i)
                        {
                            int px = (x + i + width) % width;
                            int py = (y + j + height) % height;
                            Vector3 n = temp_normals[py * width + px];
                            float weight = gaussian[j + 1][i + 1];
                            blurred_normal.x += n.x * weight;
                            blurred_normal.y += n.y * weight;
                            blurred_normal.z += n.z * weight;
                        }
                    }
        
                    // re-normalize after blurring
                    blurred_normal.Normalize();
        
                    // map to [0,1] for storage
                    blurred_normal = (blurred_normal + Vector3::One) * 0.5f;
        
                    // store in output
                    uint32_t index = (y * width + x) * 4;
                    normal_data[index + 0] = static_cast<byte>(static_cast<uint8_t>(blurred_normal.x * 255.0f)); // r: x direction
                    normal_data[index + 1] = static_cast<byte>(static_cast<uint8_t>(blurred_normal.y * 255.0f)); // g: y direction
                    normal_data[index + 2] = static_cast<byte>(static_cast<uint8_t>(blurred_normal.z * 255.0f)); // b: z direction
                    normal_data[index + 3] = static_cast<byte>(255); // a: full opacity
                }
            }
        }
    }

    Material::Material() : IResource(ResourceType::Material)
    {
        m_textures.fill(nullptr);
        m_properties.fill(0.0f);

        SetProperty(MaterialProperty::ColorR,         1.0f);
        SetProperty(MaterialProperty::ColorG,         1.0f);
        SetProperty(MaterialProperty::ColorB,         1.0f);
        SetProperty(MaterialProperty::ColorA,         1.0f);
        SetProperty(MaterialProperty::Roughness,      1.0f);
        SetProperty(MaterialProperty::TextureTilingX, 1.0f);
        SetProperty(MaterialProperty::TextureTilingY, 1.0f);
        SetProperty(MaterialProperty::WorldHeight,    1.0f);
        SetProperty(MaterialProperty::CullMode,       static_cast<float>(RHI_CullMode::Back));
    }

    void Material::LoadFromFile(const string& file_path)
    {
        pugi::xml_document doc;
        if (!doc.load_file(file_path.c_str()))
        {
            SP_LOG_ERROR("Failed to load XML file %s", file_path.c_str());
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
        pugi::xml_node textures_node = node_material.child("textures");
        for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Max); ++type)
        {
            for (uint32_t slot = 0; slot < slots_per_texture_type; ++slot)
            {
                uint32_t index = type * slots_per_texture_type + slot;
                string node_name = "texture_" + to_string(index);
                pugi::xml_node node_texture = textures_node.child(node_name.c_str());
                if (!node_texture)
                    continue; // skip if node doesn't exist
    
                string tex_name = node_texture.attribute("texture_name").as_string();
                string tex_path = node_texture.attribute("texture_path").as_string();
    
                // if the texture is already loaded, get a reference to it
                auto texture = ResourceCache::GetByName<RHI_Texture>(tex_name);

                // if the texture is not loaded yet, load it
                if (!texture && !tex_path.empty())
                {
                    texture = ResourceCache::Load<RHI_Texture>(tex_path);
                }
    
                if (texture)
                {
                    SetTexture(static_cast<MaterialTextureType>(type), texture.get(), slot, false);
                }
            }
        }
    
        m_object_size = sizeof(*this);
    }
    
    void Material::SaveToFile(const string& file_path)
    {
        SetResourceFilePath(file_path);
        pugi::xml_document doc;
        pugi::xml_node material_node = doc.append_child("Material");
    
        // save properties
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); ++i)
        {
            const char* attribute_name = material_property_to_char_ptr(static_cast<MaterialProperty>(i));
            material_node.append_child(attribute_name).text().set(m_properties[i]);
        }
    
        // save textures
        pugi::xml_node textures_node = material_node.append_child("textures");
        textures_node.append_attribute("count").set_value(static_cast<uint32_t>(m_textures.size()));
        for (uint32_t type = 0; type < static_cast<uint32_t>(MaterialTextureType::Max); ++type)
        {
            for (uint32_t slot = 0; slot < slots_per_texture_type; ++slot)
            {
                uint32_t index = type * slots_per_texture_type + slot;
                string node_name = "texture_" + to_string(index);
                pugi::xml_node texture_node = textures_node.append_child(node_name.c_str());
                texture_node.append_attribute("texture_type").set_value(type);
                texture_node.append_attribute("texture_slot").set_value(slot);
                texture_node.append_attribute("texture_name").set_value(m_textures[index] ? m_textures[index]->GetObjectName().c_str() : "");
                texture_node.append_attribute("texture_path").set_value(m_textures[index] ? m_textures[index]->GetResourceFilePath().c_str() : "");
            }
        }
    
        doc.save_file(file_path.c_str());
    }

    void Material::SetTexture(const MaterialTextureType texture_type, RHI_Texture* texture, const uint8_t slot, const bool auto_adjust_multipler)
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

        if (auto_adjust_multipler)
        {
            if (texture_type == MaterialTextureType::Metalness)
            {
                SetProperty(MaterialProperty::Metalness, 1.0f);
            }
            else if (texture_type == MaterialTextureType::Normal)
            {
                SetProperty(MaterialProperty::Normal, 1.0f);
            }
            else if (texture_type == MaterialTextureType::Roughness)
            {
                SetProperty(MaterialProperty::Roughness, 1.0f);
            }
            else if (texture_type == MaterialTextureType::Height)
            {
                SetProperty(MaterialProperty::Height, 1.0f);
            }
        }

        SP_FIRE_EVENT(EventType::MaterialOnChanged);
    }

    void Material::SetTexture(const MaterialTextureType texture_type, shared_ptr<RHI_Texture> texture, const uint8_t slot)
    {
        SetTexture(texture_type, texture.get(), slot);
    }

    void Material::SetTexture(const MaterialTextureType texture_type, const string& file_path, const uint8_t slot)
    {
        SetTexture(texture_type, ResourceCache::Load<RHI_Texture>(file_path, RHI_Texture_Srv | RHI_Texture_Compress | RHI_Texture_DontPrepareForGpu), slot);
    }
 
    bool Material::HasTextureOfType(const string& path) const
    {
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            if (texture->GetResourceFilePath() == path)
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

        return m_textures[static_cast<uint32_t>(texture_type)]->GetResourceFilePath();
    }

    vector<string> Material::GetTexturePaths()
    {
        vector<string> paths;
        for (const auto& texture : m_textures)
        {
            if (!texture)
                continue;

            paths.emplace_back(texture->GetResourceFilePath());
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

        auto pack_textures = [this](const uint8_t slot)
        {
            // get textures
            RHI_Texture* texture_color      = GetTexture(MaterialTextureType::Color,     slot);
            RHI_Texture* texture_normal     = GetTexture(MaterialTextureType::Normal,    slot);
            RHI_Texture* texture_alpha_mask = GetTexture(MaterialTextureType::AlphaMask, slot);
            RHI_Texture* texture_occlusion  = GetTexture(MaterialTextureType::Occlusion, slot);
            RHI_Texture* texture_roughness  = GetTexture(MaterialTextureType::Roughness, slot);
            RHI_Texture* texture_metalness  = GetTexture(MaterialTextureType::Metalness, slot);
            RHI_Texture* texture_height     = GetTexture(MaterialTextureType::Height,    slot);

            // check for normal_from_albedo flag
            if (GetProperty(MaterialProperty::NormalFromAlbedo) == 1.0f && texture_color && !texture_color->IsCompressedFormat())
            {
                // get albedo dimensions
                uint32_t width     = texture_color->GetWidth();
                uint32_t height    = texture_color->GetHeight();
                uint32_t depth     = texture_color->GetDepth();
                uint32_t mip_count = texture_color->GetMipCount();

                // generate normal map name
                string normal_name = "normal_from_" + texture_color->GetObjectName() + "_slot" + to_string(slot);

                // check if normal map already exists
                shared_ptr<RHI_Texture> texture_normal_new = ResourceCache::GetByName<RHI_Texture>(normal_name);
                if (!texture_normal_new)
                {
                    // create new normal texture
                    texture_normal_new = make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        width,
                        height,
                        depth,
                        mip_count,
                        RHI_Format::R8G8B8A8_Unorm,
                        RHI_Texture_Srv | RHI_Texture_Compress | RHI_Texture_DontPrepareForGpu,
                        normal_name.c_str()
                    );

                    // allocate mip
                    texture_normal_new->AllocateMip();

                    // generate normal map data
                    vector<byte> normal_data;
                    texture_processing::generate_normal_from_albedo(
                        texture_color->GetMip(0, 0).bytes,
                        normal_data,
                        width,
                        height
                    );
                    texture_normal_new->GetMip(0, 0).bytes = move(normal_data);

                    // cache the new texture
                    texture_normal_new->SetResourceFilePath(texture_color->GetObjectName() + "_normal_from_albedo.png"); // that's a hack, need to fix the ResourceCache to rely on a hash, not names and paths
                    texture_normal_new = ResourceCache::Cache<RHI_Texture>(texture_normal_new);
                }

                // set the new normal texture
                SetTexture(MaterialTextureType::Normal, texture_normal_new, slot);
                texture_normal = texture_normal_new.get();
            }

            RHI_Texture* reference_texture = texture_color      ? texture_color      :
                                             texture_alpha_mask ? texture_alpha_mask :
                                             texture_occlusion  ? texture_occlusion  :
                                             texture_roughness  ? texture_roughness  :
                                             texture_metalness  ? texture_metalness  :
                                             texture_height;
                    
            uint32_t reference_width     = reference_texture ? reference_texture->GetWidth()    : 1;
            uint32_t reference_height    = reference_texture ? reference_texture->GetHeight()   : 1;
            uint32_t reference_depth     = reference_texture ? reference_texture->GetDepth()    : 1;
            uint32_t reference_mip_count = reference_texture ? reference_texture->GetMipCount() : 1;

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
                            texture_processing::merge_alpha_mask_into_color_alpha(texture_color->GetMip(0, 0).bytes, texture_alpha_mask->GetMip(0, 0).bytes);
                        }
                    }
                }
                
                // step 2: pack occlusion, roughness, metalness, and height into a single texture
                {
                    bool textures_are_compressed = (texture_occlusion  && texture_occlusion->IsCompressedFormat()) ||
                                                   (texture_roughness  && texture_roughness->IsCompressedFormat()) ||
                                                   (texture_metalness  && texture_metalness->IsCompressedFormat()) ||
                                                   (texture_height     && texture_height->IsCompressedFormat());

                    // generate unique name by hashing texture IDs
                    string tex_name = GetObjectName() + "_packed";
                    shared_ptr<RHI_Texture> texture_packed = ResourceCache::GetByName<RHI_Texture>(tex_name);
                    if (!texture_packed && !textures_are_compressed)
                    {
                        // create packed texture
                        texture_packed = make_shared<RHI_Texture>
                        (
                            RHI_Texture_Type::Type2D,
                            reference_width,
                            reference_height,
                            reference_depth,
                            reference_mip_count,
                            RHI_Format::R8G8B8A8_Unorm,
                            RHI_Texture_Srv | RHI_Texture_Compress | RHI_Texture_DontPrepareForGpu,
                            tex_name.c_str()
                        );
                        texture_packed->SetResourceFilePath(tex_name + ".png"); // that's a hack, need to fix the ResourceCache to rely on a hash, not names and paths
                        texture_packed->AllocateMip();

                        // create some default data to replace missing textures
                        const size_t texture_size = reference_width * reference_height * 4;
                        vector<byte> texture_one(texture_size, static_cast<byte>(255));
                        vector<byte> texture_zero(texture_size, static_cast<byte>(0));
                        vector<byte> texture_half(texture_size, static_cast<byte>(127));

                        // determine metalness data based on texture and property
                        vector<byte> metalness_data = texture_zero; // default to zero
                        if (texture_metalness)
                        {
                            metalness_data = texture_metalness->GetMip(0, 0).bytes; // use texture if available
                        }
                        else if (GetProperty(MaterialProperty::Metalness) != 0.0f)
                        {
                            metalness_data = texture_one; // use all ones if Metalness property is non-zero and no texture
                        }

                        // create packed data and fallback to default data when needed
                        texture_processing::pack_occlusion_roughness_metalness_height
                        (
                            (texture_occlusion && !texture_occlusion->GetMip(0, 0).bytes.empty()) ? texture_occlusion->GetMip(0, 0).bytes : texture_one,
                            (texture_roughness && !texture_roughness->GetMip(0, 0).bytes.empty()) ? texture_roughness->GetMip(0, 0).bytes : texture_one,
                            metalness_data,
                            (texture_height    && !texture_height->GetMip(0, 0).bytes.empty())    ? texture_height->GetMip(0, 0).bytes    : texture_half,
                            GetProperty(MaterialProperty::Gltf) == 1.0f,
                            texture_packed->GetMip(0, 0).bytes
                        );
 
                        texture_packed = ResourceCache::Cache<RHI_Texture>(texture_packed);
                    }

                    SetTexture(MaterialTextureType::Packed, texture_packed, slot);

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
        };

        for (uint8_t slot = 0; slot < GetUsedSlotCount(); slot++)
        {
            pack_textures(slot);
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

        m_properties[static_cast<uint32_t>(property_type)] = value;

        // if the world is loading, don't fire an event as we will spam the event system
        // also the renderer will check all the materials after loading anyway
        if (!ProgressTracker::GetProgress(ProgressType::World).IsProgressing())
        {
            SP_FIRE_EVENT(EventType::MaterialOnChanged);
        }
    }

    float Material::GetOceanProperty(const JonswapParameters property_type) const
    {
        SP_ASSERT_MSG(m_properties[static_cast<uint32_t>(MaterialProperty::IsOcean)] == 1.0f, "Only ocean materials can have ocean properties");

        return m_ocean_properties[static_cast<uint32_t>(property_type)];
    }

    void Material::SetOceanProperty(const JonswapParameters property_type, const float value)
    {
        SP_ASSERT_MSG(m_properties[static_cast<uint32_t>(MaterialProperty::IsOcean)] == 1.0f, "Only ocean materials can have ocean properties");

        // special cases
        if (property_type == JonswapParameters::Alpha)
        {
            float fetch = m_ocean_properties[static_cast<uint32_t>(JonswapParameters::Fetch)];
            float windSpeed = m_ocean_properties[static_cast<uint32_t>(JonswapParameters::WindSpeed)];

            m_ocean_properties[static_cast<uint32_t>(property_type)] = jonswap_alpha(fetch, windSpeed);
        }
        else if (property_type == JonswapParameters::PeakOmega)
        {
            float fetch = m_ocean_properties[static_cast<uint32_t>(JonswapParameters::Fetch)];
            float windSpeed = m_ocean_properties[static_cast<uint32_t>(JonswapParameters::WindSpeed)];

            m_ocean_properties[static_cast<uint32_t>(property_type)] = jonswap_peak_frequency(fetch, windSpeed);
        }
        else
        {
            if (m_ocean_properties[static_cast<uint32_t>(property_type)] == value)
                return;

            m_ocean_properties[static_cast<uint32_t>(property_type)] = value;

            // if the world is loading, don't fire an event as we will spam the event system
            // also the renderer will check all the materials after loading anyway
            if (!ProgressTracker::GetProgress(ProgressType::World).IsProgressing())
            {
                SP_FIRE_EVENT(EventType::MaterialOnChanged);
            }
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

        if (abs(ior - 1.0f)  < epsilon) return MaterialIor::Air;
        if (abs(ior - 1.33f) < epsilon) return MaterialIor::Water;
        if (abs(ior - 1.38f) < epsilon) return MaterialIor::Eyes;
        if (abs(ior - 1.52f) < epsilon) return MaterialIor::Glass;
        if (abs(ior - 1.76f) < epsilon) return MaterialIor::Sapphire;
        if (abs(ior - 2.42f) < epsilon) return MaterialIor::Diamond;

        return MaterialIor::Air;
    }
}
