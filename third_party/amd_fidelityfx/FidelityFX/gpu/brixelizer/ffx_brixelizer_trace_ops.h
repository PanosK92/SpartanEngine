// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef FFX_BRIXELIZER_TRACE_OPS_H
#define FFX_BRIXELIZER_TRACE_OPS_H

#include "../ffx_core.h"

#include "ffx_brixelizer_host_gpu_shared.h"

FfxFloat32x3 LoadCascadeAABBTreesFloat3(FfxUInt32 cascadeID, FfxUInt32 elementIndex);
FfxUInt32 LoadCascadeAABBTreesUInt(FfxUInt32 cascadeID, FfxUInt32 elementIndex);
FfxUInt32 LoadBricksAABB(FfxUInt32 elementIndex);
FfxBrixelizerCascadeInfo GetCascadeInfo(FfxUInt32 cascadeID);
FfxFloat32 SampleSDFAtlas(FfxFloat32x3 uvw);
FfxUInt32 LoadCascadeBrickMapArrayUniform(FfxUInt32 cascadeID, FfxUInt32 elementIndex);

#include "ffx_brixelizer_brick_common.h"

/// A structure encapsulating the parameters for a ray to be marched using Brixelizer.
///
/// @ingroup Brixelizer
struct FfxBrixelizerRayDesc {
    FfxUInt32    start_cascade_id; ///< The index of the most detailed cascade for ray traversal.
    FfxUInt32    end_cascade_id;   ///< The index of the least detailed cascade for ray traversal.
    FfxFloat32   t_min;            ///< The minimum distance at which to accept a hit.
    FfxFloat32   t_max;            ///< The maximum distance at which to accept a hit.
    FfxFloat32x3 origin;           ///< The origin of the ray.
    FfxFloat32x3 direction;        ///< The direction of the ray. This input should be normalized.
};

/// A structure encapsulating all data associated with a ray SDF hit.
///
/// @ingroup Brixelizer
struct FfxBrixelizerHitRaw {
    FfxFloat32   t;             ///< The distance from the ray origin to the hit.
    FfxUInt32    brick_id;      ///< The ID of a hit brick.
    FfxUInt32    uvwc;          ///< Packed UVW coordinates of the hit location. UVW coordinates are in brick space.
    FfxUInt32    iter_count;    ///< The count of iterations to find the intersection.
};

/// A structure encapsulating minimal data associated with a ray SDF hit.
///
/// @ingroup Brixelizer
struct FfxBrixelizerHit {
    FfxFloat32 t;             ///< The distance from the ray origin to the hit.
};

/// A structure encapsulating the distance to a ray hit and the normal of the surface hit.
///
/// @ingroup Brixelizer
struct FfxBrixelizerHitWithNormal {
    FfxFloat32   t;          ///< The distance from the ray origin to the hit.
    FfxFloat32x3 normal;     ///< The normal of the SDF surface at the hit location.
};

FfxFloat32 FfxBrixelizerGetIntersectCorner(FfxBrixelizerCascadeInfo CINFO, FfxFloat32x3 corner_sign, FfxFloat32x3 ray_cursor, FfxFloat32x3 ray_idirection, FfxFloat32 EPS, inout FfxUInt32x3 coord, FfxFloat32 voxel_k)
{
    FfxFloat32x3 relative_cascade_origin = CINFO.grid_min - ray_cursor;
    coord                                = FfxUInt32x3(-relative_cascade_origin / (CINFO.voxel_size * voxel_k));
    FfxFloat32x3 node_max                = relative_cascade_origin + (FfxFloat32x3(coord.x, coord.y, coord.z) + corner_sign) * (CINFO.voxel_size * voxel_k);
    FfxFloat32x3 tbot                    = ray_idirection * node_max;
    FfxFloat32  hit_max                  = min(tbot.x, min(tbot.y, tbot.z)) + EPS;
    return hit_max;
}

