#/*
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

#pragma once

//= INCLUDES =====================
#include <memory>
#include <array>
#include "../Resource/IResource.h"
#include "Color.h"
//================================

namespace spartan
{
    class RHI_Texture;

    enum class MaterialTextureType
    {
        Color,
        Roughness, // packed g
        Metalness, // packed b
        Normal,
        Occlusion, // packed r
        Emission,
        Height,    // packed a
        AlphaMask, // packed into color a
        Packed,    // occlusion, roughness, metalness, height
        Max
    };

    enum class MaterialProperty
    {
        // system / meta
        Optimized,                  // material has been optimized (packed/compressed textures)
        Gltf,                       // imported from gltf file
    
        // world / geometry context
        WorldHeight,                // height of the mesh
        WorldWidth,                 // width of the mesh
        WorldSpaceUv,               // use world-space uvs
        Tessellation,               // enable tessellation
    
        // core pbr
        ColorR,                     // base color red
        ColorG,                     // base color green
        ColorB,                     // base color blue
        ColorA,                     // base color alpha
        Roughness,                  // surface roughness
        Metalness,                  // metallic factor
        Normal,                     // normal map
        Height,                     // height map
    
        // extended pbr
        Clearcoat,                  // extra reflective layer
        Clearcoat_Roughness,        // roughness of clearcoat
        Anisotropic,                // anisotropy level
        AnisotropicRotation,        // anisotropy direction
        Sheen,                      // velvet-like reflection at grazing angles
        SubsurfaceScattering,       // light scattering through surface
        NormalFromAlbedo,           // derive normal from albedo
        EmissiveFromAlbedo,         // derive emissive from albedo
    
        // texture transforms
        TextureTilingX,             // tiling along x axis
        TextureTilingY,             // tiling along y axis
        TextureOffsetX,             // offset along x axis
        TextureOffsetY,             // offset along y axis
    
        // special effects
        IsTerrain,                  // slope-based texture mapping
        IsGrassBlade,               // grass blade specific effects
        WindAnimation,              // vertex wind animation
        ColorVariationFromInstance, // per-instance color variation
        IsWater,                    // water flow animation
        IsOcean,                    // fft ocean rendering
    
        // render settings
        CullMode,                   // face culling mode
    
        // sentinel
        Max                         // total number of properties
    };

    enum class MaterialIor
    {
        Air,
        Water,
        Eyes,
        Glass,
        Sapphire,
        Diamond,
        Max
    };

    // used for ocean calculations
    enum class OceanParameters
    {
        Scale, // used to scale the Spectrum [1.0f, 5.0f] --> Value Range
        SpreadBlend, // used to blend between agitated water motion, and windDirection [0.0f, 1.0f]
        Swell, // influences wave choppines, the bigger the swell, the longer the wave length [0.0f, 1.0f]
        Gamma, // defines the Spectrum Peak [0.0f, 7.0f]

        ShortWavesFade, // [0.0f, 1.0f]
        WindDirection, // [0.0f, 360.0f]
        Fetch,  // distance over which Wind impacts Wave Formation [0.0f, 10000.0f]
        WindSpeed, // [0.0f, 100.0f]

        RepeatTime,
        Angle,
        Alpha,
        PeakOmega,

        Depth,
        LowCutoff,
        HighCutoff,

        FoamDecayRate,
        FoamBias,
        FoamThreshold,
        FoamAdd,

        DisplacementScale,
        SlopeScale,
        LengthScale,

        Max
    };

    class Material : public IResource
    {
    public:
        Material();
        ~Material() = default;

        static const uint32_t slots_per_texture = 4;

        // iresource
        void LoadFromFile(const std::string& file_path) override;
        void SaveToFile(const std::string& file_path) override;

        // textures
        void SetTexture(const MaterialTextureType texture_type, RHI_Texture* texture, const uint8_t slot = 0, const bool auto_adjust_multipler = true);
        void SetTexture(const MaterialTextureType texture_type, std::shared_ptr<RHI_Texture> texture, const uint8_t slot = 0);
        void SetTexture(const MaterialTextureType texture_type, const std::string& file_path, const uint8_t slot = 0);
        bool HasTextureOfType(const std::string& path) const;
        bool HasTextureOfType(const MaterialTextureType texture_type) const;
        std::string GetTexturePathByType(const MaterialTextureType texture_type, const uint8_t slot = 0);
        std::vector<std::string> GetTexturePaths();
        RHI_Texture* GetTexture(const MaterialTextureType texture_type, const uint8_t slot = 0);
        const std::array<RHI_Texture*, static_cast<uint32_t>(MaterialTextureType::Max) * slots_per_texture>& GetTextures() const { return m_textures; }

        // index of refraction
        static float EnumToIor(const MaterialIor ior);
        static MaterialIor IorToEnum(const float ior);

        // properties
        float GetProperty(const MaterialProperty property_type) const { return m_properties[static_cast<uint32_t>(property_type)]; }
        void SetProperty(const MaterialProperty property_type, const float value);
        float GetOceanProperty(const OceanParameters property_type) const;
        void SetOceanProperty(const OceanParameters property_type, const float value);
        void SetColor(const Color& color);
        bool IsTransparent() const { return GetProperty(MaterialProperty::ColorA) < 1.0f; }
        bool IsAlphaTested();

        // misc
        void PrepareForGpu();
        uint32_t GetUsedSlotCount() const;
        void SetIndex(const uint32_t index) { m_index = index; }
        uint32_t GetIndex() const           { return m_index; }

        // ocean
        bool ShouldComputeSpectrum() const { return m_should_compute_spectrum; }
        void MarkSpectrumAsComputed() { m_should_compute_spectrum = false; }
        void SetOceanTileCount(const uint32_t count) { m_ocean_tiles = count; }
        uint32_t GetOceanTileCount() const { return m_ocean_tiles; }
        void SetOceanVerticesCount(const uint32_t count) { m_ocean_vertices_count = count; }
        uint32_t GetOceanVerticesCount() const { return m_ocean_vertices_count; }
        void SetOceanTileSize(const float size) { m_ocean_tile_size = size; }
        float GetOceanTileSize() const { return m_ocean_tile_size; }
        bool IsOcean() const { return GetProperty(MaterialProperty::IsOcean) == 1.0f; }
        void SetShowDiplacement(bool show) { m_show_displacement = show; }
        bool GetShowDisplacement() const { return m_show_displacement; }
        void SetShowSlope(bool show) { m_show_slope = show; }
        bool GetShowSlope() const { return m_show_slope; }
        void SetShowSynthesised(bool show) { m_show_synthesised = show; }
        bool GetShowSynthesised() const { return m_show_synthesised; }

        const std::array<float, static_cast<uint32_t>(MaterialProperty::Max)>& GetProperties() const { return m_properties; }
        const std::array<float, static_cast<uint32_t>(OceanParameters::Max)>& GetOceanProperties() const { return m_ocean_properties; }

    private:
        std::array<RHI_Texture*, static_cast<uint32_t>(MaterialTextureType::Max) * slots_per_texture> m_textures;
        std::array<float, static_cast<uint32_t>(MaterialProperty::Max)> m_properties;
        std::array<float, static_cast<uint32_t>(OceanParameters::Max)> m_ocean_properties;
        uint32_t m_index = 0;
        std::mutex m_mutex;

        bool m_should_compute_spectrum = true;
        bool m_show_displacement = false;
        bool m_show_slope = false;
        bool m_show_synthesised = false;
        uint32_t m_ocean_tiles = 1;
        uint32_t m_ocean_vertices_count = 0;
        float m_ocean_tile_size = 0.0f;
    };
}
