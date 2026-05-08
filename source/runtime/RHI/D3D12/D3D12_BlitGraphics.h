/*
Copyright(c) 2015-2026 Panos Karabelas

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

#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

// internal scaling-blit support, mirrors the vkCmdBlitImage scaling semantics on the d3d12 backend
// the rhi command list calls into this when the source and destination dimensions differ, since copyresource and
// copytextureregion can not scale, the implementation runs a fullscreen triangle that samples the source srv and
// outputs to either an rtv or sv_depth depending on the destination format
namespace spartan::d3d12_blit
{
    struct BlitParams
    {
        D3D12_CPU_DESCRIPTOR_HANDLE source_srv_cpu_handle;
        D3D12_CPU_DESCRIPTOR_HANDLE destination_rtv_handle;
        D3D12_CPU_DESCRIPTOR_HANDLE destination_dsv_handle;
        DXGI_FORMAT                 destination_format;
        uint32_t                    destination_width;
        uint32_t                    destination_height;
        float                       source_uv_scale_x;
        float                       source_uv_scale_y;
        bool                        is_depth_destination;
    };

    void initialize();
    void blit(ID3D12GraphicsCommandList* cmd_list, const BlitParams& params);
}
