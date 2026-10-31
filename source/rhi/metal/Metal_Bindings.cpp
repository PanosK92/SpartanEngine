/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "../RHI_Device.h"
#include "../RHI_Buffer.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include "../rendering/Renderer_Buffers.h"
#include "Metal_Internal.h"
#include <metal_irconverter/metal_irconverter.h>
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // first entry of each bindless table in the bindless buffer, the samplers table holds compare samplers first
        const uint32_t samplers_compare_count = 64;
        const uint32_t samplers_count         = 64;
        const uint32_t bindless_first[metal_bindings::parameter_count] =
        {
            0, 0, 0, 0,                   // root descriptor, constants and the per pass tables live elsewhere
            0,                            // material_textures
            rhi_max_array_size,           // material_parameters
            rhi_max_array_size + 1,       // light_parameters
            rhi_max_array_size + 2,       // aabbs
            rhi_max_array_size + 3,       // draw_data
            rhi_max_array_size + 4,       // geometry, vertices, indices and instances
            rhi_max_array_size + 7        // samplers
        };
        const uint32_t bindless_count = rhi_max_array_size + 7 + samplers_compare_count + samplers_count;

        IRRootSignature* root_signature                          = nullptr;
        array<uint32_t, metal_bindings::parameter_count> offsets = {};
        uint32_t top_level_size                                  = 0;

        // written in place, a table is rewritten only when the renderer replaces what it points to
        MTL::Buffer* bindless = nullptr;
        mutex mutex_bindless;

        // the first bytes of the bindless buffer are never a descriptor, they stay zero for vertex inputs nothing feeds
        const uint32_t zero_size = 16;

        metal_bindings::Descriptor* bindless_entries(const metal_bindings::Parameter parameter)
        {
            return reinterpret_cast<metal_bindings::Descriptor*>(static_cast<uint8_t*>(bindless->contents()) + zero_size) + bindless_first[parameter];
        }

        void write_structured_buffer(const metal_bindings::Parameter parameter, const uint32_t index, RHI_Buffer* buffer)
        {
            if (!buffer || !buffer->GetRhiResource())
                return;

            lock_guard<mutex> lock(mutex_bindless);
            bindless_entries(parameter)[index] = metal_bindings::buffer(static_cast<MTL::Buffer*>(buffer->GetRhiResource()), 0, buffer->GetObjectSize());
        }

        // the d3d12 root signature, see D3D12_Pipeline.cpp, parameter order is the top level argument buffer order
        void create_root_signature()
        {
            // everything stays volatile, the engine rewrites tables and buffer contents while recording, as on d3d12
            const IRDescriptorRangeFlags flags = static_cast<IRDescriptorRangeFlags>(IRDescriptorRangeFlagDescriptorsVolatile | IRDescriptorRangeFlagDataVolatile);
            static IRDescriptorRange1 range_srv            = { IRDescriptorRangeTypeSRV, rhi_max_resource_slots, 0, 0, flags, 0 };
            static IRDescriptorRange1 range_uav            = { IRDescriptorRangeTypeUAV, rhi_max_resource_slots, 0, 0, flags, 0 };
            static IRDescriptorRange1 range_material_tex   = { IRDescriptorRangeTypeSRV, rhi_max_array_size, 15, 1, flags, 0 };
            static IRDescriptorRange1 range_material_param = { IRDescriptorRangeTypeSRV, 1, 16, 2, flags, 0 };
            static IRDescriptorRange1 range_light_param    = { IRDescriptorRangeTypeSRV, 1, 17, 3, flags, 0 };
            static IRDescriptorRange1 range_aabbs          = { IRDescriptorRangeTypeSRV, 1, 18, 4, flags, 0 };
            static IRDescriptorRange1 range_draw_data      = { IRDescriptorRangeTypeSRV, 1, 19, 5, flags, 0 };
            static IRDescriptorRange1 range_geometry[3] =
            {
                { IRDescriptorRangeTypeSRV, 1, 20, 8,  flags, 0 },
                { IRDescriptorRangeTypeSRV, 1, 22, 9,  flags, 1 },
                { IRDescriptorRangeTypeSRV, 1, 23, 10, flags, 2 }
            };
            static IRDescriptorRange1 range_samplers[2] =
            {
                { IRDescriptorRangeTypeSampler, samplers_compare_count, 0, 6, IRDescriptorRangeFlagDescriptorsVolatile, 0 },
                { IRDescriptorRangeTypeSampler, samplers_count,         1, 7, IRDescriptorRangeFlagDescriptorsVolatile, samplers_compare_count }
            };

            auto table = [](IRDescriptorRange1* ranges, const uint32_t count)
            {
                IRRootParameter1 parameter = {};
                parameter.ParameterType    = IRRootParameterTypeDescriptorTable;
                parameter.DescriptorTable  = { count, ranges };
                parameter.ShaderVisibility = IRShaderVisibilityAll;
                return parameter;
            };

            IRRootParameter1 parameters[metal_bindings::parameter_count] = {};
            parameters[metal_bindings::cbv_frame].ParameterType         = IRRootParameterTypeCBV;
            parameters[metal_bindings::cbv_frame].Descriptor            = { 0, 0, IRRootDescriptorFlagDataVolatile };
            parameters[metal_bindings::cbv_frame].ShaderVisibility      = IRShaderVisibilityAll;
            parameters[metal_bindings::push_constants].ParameterType    = IRRootParameterType32BitConstants;
            parameters[metal_bindings::push_constants].Constants        = { 1, 0, sizeof(Pcb_Pass) / 4 };
            parameters[metal_bindings::push_constants].ShaderVisibility = IRShaderVisibilityAll;
            parameters[metal_bindings::srv_table]                       = table(&range_srv, 1);
            parameters[metal_bindings::uav_table]                       = table(&range_uav, 1);
            parameters[metal_bindings::material_textures]               = table(&range_material_tex, 1);
            parameters[metal_bindings::material_parameters]             = table(&range_material_param, 1);
            parameters[metal_bindings::light_parameters]                = table(&range_light_param, 1);
            parameters[metal_bindings::aabbs]                           = table(&range_aabbs, 1);
            parameters[metal_bindings::draw_data]                       = table(&range_draw_data, 1);
            parameters[metal_bindings::geometry]                        = table(range_geometry, 3);
            parameters[metal_bindings::samplers]                        = table(range_samplers, 2);

            IRVersionedRootSignatureDescriptor descriptor = {};
            descriptor.version                            = IRRootSignatureVersion_1_1;
            descriptor.desc_1_1.NumParameters             = metal_bindings::parameter_count;
            descriptor.desc_1_1.pParameters               = parameters;
            descriptor.desc_1_1.Flags                     = IRRootSignatureFlagAllowInputAssemblerInputLayout;

            IRError* error = nullptr;
            root_signature = IRRootSignatureCreateFromDescriptor(&descriptor, &error);
            if (!root_signature)
            {
                SP_LOG_ERROR("Failed to create the root signature, error code %u", error ? IRErrorGetCode(error) : 0);
                if (error)
                {
                    IRErrorDestroy(error);
                }
            }
            SP_ASSERT_MSG(root_signature != nullptr, "Failed to create the root signature");

            // the converter packs parameters in order, pointers 8 byte aligned and constants 4 byte aligned
            uint32_t offset = 0;
            for (uint32_t i = 0; i < metal_bindings::parameter_count; i++)
            {
                const bool constants = parameters[i].ParameterType == IRRootParameterType32BitConstants;
                offset               = constants ? offset : (offset + 7) & ~7u;
                offsets[i]           = offset;
                offset              += constants ? parameters[i].Constants.Num32BitValues * 4 : sizeof(uint64_t);
            }
            top_level_size = (offset + 7) & ~7u;
        }
    }

    void metal_bindings::Initialize()
    {
        static_assert(sizeof(Pcb_Pass) <= push_constants_size_max);
        static_assert(sizeof(Descriptor) == 24);

        create_root_signature();

        bindless = RHI_Context::device->newBuffer(zero_size + bindless_count * sizeof(Descriptor), MTL::ResourceStorageModeShared | MTL::ResourceHazardTrackingModeUntracked);
        SP_ASSERT_MSG(bindless != nullptr, "Failed to create the bindless buffer");
        metal_utility::set_label(bindless, "bindless");
        memset(bindless->contents(), 0, bindless->length());
        metal_residency::Add(metal_residency::Group::Static, bindless);
    }

    void metal_bindings::Shutdown()
    {
        if (bindless)
        {
            metal_residency::Remove(bindless);
            bindless->release();
            bindless = nullptr;
        }

        if (root_signature)
        {
            IRRootSignatureDestroy(root_signature);
            root_signature = nullptr;
        }
    }

    void* metal_bindings::GetRootSignature()
    {
        return root_signature;
    }

    uint32_t metal_bindings::GetOffset(const Parameter parameter)
    {
        return offsets[parameter];
    }

    uint32_t metal_bindings::GetTopLevelSize()
    {
        return top_level_size;
    }

    uint64_t metal_bindings::GetBindlessAddress(const Parameter parameter)
    {
        return bindless->gpuAddress() + zero_size + bindless_first[parameter] * sizeof(Descriptor);
    }

    uint64_t metal_bindings::GetZeroAddress()
    {
        return bindless->gpuAddress();
    }

    void RHI_Device::UpdateBindlessMaterials(array<RHI_Texture*, rhi_max_array_size>* textures, RHI_Buffer* parameters)
    {
        if (textures)
        {
            lock_guard<mutex> lock(mutex_bindless);
            metal_bindings::Descriptor* entries = bindless_entries(metal_bindings::material_textures);
            for (uint32_t i = 0; i < rhi_max_array_size; i++)
            {
                RHI_Texture* texture = (*textures)[i];
                entries[i]           = texture && texture->GetRhiSrv() ? metal_bindings::texture(static_cast<MTL::Texture*>(texture->GetRhiSrv())) : metal_bindings::Descriptor();
            }
        }

        write_structured_buffer(metal_bindings::material_parameters, 0, parameters);
    }

    void RHI_Device::UpdateBindlessSamplers(const shared_ptr<RHI_Sampler>* samplers, uint32_t count)
    {
        if (!samplers)
            return;

        // the first sampler is the comparison sampler, the rest are regular ones, the same split d3d12 makes
        lock_guard<mutex> lock(mutex_bindless);
        metal_bindings::Descriptor* entries = bindless_entries(metal_bindings::samplers);
        for (uint32_t i = 0; i < count; i++)
        {
            if (!samplers[i] || !samplers[i]->GetRhiResource())
                continue;

            const uint32_t index = i == 0 ? 0 : samplers_compare_count + i - 1;
            entries[index]       = metal_bindings::sampler(static_cast<MTL::SamplerState*>(samplers[i]->GetRhiResource()), 0.0f);
        }
    }

    void RHI_Device::UpdateBindlessLights(RHI_Buffer* parameters)
    {
        write_structured_buffer(metal_bindings::light_parameters, 0, parameters);
    }

    void RHI_Device::UpdateBindlessAABBs(RHI_Buffer* buffer)
    {
        write_structured_buffer(metal_bindings::aabbs, 0, buffer);
    }

    void RHI_Device::UpdateBindlessDrawData(RHI_Buffer* buffer)
    {
        write_structured_buffer(metal_bindings::draw_data, 0, buffer);
    }

    void RHI_Device::UpdateBindlessGeometryVertices(RHI_Buffer* buffer)
    {
        write_structured_buffer(metal_bindings::geometry, 0, buffer);
    }

    void RHI_Device::UpdateBindlessGeometryIndices(RHI_Buffer* buffer)
    {
        write_structured_buffer(metal_bindings::geometry, 1, buffer);
    }

    void RHI_Device::UpdateBindlessInstances(RHI_Buffer* buffer)
    {
        write_structured_buffer(metal_bindings::geometry, 2, buffer);
    }
}
