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

//= INCLUDES ==========
#include "pch.h"
#include "RHI_Device.h"
//=====================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // device properties
    float RHI_Device::m_timestamp_period                            = 0;
    uint64_t RHI_Device::m_min_uniform_buffer_offset_alignment      = 0;
    uint64_t RHI_Device::m_min_storage_buffer_offset_alignment      = 0;
    uint64_t RHI_Device::m_min_acceleration_buffer_offset_alignment = 0;
    uint32_t RHI_Device::m_max_texture_1d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_2d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_3d_dimension                 = 0;
    uint32_t RHI_Device::m_max_texture_cube_dimension               = 0;
    uint32_t RHI_Device::m_max_texture_array_layers                 = 0;
    uint32_t RHI_Device::m_max_push_constant_size                   = 0;
    uint32_t RHI_Device::m_max_shading_rate_texel_size_x            = 0;
    uint32_t RHI_Device::m_max_shading_rate_texel_size_y            = 0;
    uint64_t RHI_Device::m_optimal_buffer_copy_offset_alignment     = 0;
    uint32_t RHI_Device::m_shader_group_handle_size                 = 0;
    uint32_t RHI_Device::m_shader_group_handle_alignment            = 0;
    uint32_t RHI_Device::m_shader_group_base_alignment              = 0;
    bool RHI_Device::m_is_shading_rate_supported                    = false;
    bool RHI_Device::m_xess_supported                               = false;
    bool RHI_Device::m_dlss_supported                               = false;
    bool RHI_Device::m_is_ray_tracing_supported                     = false;
    bool RHI_Device::m_is_mesh_shaders_supported                    = false;
    void (*RHI_Device::m_pipeline_bound_callback)(RHI_CommandList*) = nullptr;
    uint32_t (*RHI_Device::m_scale_dimension_callback)(uint32_t, float) = nullptr;
    RHI_Buffer* RHI_Device::m_dummy_vertex_buffer                   = nullptr;

    // misc
    bool RHI_Device::m_wide_lines                = false;
    bool RHI_Device::m_device_lost               = false;
    uint32_t RHI_Device::m_physical_device_index = 0;
    vector<RHI_PhysicalDevice> physical_devices;

    void RHI_Device::PhysicalDeviceRegister(const RHI_PhysicalDevice& physical_device)
    {
        // discrete devices come first
        vector<RHI_PhysicalDevice>::const_iterator iter = find_if(physical_devices.begin(), physical_devices.end(), [](const RHI_PhysicalDevice& device)
        {
            return device.GetType() != RHI_PhysicalDevice_Type::Discrete;
        });

        physical_devices.emplace(iter, physical_device);

        // sort devices by memory, in an ascending order, the type order will be maintained
        sort(physical_devices.begin(), physical_devices.end(), [](const RHI_PhysicalDevice& adapter1, const RHI_PhysicalDevice& adapter2)
        {
            return adapter1.GetMemory() > adapter2.GetMemory() && adapter1.GetType() == adapter2.GetType();
        });

        SP_LOG_INFO("%s (%d MB)", physical_device.GetName(), physical_device.GetMemory());
    }

    RHI_PhysicalDevice* RHI_Device::GetPrimaryPhysicalDevice()
    {
        SP_ASSERT_MSG(physical_devices.size() != 0, "No physical devices detected");
        SP_ASSERT_MSG(m_physical_device_index < physical_devices.size(), "Index out of bounds");

        return &physical_devices[m_physical_device_index];
    }

    void RHI_Device::PhysicalDeviceSetPrimary(const uint32_t index)
    {
        m_physical_device_index = index;

        if (const RHI_PhysicalDevice* physical_device = GetPrimaryPhysicalDevice())
        {
            SP_LOG_INFO("%s (%d MB)", physical_device->GetName(), physical_device->GetMemory());
        }
    }
 
    vector<RHI_PhysicalDevice>& RHI_Device::PhysicalDeviceGet()
    {
        return physical_devices;
    }

    bool RHI_Device::IsValidResolution(const uint32_t width, const uint32_t height)
    {
        return width  >= 4 && width  <= m_max_texture_2d_dimension &&
               height >= 4 && height <= m_max_texture_2d_dimension;
    }

    void RHI_Device::SetPipelineBoundCallback(void (*callback)(RHI_CommandList*))
    {
        m_pipeline_bound_callback = callback;
    }

    void RHI_Device::InvokePipelineBound(RHI_CommandList* cmd_list)
    {
        if (m_pipeline_bound_callback)
        {
            m_pipeline_bound_callback(cmd_list);
        }
    }

    void RHI_Device::SetScaleDimensionCallback(uint32_t (*callback)(uint32_t, float))
    {
        m_scale_dimension_callback = callback;
    }

    uint32_t RHI_Device::ScaleDimension(uint32_t dimension, float scale)
    {
        if (m_scale_dimension_callback)
        {
            return m_scale_dimension_callback(dimension, scale);
        }

        return dimension;
    }

    void RHI_Device::SetDummyVertexBuffer(RHI_Buffer* buffer)
    {
        m_dummy_vertex_buffer = buffer;
    }

    RHI_Buffer* RHI_Device::GetDummyVertexBuffer()
    {
        return m_dummy_vertex_buffer;
    }
}