/// This function is used for running a ray query against the Brixelizer SDF acceleration structure.
/// The "raw" version returns the data immediately accessible from the SDF structure generated by a hit.
///
/// @param [in]  ray_desc   A structure encapsulating the parameters of the ray for ray traversal. See <c><i>FfxBrixelizerRayDesc</i></c>.
/// @param [out] hit        A structure of values to be filled in with details of any hit.
///
/// @retval
/// true                    The ray hit the SDF and hit data has been written to the <c><i>hit</i></c> parameter.
/// @retval
/// false                   The ray did not intersect the SDF and no hit data has been written.
///
/// @ingroup Brixelizer
FfxBoolean FfxBrixelizerTraverseRaw(in FfxBrixelizerRayDesc ray_desc, out FfxBrixelizerHitRaw hit)
{
    FfxUInt32 cascade_id = ray_desc.start_cascade_id;
    FfxUInt32 g_end_cascade = ray_desc.end_cascade_id;

    FfxFloat32x3 ray_origin     = ray_desc.origin;
    FfxFloat32   ray_t          = ray_desc.t_min;
    FfxFloat32x3 ray_direction  = ray_desc.direction;
    FfxFloat32x3 ray_idirection = FfxFloat32(1.0) / ray_desc.direction;
    FfxFloat32x3 corner_sign    = FfxFloat32x3(
        ray_direction.x > FfxFloat32(0.0) ? FfxFloat32(1.0) : FfxFloat32(0.0),
        ray_direction.y > FfxFloat32(0.0) ? FfxFloat32(1.0) : FfxFloat32(0.0),
        ray_direction.z > FfxFloat32(0.0) ? FfxFloat32(1.0) : FfxFloat32(0.0)
    );
    hit.iter_count           = 0;
    FfxUInt32 local_iter_cnt = 0;
    cascade_id               = ffxWaveMin(cascade_id);
    for (; cascade_id <= g_end_cascade; cascade_id++) {
        cascade_id               = ffxWaveReadLaneFirstU1(cascade_id);
        FfxBrixelizerCascadeInfo CINFO = GetCascadeInfo(cascade_id);
        const FfxFloat32 voxel_size = CINFO.voxel_size;
        local_iter_cnt           = 0;

        FfxFloat32 orig_ray_t = ray_t;

        FfxFloat32      EPS      = CINFO.voxel_size / FfxFloat32(1024.0);
        FfxFloat32      cascade_hit_min;
        FfxFloat32      cascade_hit_max;
        FfxFloat32x3    ray_cursor = ray_origin + ray_direction * ray_t;
        FfxFloat32      top_level_max;
        const FfxUInt32 ITER_LIMIT       = 32;
        FfxFloat32x3 cascade_aabb_min = LoadCascadeAABBTreesFloat3(cascade_id, (16 * 16 * 16) + (2 * 4 * 4 * 4 + 0) * 3);
        FfxFloat32x3 cascade_aabb_max = LoadCascadeAABBTreesFloat3(cascade_id, (16 * 16 * 16) + (2 * 4 * 4 * 4 + 1) * 3);

        // if the ray cursor isn't inside the current cascade skip to the next one
        if (!((CINFO.is_enabled > 0) && all(FFX_GREATER_THAN(ray_cursor, CINFO.grid_min)) && all(FFX_LESS_THAN(ray_cursor, CINFO.grid_max)))) {
            continue;
        }

        if (FfxBrixelizerIntersectAABB(ray_origin, ray_idirection, cascade_aabb_min, cascade_aabb_max,
                /* out */ cascade_hit_min,
                /* out */ cascade_hit_max)) {
            FfxFloat32 stamp_size = FfxFloat32(16.0);
            FfxUInt32  level      = 0;
            cascade_hit_max = min(cascade_hit_max, ray_desc.t_max);

            while (ray_t < cascade_hit_max) {
                hit.iter_count++;
                local_iter_cnt++;
                if (local_iter_cnt > ITER_LIMIT)
                    break;

                ray_cursor = ray_origin + ray_direction * ray_t;

                FfxFloat32x3 stamp_aabb_min;
                FfxFloat32x3 stamp_aabb_max;

                FfxUInt32x3 stamp_coord;
                FfxFloat32 stamp_hit_max = ray_t + FfxBrixelizerGetIntersectCorner(CINFO, corner_sign, ray_cursor, ray_idirection, EPS, /* inout */ stamp_coord, stamp_size);
                FfxUInt32  stamp_idx     = FfxBrixelizerFlattenPOT(stamp_coord, FfxUInt32(2) << level);
                if (level == 0) {
                    top_level_max  = stamp_hit_max;
                    stamp_aabb_min = LoadCascadeAABBTreesFloat3(cascade_id, (16 * 16 * 16) + (2 * stamp_idx + 0) * 3);
                    stamp_aabb_max = LoadCascadeAABBTreesFloat3(cascade_id, (16 * 16 * 16) + (2 * stamp_idx + 1) * 3);
                } else {
                    FfxUInt32 bottom_stamp_pack = LoadCascadeAABBTreesUInt(cascade_id, stamp_idx);
                    if (bottom_stamp_pack == FFX_BRIXELIZER_INVALID_BOTTOM_AABB_NODE) {
                        stamp_aabb_min = FFX_BROADCAST_FLOAT32X3(0.0);
                        stamp_aabb_max = FFX_BROADCAST_FLOAT32X3(0.0);
                    } else {
                        FfxUInt32x3  bottom_iaabb_min         = FfxBrixelizerUnflattenPOT(bottom_stamp_pack & 0x7fff, 5);
                        FfxUInt32x3  bottom_iaabb_max         = FfxBrixelizerUnflattenPOT((bottom_stamp_pack >> 16) & 0x7fff, 5);
                        FfxFloat32x3 bottom_stamp_world_coord = FfxFloat32x3(stamp_coord) * CINFO.voxel_size * FfxFloat32(4.0) + CINFO.grid_min;
                        stamp_aabb_min                        = bottom_stamp_world_coord + FfxFloat32x3(bottom_iaabb_min) * CINFO.voxel_size / FfxFloat32(8.0);
                        stamp_aabb_max                        = bottom_stamp_world_coord + FfxFloat32x3(bottom_iaabb_max + FFX_BROADCAST_UINT32X3(1)) * CINFO.voxel_size / FfxFloat32(8.0);
                    }
                }

                FfxFloat32 stamp_aabb_hit_max;
                FfxFloat32 stamp_aabb_hit_min;
                if (ffxAsUInt32(stamp_aabb_min.x) == ffxAsUInt32(stamp_aabb_max.x) || !FfxBrixelizerIntersectAABB(ray_origin, ray_idirection, stamp_aabb_min, stamp_aabb_max,
                        /* out */ stamp_aabb_hit_min,
                        /* out */ stamp_aabb_hit_max)) { // empty node
                    ray_t = stamp_hit_max;               // Advance the ray
                    if (level == 0) {
                        continue;
                    } else {
                        if (ray_t > top_level_max) {
                            level      = 0;
                            stamp_size = FfxFloat32(16.0);
                        }
                        continue;
                    }
                } else {
                    if (level == 0) {
                        level      = 1;
                        stamp_size = FfxFloat32(4.0);
                        continue;
                    } else {
                        stamp_aabb_hit_max = min(stamp_aabb_hit_max, ray_desc.t_max);

                        while (ray_t < stamp_aabb_hit_max) {
                            hit.iter_count++;
                            local_iter_cnt++;
                            if (local_iter_cnt > ITER_LIMIT)
                                break;

                            ray_cursor = ray_origin + ray_direction * ray_t;

                            FfxUInt32x3 voxel;
                            FfxFloat32 voxel_hit_max = FfxBrixelizerGetIntersectCorner(CINFO, corner_sign, ray_cursor, ray_idirection, EPS, /* inout */ voxel, FfxFloat32(1.0));
                            if (voxel_hit_max < EPS) {
                                ray_t = ray_t + voxel_hit_max;
                                break;
                            }
                            voxel_hit_max += ray_t;
                            FfxUInt32 brick_id = FfxBrixelizerLoadBrickIDUniform(FfxBrixelizerFlattenPOT(FfxBrixelizerWrapCoords(CINFO, voxel), 6), cascade_id);
                            if (brick_id == FFX_BRIXELIZER_UNINITIALIZED_ID) {
                                hit.iter_count = ITER_LIMIT + 1;
                                local_iter_cnt = ITER_LIMIT + 1;
                                break;
                            }
                            if (FfxBrixelizerIsValidID(brick_id)) {
                                FfxFloat32x3 voxel_min       = FfxFloat32x3(voxel.x, voxel.y, voxel.z) * CINFO.voxel_size + CINFO.grid_min;
                                FfxUInt32    brick_aabb_pack = LoadBricksAABB(FfxBrixelizerBrickGetIndex(brick_id));
                                FfxUInt32x3  brick_aabb_umin = FfxBrixelizerUnflattenPOT(brick_aabb_pack & ((1 << 9) - 1), 3);
                                FfxUInt32x3  brick_aabb_umax = FfxBrixelizerUnflattenPOT((brick_aabb_pack >> 9) & ((1 << 9) - 1), 3) + FFX_BROADCAST_UINT32X3(1);
                                FfxFloat32x3 brick_aabb_min  = voxel_min - FFX_BROADCAST_FLOAT32X3(CINFO.voxel_size / FfxFloat32(2.0 * 7.0)) + FfxFloat32x3(brick_aabb_umin) * (CINFO.voxel_size / FfxFloat32(7.0));
                                FfxFloat32x3 brick_aabb_max  = voxel_min - FFX_BROADCAST_FLOAT32X3(CINFO.voxel_size / FfxFloat32(2.0 * 7.0)) + FfxFloat32x3(brick_aabb_umax) * (CINFO.voxel_size / FfxFloat32(7.0));
                                FfxFloat32   brick_hit_min;
                                FfxFloat32   brick_hit_max;
                                if (FfxBrixelizerIntersectAABB(ray_cursor, ray_idirection, brick_aabb_min, brick_aabb_max,
                                        /* out */ brick_hit_min,
                                        /* out */ brick_hit_max)) {

                                    FfxFloat32x3 uvw          = (ray_cursor + brick_hit_min * ray_direction - voxel_min) * CINFO.ivoxel_size;
                                    FfxFloat32   dist         = FfxFloat32(1.0);
                                    FfxFloat32   total_dist   = 0.0f;
                                    FfxFloat32x3 brick_offset = FfxBrixelizerGetSDFAtlasOffset(brick_id);
                                    FfxFloat32x3 uvw_min      = (brick_offset + FFX_BROADCAST_FLOAT32X3(FfxFloat32(0.5))) / FfxFloat32(FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE);
                                    FfxFloat32x3 uvw_max      = uvw_min + FFX_BROADCAST_FLOAT32X3(FfxFloat32(7.0)) / FfxFloat32(FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE);
                                    for (FfxUInt32 i = 0; i < 8; i++) {
                                        hit.iter_count++;
                                        dist = FfxBrixelizerSampleBrixelDistance(uvw_min, uvw_max, uvw) - FFX_BRIXELIZER_TRAVERSAL_EPS;
                                        if (dist < FFX_BRIXELIZER_TRAVERSAL_EPS) {
                                            hit.t = ray_t + brick_hit_min + total_dist * voxel_size;
                                            if (hit.t > ray_desc.t_max) {
                                                return false;
                                            }

                                            hit.brick_id = brick_id;
                                            hit.uvwc     = PackUVWC(FfxFloat32x4(uvw, 0.0));
                                            return true;
                                        }
                                        uvw += ray_direction * dist;
                                        total_dist += dist;
                                        if (any(FFX_GREATER_THAN(abs(uvw - FFX_BROADCAST_FLOAT32X3(0.5)), FFX_BROADCAST_FLOAT32X3(0.501))))
                                            break;
                                    }
                                }
                            }
                            ray_t = voxel_hit_max;
                        }
                        if (ray_t > top_level_max) {
                            level      = 0;
                            stamp_size = FfxFloat32(16.0);
                        }
                        if (local_iter_cnt > ITER_LIMIT)
                            break;
                        ray_t = stamp_hit_max;
                        continue;
                    }
                }
            }
        }

        if (local_iter_cnt > ITER_LIMIT) {
            ray_t = max(orig_ray_t, ray_t - CINFO.voxel_size);
            continue;
        }

        // advance ray to end of current cascade
        FfxBrixelizerIntersectAABB(ray_origin, ray_idirection, CINFO.grid_min, CINFO.grid_max, /* out */ cascade_hit_min, /* out */ cascade_hit_max);
        ray_t = max(orig_ray_t, cascade_hit_max - CINFO.voxel_size);
    }

    return false;
}

