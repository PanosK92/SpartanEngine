/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "D3D12_Internal.h"
#include "../../profiling/Profiler.h"
#include <wrl/client.h>
#include <vector>
#include <cstdint>
//================================

// native d3d12 enhanced barriers through ID3D12GraphicsCommandList7::Barrier
// buffers carry no layout and only synchronize through global/buffer memory barriers;
// textures track D3D12_BARRIER_LAYOUT and batch together with any global memory barrier into one call

namespace spartan::d3d12_barriers
{
    namespace
    {
        bool supported = false;

        constexpr D3D12_BARRIER_SYNC sync_graphics_only =
            D3D12_BARRIER_SYNC_DRAW           |
            D3D12_BARRIER_SYNC_INDEX_INPUT    |
            D3D12_BARRIER_SYNC_VERTEX_SHADING |
            D3D12_BARRIER_SYNC_PIXEL_SHADING  |
            D3D12_BARRIER_SYNC_DEPTH_STENCIL  |
            D3D12_BARRIER_SYNC_RENDER_TARGET  |
            D3D12_BARRIER_SYNC_RESOLVE;

        constexpr D3D12_BARRIER_ACCESS access_graphics_only =
            D3D12_BARRIER_ACCESS_VERTEX_BUFFER        |
            D3D12_BARRIER_ACCESS_INDEX_BUFFER         |
            D3D12_BARRIER_ACCESS_RENDER_TARGET        |
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE  |
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ   |
            D3D12_BARRIER_ACCESS_STREAM_OUTPUT        |
            D3D12_BARRIER_ACCESS_RESOLVE_DEST         |
            D3D12_BARRIER_ACCESS_RESOLVE_SOURCE       |
            D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;

