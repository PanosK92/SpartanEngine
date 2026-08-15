/*
Copyright(c) 2016-2026 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "D3D12_Internal.h"
#include "../core/Debugging.h"
#include <wrl/client.h>
#include <vector>
#include <cstdint>
//================================

// translates the legacy D3D12_RESOURCE_BARRIER descriptions that the command list builds into enhanced
// barriers, so all of the existing state tracking, dedup and compute queue masking is preserved while
// submission becomes a single batched ID3D12GraphicsCommandList7::Barrier call
// the legacy path stays reachable through Debugging::IsD3D12EnhancedBarriersEnabled
//
// the sync, access and layout mapping below is validated against the compatibility tables in the
// enhanced barriers spec, layout is derived from the already masked access so the two cannot disagree
//
// two interop prerequisites remain before this can be the default, the spec requires a subresource
// holding a non common legacy state to pass through common before an enhanced barrier references it
//   1. textures with initial data are created in copy_dest and depth textures in depth_write, see
//      RHI_Texture::RHI_CreateResource, so the first enhanced barrier on them crosses that boundary
//   2. nrd is initialised with disableD3D12EnhancedBarriers, see D3D12_VendorTechnology.cpp, so it
//      issues legacy barriers on textures this path already moved to an enhanced layout
// both surface as debug layer errors, so enable the validation layer when turning this on

namespace spartan::d3d12_barriers
{
    namespace
    {
        bool enabled = false;

        // legacy read states that have no dedicated enhanced layout, they resolve to generic_read
        constexpr D3D12_RESOURCE_STATES state_shader_read =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        // sync bits that a compute command list rejects
        constexpr D3D12_BARRIER_SYNC sync_graphics_only =
            D3D12_BARRIER_SYNC_DRAW           |
            D3D12_BARRIER_SYNC_INDEX_INPUT    |
            D3D12_BARRIER_SYNC_VERTEX_SHADING |
            D3D12_BARRIER_SYNC_PIXEL_SHADING  |
            D3D12_BARRIER_SYNC_DEPTH_STENCIL  |
            D3D12_BARRIER_SYNC_RENDER_TARGET  |
            D3D12_BARRIER_SYNC_RESOLVE;

        // access bits that a compute command list rejects
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

        // access bits that only mean something on a buffer, they are illegal in a texture barrier
        constexpr D3D12_BARRIER_ACCESS access_buffer_only =
            D3D12_BARRIER_ACCESS_VERTEX_BUFFER   |
            D3D12_BARRIER_ACCESS_CONSTANT_BUFFER |
            D3D12_BARRIER_ACCESS_INDEX_BUFFER    |
            D3D12_BARRIER_ACCESS_STREAM_OUTPUT   |
            D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT |
            D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
            D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;

        // access bits that only mean something on a texture, they are illegal in a buffer barrier
        constexpr D3D12_BARRIER_ACCESS access_texture_only =
            D3D12_BARRIER_ACCESS_RENDER_TARGET       |
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE |
            D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ  |
            D3D12_BARRIER_ACCESS_RESOLVE_DEST        |
            D3D12_BARRIER_ACCESS_RESOLVE_SOURCE      |
            D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;

        // legacy states are a bitmask, so every matching access bit has to accumulate
        D3D12_BARRIER_ACCESS state_to_access(D3D12_RESOURCE_STATES state)
        {
            // common and present are both zero, meaning any access compatible with the layout
            if (state == D3D12_RESOURCE_STATE_COMMON)
            {
                return D3D12_BARRIER_ACCESS_COMMON;
            }

            D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;

            if (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
            {
                access |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            }
            if (state & D3D12_RESOURCE_STATE_INDEX_BUFFER)
            {
                access |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
            }
            if (state & D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                access |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
            }
            if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                access |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            }
            if (state & D3D12_RESOURCE_STATE_DEPTH_WRITE)
            {
                access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
            }
            if (state & D3D12_RESOURCE_STATE_DEPTH_READ)
            {
                access |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
            }
            if (state & state_shader_read)
            {
                access |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            }
            if (state & D3D12_RESOURCE_STATE_STREAM_OUT)
            {
                access |= D3D12_BARRIER_ACCESS_STREAM_OUTPUT;
            }
            if (state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
            {
                access |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
            }
            if (state & D3D12_RESOURCE_STATE_COPY_DEST)
            {
                access |= D3D12_BARRIER_ACCESS_COPY_DEST;
            }
            if (state & D3D12_RESOURCE_STATE_COPY_SOURCE)
            {
                access |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
            }
            if (state & D3D12_RESOURCE_STATE_RESOLVE_DEST)
            {
                access |= D3D12_BARRIER_ACCESS_RESOLVE_DEST;
            }
            if (state & D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
            {
                access |= D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
            }
            if (state & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
            {
                access |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
                          D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            }
            if (state & D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE)
            {
                access |= D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
            }

            return access;
        }

        // pixel and non pixel shading are folded into all_shading when both are present, the runtime
        // treats all_shading as the superset and redundant stage bits are not worth the validation risk
        D3D12_BARRIER_SYNC state_to_sync(D3D12_RESOURCE_STATES state)
        {
            if (state == D3D12_RESOURCE_STATE_COMMON)
            {
                return D3D12_BARRIER_SYNC_ALL;
            }

            D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
            bool pixel_shading      = false;
            bool non_pixel_shading  = false;

            // constant and vertex buffers can be read by every shading stage
            if (state & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
            {
                pixel_shading     = true;
                non_pixel_shading = true;
            }
            if (state & D3D12_RESOURCE_STATE_INDEX_BUFFER)
            {
                sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
            }
            if (state & D3D12_RESOURCE_STATE_RENDER_TARGET)
            {
                sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
            }
            if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                pixel_shading     = true;
                non_pixel_shading = true;
            }
            if (state & (D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ))
            {
                sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
            }
            if (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            {
                pixel_shading = true;
            }
            if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            {
                non_pixel_shading = true;
            }
            if (state & D3D12_RESOURCE_STATE_STREAM_OUT)
            {
                sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
            }
            if (state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
            {
                sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
            }
            if (state & (D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE))
            {
                sync |= D3D12_BARRIER_SYNC_COPY;
            }
            if (state & (D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_RESOLVE_SOURCE))
            {
                sync |= D3D12_BARRIER_SYNC_RESOLVE;
            }
            if (state & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
            {
                sync |= D3D12_BARRIER_SYNC_RAYTRACING |
                        D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            }
            if (state & D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE)
            {
                pixel_shading = true;
            }

            if (pixel_shading && non_pixel_shading)
            {
                sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
            }
            else if (pixel_shading)
            {
                sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
            }
            else if (non_pixel_shading)
            {
                sync |= D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
            }

            // sync_none is only legal alongside no_access, fall back to a full barrier
            if (sync == D3D12_BARRIER_SYNC_NONE)
            {
                sync = D3D12_BARRIER_SYNC_ALL;
            }

            return sync;
        }

        // the layout is derived from the already masked access set so the two cannot disagree, every
        // specific layout permits exactly one access type which means a multi access read set has to
        // widen to a generic read layout
        // only the direct queue flavour of generic read covers depth read, shading rate and resolve
        // source, which is why the plain generic read layout is reserved for the compute queue
        D3D12_BARRIER_LAYOUT access_to_layout(D3D12_BARRIER_ACCESS access, D3D12_COMMAND_LIST_TYPE type)
        {
            // the copy queue only permits the common layout, and common permits both copy accesses
            if (type == D3D12_COMMAND_LIST_TYPE_COPY)
            {
                return D3D12_BARRIER_LAYOUT_COMMON;
            }

            // common and present share the value zero, and common access means any compatible access
            if (access == D3D12_BARRIER_ACCESS_COMMON)
            {
                return D3D12_BARRIER_LAYOUT_COMMON;
            }

            // writable layouts win, the resource still has to be writable afterwards
            if (access & D3D12_BARRIER_ACCESS_RENDER_TARGET)
            {
                return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
            }
            // this layout also permits depth read, so it covers the read plus write combination
            if (access & D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE)
            {
                return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
            }
            if (access & D3D12_BARRIER_ACCESS_UNORDERED_ACCESS)
            {
                return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
            }
            if (access & D3D12_BARRIER_ACCESS_COPY_DEST)
            {
                return D3D12_BARRIER_LAYOUT_COPY_DEST;
            }
            if (access & D3D12_BARRIER_ACCESS_RESOLVE_DEST)
            {
                return D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
            }

            // read only from here, a lone access maps to its dedicated layout
            switch (access)
            {
                case D3D12_BARRIER_ACCESS_SHADER_RESOURCE:     return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
                case D3D12_BARRIER_ACCESS_COPY_SOURCE:         return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
                case D3D12_BARRIER_ACCESS_RESOLVE_SOURCE:      return D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
                case D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE: return D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE;
                case D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ:  return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
                default: break;
            }

            if (type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ;
            }

            // the graphics only read bits are already masked off outside the direct queue, so what
            // remains is shader resource and copy source, both of which plain generic read permits
            return D3D12_BARRIER_LAYOUT_GENERIC_READ;
        }

        // defense in depth, the command list already masks legacy states for compute but the standalone
        // barrier sites do not, and an illegal bit here removes the device rather than raising an error
        void mask_for_queue(D3D12_COMMAND_LIST_TYPE type, D3D12_BARRIER_SYNC& sync, D3D12_BARRIER_ACCESS& access)
        {
            if (type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
            {
                sync   &= ~sync_graphics_only;
                access &= ~access_graphics_only;
            }
            else if (type == D3D12_COMMAND_LIST_TYPE_COPY)
            {
                sync   &= (D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY);
                access &= (D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE);
            }
            else
            {
                return;
            }

            if (sync == D3D12_BARRIER_SYNC_NONE)
            {
                sync = D3D12_BARRIER_SYNC_ALL;
            }
        }

        // a single flat subresource index, NumMipLevels of zero makes IndexOrFirstMipLevel an index
        D3D12_BARRIER_SUBRESOURCE_RANGE subresource_index(UINT subresource)
        {
            D3D12_BARRIER_SUBRESOURCE_RANGE range = {};
            range.IndexOrFirstMipLevel            = subresource;
            range.NumMipLevels                    = 0;
            return range;
        }
    }

    void Initialize()
    {
        enabled = d3d12_caps::IsEnhancedBarriersSupported() && Debugging::IsD3D12EnhancedBarriersEnabled();

        if (enabled)
        {
            SP_LOG_INFO("D3D12 barriers: enhanced");
        }
        else if (!d3d12_caps::IsEnhancedBarriersSupported())
        {
            SP_LOG_INFO("D3D12 barriers: legacy (enhanced barriers unsupported by the runtime or driver)");
        }
        else
        {
            SP_LOG_INFO("D3D12 barriers: legacy (disabled in Debugging)");
        }
    }

    bool IsEnabled()
    {
        return enabled;
    }

    void Submit(ID3D12GraphicsCommandList* cmd_list, const D3D12_RESOURCE_BARRIER* barriers, uint32_t count)
    {
        if (!cmd_list || !barriers || count == 0)
        {
            return;
        }

        if (!enabled)
        {
            // legacy submits one at a time, a batch containing a bad entry avs some debug layer and
            // driver combinations without identifying the offender
            for (uint32_t i = 0; i < count; i++)
            {
                cmd_list->ResourceBarrier(1, &barriers[i]);
            }
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> cmd_list_7;
        if (FAILED(cmd_list->QueryInterface(IID_PPV_ARGS(&cmd_list_7))))
        {
            for (uint32_t i = 0; i < count; i++)
            {
                cmd_list->ResourceBarrier(1, &barriers[i]);
            }
            return;
        }

        const D3D12_COMMAND_LIST_TYPE type = cmd_list->GetType();

        // reused across calls on the recording thread to keep the translation allocation free
        thread_local std::vector<D3D12_TEXTURE_BARRIER> texture_barriers;
        thread_local std::vector<D3D12_BUFFER_BARRIER>  buffer_barriers;
        thread_local std::vector<D3D12_GLOBAL_BARRIER>  global_barriers;
        texture_barriers.clear();
        buffer_barriers.clear();
        global_barriers.clear();

        for (uint32_t i = 0; i < count; i++)
        {
            const D3D12_RESOURCE_BARRIER& barrier = barriers[i];

            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                ID3D12Resource* resource = barrier.Transition.pResource;
                if (!resource || barrier.Transition.StateBefore == barrier.Transition.StateAfter)
                {
                    continue;
                }

                D3D12_BARRIER_SYNC   sync_before   = state_to_sync(barrier.Transition.StateBefore);
                D3D12_BARRIER_SYNC   sync_after    = state_to_sync(barrier.Transition.StateAfter);
                D3D12_BARRIER_ACCESS access_before = state_to_access(barrier.Transition.StateBefore);
                D3D12_BARRIER_ACCESS access_after  = state_to_access(barrier.Transition.StateAfter);

                mask_for_queue(type, sync_before, access_before);
                mask_for_queue(type, sync_after,  access_after);

                if (d3d12_state::IsBuffer(resource))
                {
                    // buffers have no layout, only the access scope matters
                    access_before &= ~access_texture_only;
                    access_after  &= ~access_texture_only;

                    D3D12_BUFFER_BARRIER buffer_barrier = {};
                    buffer_barrier.SyncBefore           = sync_before;
                    buffer_barrier.SyncAfter            = sync_after;
                    buffer_barrier.AccessBefore         = access_before;
                    buffer_barrier.AccessAfter          = access_after;
                    buffer_barrier.pResource            = resource;
                    buffer_barrier.Offset               = 0;
                    buffer_barrier.Size                 = UINT64_MAX;
                    buffer_barriers.push_back(buffer_barrier);
                    continue;
                }

                access_before &= ~access_buffer_only;
                access_after  &= ~access_buffer_only;

                const D3D12_BARRIER_LAYOUT layout_before = access_to_layout(access_before, type);
                const D3D12_BARRIER_LAYOUT layout_after  = access_to_layout(access_after,  type);

                D3D12_TEXTURE_BARRIER texture_barrier = {};
                texture_barrier.SyncBefore            = sync_before;
                texture_barrier.SyncAfter             = sync_after;
                texture_barrier.AccessBefore          = access_before;
                texture_barrier.AccessAfter           = access_after;
                texture_barrier.LayoutBefore          = layout_before;
                texture_barrier.LayoutAfter           = layout_after;
                texture_barrier.pResource             = resource;
                texture_barrier.Subresources          = subresource_index(barrier.Transition.Subresource);
                texture_barrier.Flags                 = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                texture_barriers.push_back(texture_barrier);
                continue;
            }

            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
            {
                ID3D12Resource* resource = barrier.UAV.pResource;

                D3D12_BARRIER_SYNC   sync   = D3D12_BARRIER_SYNC_ALL_SHADING;
                D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
                mask_for_queue(type, sync, access);

                // a null resource means every uav write has to drain, which is a global barrier
                if (!resource)
                {
                    D3D12_GLOBAL_BARRIER global_barrier = {};
                    global_barrier.SyncBefore           = sync;
                    global_barrier.SyncAfter            = sync;
                    global_barrier.AccessBefore         = access;
                    global_barrier.AccessAfter          = access;
                    global_barriers.push_back(global_barrier);
                    continue;
                }

                if (d3d12_state::IsBuffer(resource))
                {
                    D3D12_BUFFER_BARRIER buffer_barrier = {};
                    buffer_barrier.SyncBefore           = sync;
                    buffer_barrier.SyncAfter            = sync;
                    buffer_barrier.AccessBefore         = access;
                    buffer_barrier.AccessAfter          = access;
                    buffer_barrier.pResource            = resource;
                    buffer_barrier.Offset               = 0;
                    buffer_barrier.Size                 = UINT64_MAX;
                    buffer_barriers.push_back(buffer_barrier);
                    continue;
                }

                // layout is unchanged, this only orders the writes against the reads
                D3D12_TEXTURE_BARRIER texture_barrier = {};
                texture_barrier.SyncBefore            = sync;
                texture_barrier.SyncAfter             = sync;
                texture_barrier.AccessBefore          = access;
                texture_barrier.AccessAfter           = access;
                texture_barrier.LayoutBefore          = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
                texture_barrier.LayoutAfter           = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
                texture_barrier.pResource             = resource;
                texture_barrier.Subresources          = subresource_index(0);
                texture_barrier.Flags                 = D3D12_TEXTURE_BARRIER_FLAG_NONE;
                texture_barriers.push_back(texture_barrier);
                continue;
            }

            // aliasing barriers become a global barrier, enhanced barriers have no dedicated aliasing type
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
            {
                D3D12_GLOBAL_BARRIER global_barrier = {};
                global_barrier.SyncBefore           = D3D12_BARRIER_SYNC_ALL;
                global_barrier.SyncAfter            = D3D12_BARRIER_SYNC_ALL;
                global_barrier.AccessBefore         = D3D12_BARRIER_ACCESS_COMMON;
                global_barrier.AccessAfter          = D3D12_BARRIER_ACCESS_COMMON;
                global_barriers.push_back(global_barrier);
            }
        }

        D3D12_BARRIER_GROUP groups[3] = {};
        uint32_t group_count          = 0;

        if (!global_barriers.empty())
        {
            groups[group_count].Type            = D3D12_BARRIER_TYPE_GLOBAL;
            groups[group_count].NumBarriers     = static_cast<UINT32>(global_barriers.size());
            groups[group_count].pGlobalBarriers = global_barriers.data();
            group_count++;
        }
        if (!buffer_barriers.empty())
        {
            groups[group_count].Type            = D3D12_BARRIER_TYPE_BUFFER;
            groups[group_count].NumBarriers     = static_cast<UINT32>(buffer_barriers.size());
            groups[group_count].pBufferBarriers = buffer_barriers.data();
            group_count++;
        }
        if (!texture_barriers.empty())
        {
            groups[group_count].Type             = D3D12_BARRIER_TYPE_TEXTURE;
            groups[group_count].NumBarriers      = static_cast<UINT32>(texture_barriers.size());
            groups[group_count].pTextureBarriers = texture_barriers.data();
            group_count++;
        }

        if (group_count > 0)
        {
            cmd_list_7->Barrier(group_count, groups);
        }
    }
}