/// Calculate a normal from a hit described by an <c><i>FfxBrixelizerHitRaw</i></c> structure.
///
/// @param [in] hit   A ray hit with the SDF returned by <c><i>FfxBrixelizerTraverseRaw</i></c>.
///
/// @retval           A normal to the hit described by the <c><i>hit</i></c> paramter.
///
/// @ingroup Brixelizer
FfxFloat32x3 FfxBrixelizerGetHitNormal(FfxBrixelizerHitRaw hit)
{
    FfxFloat32x3 uvw = FfxFloat32x3(
        FfxBrixelizerUnpackUnsigned8Bits((hit.uvwc >> 0) & 0xff),
        FfxBrixelizerUnpackUnsigned8Bits((hit.uvwc >> 8) & 0xff),
        FfxBrixelizerUnpackUnsigned8Bits((hit.uvwc >> 16) & 0xff)
    );
    uvw += FFX_BROADCAST_FLOAT32X3(1.0 / 512.0);
    FfxUInt32x3  brick_offset = FfxBrixelizerGetSDFAtlasOffset(hit.brick_id);
    FfxFloat32x3 uvw_min      = (FfxFloat32x3(brick_offset) + FFX_BROADCAST_FLOAT32X3(0.5)) / FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE;
    FfxFloat32x3 uvw_max      = (FfxFloat32x3(brick_offset) + FFX_BROADCAST_FLOAT32X3(float(8.0 - 0.5))) / FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE;
    return FfxBrixelizerGetBrixelGrad(uvw_min, uvw_max, uvw);
}

