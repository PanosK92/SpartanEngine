/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>

// mirrors vkCmdBlitImage scaling on d3d12 with a fullscreen triangle, since copyresource cannot scale
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