        D3D12_BARRIER_ACCESS compatible_access_for_sync(D3D12_BARRIER_SYNC sync)
        {
            if (sync == D3D12_BARRIER_SYNC_NONE)
            {
                return D3D12_BARRIER_ACCESS_NO_ACCESS;
            }
            if (sync & D3D12_BARRIER_SYNC_ALL)
            {
                return static_cast<D3D12_BARRIER_ACCESS>(~0u);
            }

            D3D12_BARRIER_ACCESS allowed = D3D12_BARRIER_ACCESS_COMMON;
            if (sync & D3D12_BARRIER_SYNC_DRAW)
            {
                allowed |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_INDEX_BUFFER |
                           D3D12_BARRIER_ACCESS_RENDER_TARGET | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
                           D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_STREAM_OUTPUT |
                           D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_INDEX_INPUT)
            {
                allowed |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
            }
            if (sync & D3D12_BARRIER_SYNC_VERTEX_SHADING)
            {
                allowed |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                           D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_PIXEL_SHADING)
            {
                allowed |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                           D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_DEPTH_STENCIL)
            {
                allowed |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_RENDER_TARGET)
            {
                allowed |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
            }
            if (sync & (D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING))
            {
                allowed |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_SHADER_RESOURCE |
                           D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_ALL_SHADING)
            {
                allowed |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                           D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE |
                           D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_NON_PIXEL_SHADING)
            {
                allowed |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                           D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (sync & D3D12_BARRIER_SYNC_COPY)
            {
                allowed |= D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE;
            }
            if (sync & D3D12_BARRIER_SYNC_RESOLVE)
            {
                allowed |= D3D12_BARRIER_ACCESS_RESOLVE_DEST | D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
            }
            if (sync & D3D12_BARRIER_SYNC_EXECUTE_INDIRECT)
            {
                allowed |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
            }
            if (sync & D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW)
            {
                allowed |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            }
            if (sync & D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE)
            {
                allowed |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                           D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            }
            if (sync & D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE)
            {
                allowed |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            }
            if (sync & D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO)
            {
                allowed |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            }
            return allowed;
        }

        D3D12_BARRIER_SYNC compatible_sync_for_access(D3D12_BARRIER_ACCESS access)
        {
            if (access == D3D12_BARRIER_ACCESS_COMMON || access == D3D12_BARRIER_ACCESS_NO_ACCESS)
            {
                return static_cast<D3D12_BARRIER_SYNC>(~0u);
            }

            D3D12_BARRIER_SYNC allowed = D3D12_BARRIER_SYNC_ALL;
            if (access & D3D12_BARRIER_ACCESS_VERTEX_BUFFER)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
            }
            if (access & D3D12_BARRIER_ACCESS_CONSTANT_BUFFER)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
            }
            if (access & D3D12_BARRIER_ACCESS_INDEX_BUFFER)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_INDEX_INPUT;
            }
            if (access & D3D12_BARRIER_ACCESS_RENDER_TARGET)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_RENDER_TARGET;
            }
            if (access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW | D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                           D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
            }
            if (access & (D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ))
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_DEPTH_STENCIL;
            }
            if (access & D3D12_BARRIER_ACCESS_SHADER_RESOURCE)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            }
            if (access & D3D12_BARRIER_ACCESS_STREAM_OUTPUT)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING;
            }
            if (access & D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
            }
            if (access & (D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE))
            {
                allowed |= D3D12_BARRIER_SYNC_COPY;
            }
            if (access & (D3D12_BARRIER_ACCESS_RESOLVE_DEST | D3D12_BARRIER_ACCESS_RESOLVE_SOURCE))
            {
                allowed |= D3D12_BARRIER_SYNC_RESOLVE;
            }
            if (access & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                           D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
            }
            if (access & D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE)
            {
                allowed |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
            }
            if (access & D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE)
            {
                allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_PIXEL_SHADING | D3D12_BARRIER_SYNC_ALL_SHADING;
            }
            return allowed;
        }

        void mask_for_queue(D3D12_COMMAND_LIST_TYPE type, D3D12_BARRIER_SYNC& sync, D3D12_BARRIER_ACCESS& access)
        {
            if (sync == D3D12_BARRIER_SYNC_NONE)
            {
                access = D3D12_BARRIER_ACCESS_NO_ACCESS;
                return;
            }

            // SYNC_ALL must not be combined with other sync bits
            if (sync & D3D12_BARRIER_SYNC_ALL)
            {
                sync = D3D12_BARRIER_SYNC_ALL;
            }

            // NO_ACCESS must not be combined with real access bits
            if ((access & D3D12_BARRIER_ACCESS_NO_ACCESS) && access != D3D12_BARRIER_ACCESS_NO_ACCESS)
            {
                access &= ~D3D12_BARRIER_ACCESS_NO_ACCESS;
            }

            if (type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
            {
                if (sync != D3D12_BARRIER_SYNC_ALL)
                {
                    // ALL_SHADING and NON_PIXEL_SHADING include graphics stages rejected on a compute queue
                    if (sync & (D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING))
                    {
                        sync &= ~(D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING);
                        sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
                    }
                    sync &= ~sync_graphics_only;
                }
                if (access != D3D12_BARRIER_ACCESS_NO_ACCESS && access != D3D12_BARRIER_ACCESS_COMMON)
                {
                    access &= ~access_graphics_only;
                }
                if (sync == D3D12_BARRIER_SYNC_NONE)
                {
                    sync = (access == D3D12_BARRIER_ACCESS_NO_ACCESS) ? D3D12_BARRIER_SYNC_NONE : D3D12_BARRIER_SYNC_ALL;
                }
            }
            else if (type == D3D12_COMMAND_LIST_TYPE_COPY)
            {
                sync &= (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY);
                if (access != D3D12_BARRIER_ACCESS_NO_ACCESS && access != D3D12_BARRIER_ACCESS_COMMON)
                {
                    access &= (D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE);
                }
                if (sync == D3D12_BARRIER_SYNC_NONE && access != D3D12_BARRIER_ACCESS_NO_ACCESS)
                {
                    sync = D3D12_BARRIER_SYNC_COPY;
                }
            }

            // ACCESS_COMMON requires SYNC_ALL; otherwise guarantee sync and access are mutually compatible
            if (access == D3D12_BARRIER_ACCESS_COMMON)
            {
                sync = D3D12_BARRIER_SYNC_ALL;
            }
            else if (access != D3D12_BARRIER_ACCESS_NO_ACCESS && sync != D3D12_BARRIER_SYNC_ALL)
            {
                const D3D12_BARRIER_ACCESS compatible_access = compatible_access_for_sync(sync);
                if ((access & ~compatible_access) != 0)
                {
                    sync   = D3D12_BARRIER_SYNC_ALL;
                    access = D3D12_BARRIER_ACCESS_COMMON;
                }
                else
                {
                    const D3D12_BARRIER_SYNC compatible_sync = compatible_sync_for_access(access);
                    if ((sync & ~compatible_sync) != 0)
                    {
                        sync &= compatible_sync;
                        if (sync == D3D12_BARRIER_SYNC_NONE)
                        {
                            sync   = D3D12_BARRIER_SYNC_ALL;
                            access = D3D12_BARRIER_ACCESS_COMMON;
                        }
                    }
                }
            }
        }

        void mask_texture_barrier(D3D12_COMMAND_LIST_TYPE type, D3D12_TEXTURE_BARRIER& barrier)
        {
            if (type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
            {
                if (barrier.LayoutBefore == D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ)
                {
                    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_GENERIC_READ;
                }
                if (barrier.LayoutAfter == D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ)
                {
                    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_GENERIC_READ;
                }
            }
            else if (type == D3D12_COMMAND_LIST_TYPE_COPY)
            {
                if (barrier.LayoutBefore != D3D12_BARRIER_LAYOUT_UNDEFINED)
                {
                    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
                }
                barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
            }

            mask_for_queue(type, barrier.SyncBefore, barrier.AccessBefore);
            mask_for_queue(type, barrier.SyncAfter,  barrier.AccessAfter);

            // UNDEFINED only allows NO_ACCESS; COMMON/PRESENT allow COMMON or NO_ACCESS
            if (barrier.LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED)
            {
                barrier.SyncBefore   = D3D12_BARRIER_SYNC_NONE;
                barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
            }
            if (barrier.LayoutAfter == D3D12_BARRIER_LAYOUT_UNDEFINED)
            {
                barrier.SyncAfter   = D3D12_BARRIER_SYNC_NONE;
                barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
            }
        }

        D3D12_BARRIER_SUBRESOURCE_RANGE make_subresource_range(UINT subresource)
        {
            D3D12_BARRIER_SUBRESOURCE_RANGE range = {};
            range.IndexOrFirstMipLevel            = subresource;
            range.NumMipLevels                    = 0;
            return range;
        }
    }

    void Initialize()
    {
        supported = d3d12_caps::IsEnhancedBarriersSupported();
        if (supported)
        {
            SP_LOG_INFO("D3D12 barriers: enhanced (native ID3D12GraphicsCommandList7::Barrier)");
        }
        else
        {
            SP_LOG_ERROR("D3D12 enhanced barriers are not supported by the active runtime or driver");
        }
    }

    bool IsSupported()
    {
        return supported;
    }

    D3D12_BARRIER_SYNC LayoutDefaultSync(D3D12_BARRIER_LAYOUT layout)
    {
        switch (layout)
        {
            case D3D12_BARRIER_LAYOUT_UNDEFINED:                return D3D12_BARRIER_SYNC_NONE;
            case D3D12_BARRIER_LAYOUT_COMMON:                   return D3D12_BARRIER_SYNC_ALL; // also D3D12_BARRIER_LAYOUT_PRESENT
            case D3D12_BARRIER_LAYOUT_RENDER_TARGET:            return D3D12_BARRIER_SYNC_RENDER_TARGET;
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:       return D3D12_BARRIER_SYNC_DEPTH_STENCIL;
            case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:         return D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
            case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:          return D3D12_BARRIER_SYNC_ALL_SHADING;
            case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
            case D3D12_BARRIER_LAYOUT_COPY_DEST:                return D3D12_BARRIER_SYNC_COPY;
            case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
            case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:             return D3D12_BARRIER_SYNC_RESOLVE;
            case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:      return D3D12_BARRIER_SYNC_PIXEL_SHADING;
            case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:return D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_COPY;
            case D3D12_BARRIER_LAYOUT_GENERIC_READ:             return D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_COPY;
            default:                                            return D3D12_BARRIER_SYNC_ALL;
        }
    }

    D3D12_BARRIER_ACCESS LayoutDefaultAccess(D3D12_BARRIER_LAYOUT layout)
    {
        switch (layout)
        {
            case D3D12_BARRIER_LAYOUT_UNDEFINED:                return D3D12_BARRIER_ACCESS_NO_ACCESS;
            case D3D12_BARRIER_LAYOUT_COMMON:                   return D3D12_BARRIER_ACCESS_COMMON; // also D3D12_BARRIER_LAYOUT_PRESENT
            case D3D12_BARRIER_LAYOUT_RENDER_TARGET:            return D3D12_BARRIER_ACCESS_RENDER_TARGET;
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:      return D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:       return D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
            case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:         return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:          return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            case D3D12_BARRIER_LAYOUT_COPY_SOURCE:              return D3D12_BARRIER_ACCESS_COPY_SOURCE;
            case D3D12_BARRIER_LAYOUT_COPY_DEST:                return D3D12_BARRIER_ACCESS_COPY_DEST;
            case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:           return D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
            case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:             return D3D12_BARRIER_ACCESS_RESOLVE_DEST;
            case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:      return D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
            case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:return D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_COPY_SOURCE;
            case D3D12_BARRIER_LAYOUT_GENERIC_READ:             return D3D12_BARRIER_ACCESS_SHADER_RESOURCE | D3D12_BARRIER_ACCESS_COPY_SOURCE;
            default:                                            return D3D12_BARRIER_ACCESS_COMMON;
        }
    }

    bool IsDirectOnlyLayout(D3D12_BARRIER_LAYOUT layout)
    {
        switch (layout)
        {
            case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
            case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
            case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
            case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
            case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
            case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
                return true;
            default:
                return false;
        }
    }

    void Submit(
        ID3D12GraphicsCommandList*   cmd_list,
        const D3D12_GLOBAL_BARRIER*  globals,
        uint32_t                     global_count,
        const D3D12_BUFFER_BARRIER*  buffers,
        uint32_t                     buffer_count,
        const D3D12_TEXTURE_BARRIER* textures,
        uint32_t                     texture_count,
        ID3D12GraphicsCommandList7*  cmd_list_7
    )
    {
        if (!cmd_list || (global_count == 0 && buffer_count == 0 && texture_count == 0))
        {
            return;
        }

        if (!cmd_list_7)
        {
            cmd_list_7 = static_cast<ID3D12GraphicsCommandList7*>(cmd_list);
        }

        const D3D12_COMMAND_LIST_TYPE type = cmd_list->GetType();

        thread_local std::vector<D3D12_GLOBAL_BARRIER>  masked_globals;
        thread_local std::vector<D3D12_BUFFER_BARRIER>  masked_buffers;
        thread_local std::vector<D3D12_TEXTURE_BARRIER> masked_textures;
        masked_globals.clear();
        masked_buffers.clear();
        masked_textures.clear();

        if (global_count > 0 && globals)
        {
            masked_globals.reserve(global_count);
            for (uint32_t i = 0; i < global_count; i++)
            {
                D3D12_GLOBAL_BARRIER b = globals[i];
                mask_for_queue(type, b.SyncBefore, b.AccessBefore);
                mask_for_queue(type, b.SyncAfter,  b.AccessAfter);
                // D3D12 requires ACCESS_COMMON to be paired with ACCESS_COMMON (or NO_ACCESS) on global barriers
                if (b.AccessBefore == D3D12_BARRIER_ACCESS_COMMON && b.AccessAfter != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessAfter != D3D12_BARRIER_ACCESS_COMMON)
                {
                    b.SyncAfter   = D3D12_BARRIER_SYNC_ALL;
                    b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                }
                else if (b.AccessAfter == D3D12_BARRIER_ACCESS_COMMON && b.AccessBefore != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessBefore != D3D12_BARRIER_ACCESS_COMMON)
                {
                    b.SyncBefore   = D3D12_BARRIER_SYNC_ALL;
                    b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
                }
                masked_globals.push_back(b);
            }
        }

        if (buffer_count > 0 && buffers)
        {
            masked_buffers.reserve(buffer_count);
            for (uint32_t i = 0; i < buffer_count; i++)
            {
                if (!buffers[i].pResource)
                {
                    continue;
                }
                D3D12_BUFFER_BARRIER b = buffers[i];
                const D3D12_RESOURCE_FLAGS res_flags = b.pResource->GetDesc().Flags;
                D3D12_BARRIER_ACCESS disallowed = D3D12_BARRIER_ACCESS_COMMON;
                if ((res_flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
                {
                    disallowed |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
                }
                if ((res_flags & D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) == 0)
                {
                    disallowed |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
                }
                if ((res_flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
                {
                    disallowed |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
                }
                if (disallowed != D3D12_BARRIER_ACCESS_COMMON)
                {
                    if (b.AccessBefore != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessBefore != D3D12_BARRIER_ACCESS_COMMON)
                    {
                        b.AccessBefore &= ~disallowed;
                        if (b.AccessBefore == D3D12_BARRIER_ACCESS_COMMON)
                        {
                            b.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
                        }
                    }
                    if (b.AccessAfter != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessAfter != D3D12_BARRIER_ACCESS_COMMON)
                    {
                        b.AccessAfter &= ~disallowed;
                        if (b.AccessAfter == D3D12_BARRIER_ACCESS_COMMON)
                        {
                            b.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
                        }
                    }
                }
                mask_for_queue(type, b.SyncBefore, b.AccessBefore);
                mask_for_queue(type, b.SyncAfter,  b.AccessAfter);
                if (b.AccessBefore == D3D12_BARRIER_ACCESS_COMMON && b.AccessAfter != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessAfter != D3D12_BARRIER_ACCESS_COMMON)
                {
                    b.SyncAfter   = D3D12_BARRIER_SYNC_ALL;
                    b.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
                }
                else if (b.AccessAfter == D3D12_BARRIER_ACCESS_COMMON && b.AccessBefore != D3D12_BARRIER_ACCESS_NO_ACCESS && b.AccessBefore != D3D12_BARRIER_ACCESS_COMMON)
                {
                    b.SyncBefore   = D3D12_BARRIER_SYNC_ALL;
                    b.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
                }
                masked_buffers.push_back(b);
            }
        }

        if (texture_count > 0 && textures)
        {
            masked_textures.reserve(texture_count);
            for (uint32_t i = 0; i < texture_count; i++)
            {
                if (!textures[i].pResource)
                {
                    continue;
                }
                D3D12_TEXTURE_BARRIER b = textures[i];
                mask_texture_barrier(type, b);
                if (b.LayoutBefore != b.LayoutAfter)
                {
                    Profiler::m_rhi_layout_barriers++;
                }
                masked_textures.push_back(b);
            }
        }

        D3D12_BARRIER_GROUP groups[3] = {};
        uint32_t group_count          = 0;

        if (!masked_globals.empty())
        {
            groups[group_count].Type            = D3D12_BARRIER_TYPE_GLOBAL;
            groups[group_count].NumBarriers     = static_cast<UINT32>(masked_globals.size());
            groups[group_count].pGlobalBarriers = masked_globals.data();
            group_count++;
        }
        if (!masked_buffers.empty())
        {
            groups[group_count].Type            = D3D12_BARRIER_TYPE_BUFFER;
            groups[group_count].NumBarriers     = static_cast<UINT32>(masked_buffers.size());
            groups[group_count].pBufferBarriers = masked_buffers.data();
            group_count++;
        }
        if (!masked_textures.empty())
        {
            groups[group_count].Type             = D3D12_BARRIER_TYPE_TEXTURE;
            groups[group_count].NumBarriers      = static_cast<UINT32>(masked_textures.size());
            groups[group_count].pTextureBarriers = masked_textures.data();
            group_count++;
        }

        if (group_count > 0)
        {
            cmd_list_7->Barrier(group_count, groups);
            Profiler::m_rhi_pipeline_barriers++;
        }
    }

    void SubmitGlobal(
        ID3D12GraphicsCommandList* cmd_list,
        D3D12_BARRIER_SYNC         sync_before,
        D3D12_BARRIER_SYNC         sync_after,
        D3D12_BARRIER_ACCESS       access_before,
        D3D12_BARRIER_ACCESS       access_after
    )
    {
        D3D12_GLOBAL_BARRIER barrier = {};
        barrier.SyncBefore           = sync_before;
        barrier.SyncAfter            = sync_after;
        barrier.AccessBefore         = access_before;
        barrier.AccessAfter          = access_after;
        Submit(cmd_list, &barrier, 1, nullptr, 0, nullptr, 0);
    }

    void SubmitBuffer(
        ID3D12GraphicsCommandList* cmd_list,
        ID3D12Resource*            resource,
        D3D12_BARRIER_SYNC         sync_before,
        D3D12_BARRIER_SYNC         sync_after,
        D3D12_BARRIER_ACCESS       access_before,
        D3D12_BARRIER_ACCESS       access_after
    )
    {
        if (!resource)
        {
            return;
        }
        D3D12_BUFFER_BARRIER barrier = {};
        barrier.SyncBefore           = sync_before;
        barrier.SyncAfter            = sync_after;
        barrier.AccessBefore         = access_before;
        barrier.AccessAfter          = access_after;
        barrier.pResource            = resource;
        barrier.Offset               = 0;
        barrier.Size                 = UINT64_MAX;
        Submit(cmd_list, nullptr, 0, &barrier, 1, nullptr, 0);
    }

    void SubmitTexture(
        ID3D12GraphicsCommandList* cmd_list,
        ID3D12Resource*            resource,
        D3D12_BARRIER_LAYOUT       layout_before,
        D3D12_BARRIER_LAYOUT       layout_after,
        UINT                       subresource
    )
    {
        if (!resource || layout_before == layout_after)
        {
            return;
        }
        D3D12_TEXTURE_BARRIER barrier = {};
        barrier.SyncBefore            = LayoutDefaultSync(layout_before);
        barrier.SyncAfter             = LayoutDefaultSync(layout_after);
        barrier.AccessBefore          = LayoutDefaultAccess(layout_before);
        barrier.AccessAfter           = LayoutDefaultAccess(layout_after);
        barrier.LayoutBefore          = layout_before;
        barrier.LayoutAfter           = layout_after;
        barrier.pResource             = resource;
        barrier.Subresources          = make_subresource_range(subresource);
        barrier.Flags                 = D3D12_TEXTURE_BARRIER_FLAG_NONE;
        Submit(cmd_list, nullptr, 0, nullptr, 0, &barrier, 1);
    }
}
