/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include "Terrain.h"
#include "../Entity.h"
#include "../../rhi/RHI_Texture.h"
//=================================

// oriented box and pad math shared by the terrain translation units
namespace spartan::terrain_common
{
    inline float obb_outside_distance(
        float x,
        float z,
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw
    )
    {
        const float c  = cosf(yaw);
        const float s  = sinf(yaw);
        const float dx = x - center_x;
        const float dz = z - center_z;
        const float lx =  dx * c + dz * s;
        const float lz = -dx * s + dz * c;
        const float ox = std::max(fabsf(lx) - half_x, 0.0f);
        const float oz = std::max(fabsf(lz) - half_z, 0.0f);
        return sqrtf(ox * ox + oz * oz);
    }

    inline void obb_write_aabb(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw,
        float& min_x,
        float& min_z,
        float& max_x,
        float& max_z
    )
    {
        const float c = cosf(yaw);
        const float s = sinf(yaw);
        min_x = std::numeric_limits<float>::max();
        min_z = std::numeric_limits<float>::max();
        max_x = -std::numeric_limits<float>::max();
        max_z = -std::numeric_limits<float>::max();
        const float signs[2] = { -1.0f, 1.0f };
        for (float sx : signs)
        {
            for (float sz : signs)
            {
                const float x = center_x + (c * sx * half_x) - (s * sz * half_z);
                const float z = center_z + (s * sx * half_x) + (c * sz * half_z);
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_z = std::min(min_z, z);
                max_z = std::max(max_z, z);
            }
        }
    }

    inline float pad_deform_margin(const TerrainPlatform& pad, float cell)
    {
        return std::max(std::max(pad.margin, 0.0f), std::max(cell, 1.0f));
    }

    inline void pad_deform_extents(
        const TerrainPlatform& pad,
        float cell,
        float& half_x,
        float& half_z
    )
    {
        const float extra = pad_deform_margin(pad, cell);
        half_x = pad.half_x + extra;
        half_z = pad.half_z + extra;
    }

    inline void platform_to_local(
        Entity* terrain_entity,
        const TerrainPlatform& pad,
        float& local_cx,
        float& local_cz,
        float& local_hx,
        float& local_hz,
        float& local_yaw,
        float& local_height
    )
    {
        local_cx     = pad.center_x;
        local_cz     = pad.center_z;
        local_hx     = pad.half_x;
        local_hz     = pad.half_z;
        local_yaw    = pad.yaw;
        local_height = pad.height;
        if (!terrain_entity)
        {
            return;
        }

        const math::Matrix inverse = terrain_entity->GetMatrix().Inverted();
        const math::Vector3 local_center = inverse * math::Vector3(pad.center_x, pad.height, pad.center_z);
        local_cx     = local_center.x;
        local_cz     = local_center.z;
        local_height = local_center.y;

        const math::Vector3 world_axis(pad.center_x + cosf(pad.yaw), pad.height, pad.center_z + sinf(pad.yaw));
        const math::Vector3 local_axis = inverse * world_axis - local_center;
        local_yaw = atan2f(local_axis.z, local_axis.x);

        const math::Vector3 world_x(
            pad.center_x + cosf(pad.yaw) * pad.half_x,
            pad.height,
            pad.center_z + sinf(pad.yaw) * pad.half_x
        );
        const math::Vector3 world_z(
            pad.center_x - sinf(pad.yaw) * pad.half_z,
            pad.height,
            pad.center_z + cosf(pad.yaw) * pad.half_z
        );
        local_hx = (inverse * world_x - local_center).Length();
        local_hz = (inverse * world_z - local_center).Length();
    }

    // a single mip, rhi_texture::prepareforgpu box downsamples the rest because the analysis
    // maps qualify as material textures, building a chain here would append a second one on
    // top of that and push the mip count past rhi_max_mip_count
    inline std::vector<RHI_Texture_Slice> to_single_mip_slice(const std::vector<uint8_t>& pixels)
    {
        std::vector<RHI_Texture_Slice> slices(1);
        slices[0].mips.resize(1);
        slices[0].mips[0].bytes.resize(pixels.size());
        memcpy(slices[0].mips[0].bytes.data(), pixels.data(), pixels.size());

        return slices;
    }
}