/// This function is used for running a ray query against the Brixelizer SDF acceleration structure.
/// This version simply returns the distance to a hit if a hit is encountered.
///
/// @param [in]  ray_desc   A structure encapsulating the parameters of the ray for ray traversal. See <c><i>FfxBrixelizerRayDesc</i></c>.
/// @param [out] hit        A structure of values to be filled in with details of any hit.
///
/// @retval
/// true                    The ray hit the SDF and hit data has been written to the <c><i>hit</i></c> parameter.
/// @retval
/// false                   The ray did not intersect the SDF and no hit data has been written.
///
/// @ingroup Brixelizer
FfxBoolean FfxBrixelizerTraverse(FfxBrixelizerRayDesc ray_desc, out FfxBrixelizerHit hit)
{
    FfxBrixelizerHitRaw raw_payload;
    FfxBoolean result = FfxBrixelizerTraverseRaw(ray_desc, raw_payload);
    hit.t = raw_payload.t;
    return result;
}

/// This function is used for running a ray query against the Brixelizer SDF acceleration structure.
/// This version returns the distance to a hit and a normal to the SDF geometry at a hit location when a hit
/// is encountered.
///
///
/// @param [in]  ray_desc   A structure encapsulating the parameters of the ray for ray traversal. See <c><i>FfxBrixelizerRayDesc</i></c>.
/// @param [out] hit        A structure of values to be filled in with details of any hit.
///
/// @retval
/// true                    The ray hit the SDF and hit data has been written to the <c><i>hit</i></c> parameter.
/// @retval
/// false                   The ray did not intersect the SDF and no hit data has been written.
///
/// @ingroup Brixelizer
FfxBoolean FfxBrixelizerTraverseWithNormal(FfxBrixelizerRayDesc ray_desc, out FfxBrixelizerHitWithNormal hit)
{
    FfxBrixelizerHitRaw raw_payload;
    FfxBoolean result = FfxBrixelizerTraverseRaw(ray_desc, raw_payload);
    if (!result) {
        return false;
    }
    hit.t = raw_payload.t;
    hit.normal = FfxBrixelizerGetHitNormal(raw_payload);
    return true;
}

#endif // FFX_BRIXELIZER_TRACE_OPS_H