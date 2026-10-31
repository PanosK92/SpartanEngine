/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_BlendState.h"
#include "../RHI_DepthStencilState.h"
#include "Metal_Internal.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        metal_shader::Shader* get_shader(const RHI_PipelineState& state, const RHI_Shader_Type type)
        {
            RHI_Shader* shader = state.shaders[type];
            return shader ? static_cast<metal_shader::Shader*>(shader->GetRhiResource()) : nullptr;
        }

        MTL::VertexFormat to_vertex_format(const RHI_Format format)
        {
            switch (format)
            {
                case RHI_Format::R32_Uint:           return MTL::VertexFormatUInt;
                case RHI_Format::R32G32_Float:       return MTL::VertexFormatFloat2;
                case RHI_Format::R32G32B32_Float:    return MTL::VertexFormatFloat3;
                case RHI_Format::R32G32B32A32_Float: return MTL::VertexFormatFloat4;
                case RHI_Format::R8G8B8A8_Unorm:     return MTL::VertexFormatUChar4Normalized;
                default:                             return MTL::VertexFormatInvalid;
            }
        }

        // the input layout says where each semantic lives in the vertex, the shader says which attribute reads it
        MTL::VertexDescriptor* create_vertex_descriptor(RHI_Shader* shader_vertex, const metal_shader::Shader* shader)
        {
            if (shader->vertex_inputs.empty())
                return nullptr;

            RHI_InputLayout* input_layout     = shader_vertex->GetInputLayout().get();
            MTL::VertexDescriptor* descriptor = MTL::VertexDescriptor::alloc()->init();
            bool uses_vertex_buffer           = false;
            bool uses_zero_buffer             = false;
            for (const metal_shader::VertexInput& input : shader->vertex_inputs)
            {
                const VertexAttribute* match = nullptr;
                if (input_layout)
                {
                    for (const VertexAttribute& attribute : input_layout->GetAttributeDescriptions())
                    {
                        string semantic = attribute.name + "0";
                        for (char& c : semantic)
                        {
                            c = static_cast<char>(tolower(c));
                        }

                        if (input.semantic == semantic)
                        {
                            match = &attribute;
                        }
                    }
                }

                MTL::VertexAttributeDescriptor* attribute_descriptor = descriptor->attributes()->object(metal_bindings::index_stage_in_offset + input.attribute);
                attribute_descriptor->setFormat(match ? to_vertex_format(match->format) : MTL::VertexFormatUInt);
                attribute_descriptor->setOffset(match ? match->offset : 0);
                attribute_descriptor->setBufferIndex(match ? metal_bindings::index_vertex_buffer : metal_bindings::index_vertex_zero);
                uses_vertex_buffer |= match != nullptr;
                uses_zero_buffer   |= match == nullptr;
            }

            if (uses_vertex_buffer)
            {
                descriptor->layouts()->object(metal_bindings::index_vertex_buffer)->setStride(input_layout->GetVertexSize());
                descriptor->layouts()->object(metal_bindings::index_vertex_buffer)->setStepFunction(MTL::VertexStepFunctionPerVertex);
            }
            if (uses_zero_buffer)
            {
                descriptor->layouts()->object(metal_bindings::index_vertex_zero)->setStride(sizeof(uint32_t));
                descriptor->layouts()->object(metal_bindings::index_vertex_zero)->setStepFunction(MTL::VertexStepFunctionConstant);
                descriptor->layouts()->object(metal_bindings::index_vertex_zero)->setStepRate(0);
            }
            return descriptor;
        }

        MTL::DepthStencilState* create_depth_stencil_state(const RHI_DepthStencilState* state)
        {
            MTL::DepthStencilDescriptor* descriptor = MTL::DepthStencilDescriptor::alloc()->init();
            if (state && state->GetDepthTestEnabled())
            {
                descriptor->setDepthCompareFunction(metal_comparison_function[static_cast<uint32_t>(state->GetDepthComparisonFunction())]);
            }
            descriptor->setDepthWriteEnabled(state && state->GetDepthWriteEnabled());

            if (state && (state->GetStencilTestEnabled() || state->GetStencilWriteEnabled()))
            {
                MTL::StencilDescriptor* stencil = MTL::StencilDescriptor::alloc()->init();
                stencil->setStencilCompareFunction(metal_comparison_function[static_cast<uint32_t>(state->GetStencilComparisonFunction())]);
                stencil->setStencilFailureOperation(metal_stencil_operation[static_cast<uint32_t>(state->GetStencilFailOperation())]);
                stencil->setDepthFailureOperation(metal_stencil_operation[static_cast<uint32_t>(state->GetStencilDepthFailOperation())]);
                stencil->setDepthStencilPassOperation(metal_stencil_operation[static_cast<uint32_t>(state->GetStencilPassOperation())]);
                stencil->setReadMask(state->GetStencilReadMask());
                stencil->setWriteMask(state->GetStencilWriteMask());
                descriptor->setFrontFaceStencil(stencil);
                descriptor->setBackFaceStencil(stencil);
                stencil->release();
            }

            MTL::DepthStencilState* depth_stencil_state = RHI_Context::device->newDepthStencilState(descriptor);
            descriptor->release();
            return depth_stencil_state;
        }

        metal_pipeline::Pipeline* create_compute_pipeline(const RHI_PipelineState& state)
        {
            metal_shader::Shader* shader = get_shader(state, RHI_Shader_Type::Compute);
            if (!shader)
                return nullptr;

            NS::Error* error                   = nullptr;
            metal_pipeline::Pipeline* pipeline = new metal_pipeline::Pipeline();
            pipeline->compute                  = RHI_Context::device->newComputePipelineState(shader->function, &error);
            pipeline->threadgroup_size         = shader->threadgroup_size;
            if (!pipeline->compute)
            {
                SP_LOG_ERROR("Failed to create compute pipeline '%s': %s", state.name, error ? error->localizedDescription()->utf8String() : "unknown error");
                metal_pipeline::Destroy(pipeline);
                return nullptr;
            }

            return pipeline;
        }

        // imgui leaves alpha alone so the cleared alpha survives compositing, see D3D12_Pipeline.cpp
        bool is_imgui(const RHI_PipelineState& state)
        {
            return state.name && strcmp(state.name, "imgui") == 0;
        }

        // vertex and mesh pipeline descriptors describe their attachments the same way
        template<typename T>
        void set_attachments(T* descriptor, const RHI_PipelineState& state)
        {
            const MTL::ColorWriteMask write_mask = is_imgui(state) ? MTL::ColorWriteMaskRed | MTL::ColorWriteMaskGreen | MTL::ColorWriteMaskBlue : MTL::ColorWriteMaskAll;
            const RHI_BlendState* blend          = state.blend_state;
            auto set_color_attachment            = [&](const uint32_t index, const MTL::PixelFormat format)
            {
                MTL::RenderPipelineColorAttachmentDescriptor* attachment = descriptor->colorAttachments()->object(index);
                attachment->setPixelFormat(format);
                attachment->setWriteMask(write_mask);
                if (blend && blend->GetBlendEnabled())
                {
                    attachment->setBlendingEnabled(true);
                    attachment->setSourceRGBBlendFactor(metal_blend_factor[static_cast<uint32_t>(blend->GetSourceBlend())]);
                    attachment->setDestinationRGBBlendFactor(metal_blend_factor[static_cast<uint32_t>(blend->GetDestBlend())]);
                    attachment->setRgbBlendOperation(metal_blend_operation[static_cast<uint32_t>(blend->GetBlendOp())]);
                    attachment->setSourceAlphaBlendFactor(metal_blend_factor[static_cast<uint32_t>(blend->GetSourceBlendAlpha())]);
                    attachment->setDestinationAlphaBlendFactor(metal_blend_factor[static_cast<uint32_t>(blend->GetDestBlendAlpha())]);
                    attachment->setAlphaBlendOperation(metal_blend_operation[static_cast<uint32_t>(blend->GetBlendOpAlpha())]);
                }
            };

            if (state.render_target_swapchain)
            {
                set_color_attachment(0, metal_swapchain::layer_format(state.swapchain_format));
            }
            else
            {
                for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                {
                    if (state.render_target_color_textures[i])
                    {
                        set_color_attachment(i, metal_utility::to_pixel_format(state.color_formats[i]));
                    }
                }
            }

            if (state.render_target_depth_texture)
            {
                const MTL::PixelFormat format = metal_utility::to_pixel_format(state.depth_format);
                descriptor->setDepthAttachmentPixelFormat(format);
                if (state.depth_has_stencil)
                {
                    descriptor->setStencilAttachmentPixelFormat(format);
                }
            }

            metal_utility::set_label(descriptor, state.name);
        }

        MTL::RenderPipelineState* create_vertex_render_state(const RHI_PipelineState& state, NS::Error** error)
        {
            metal_shader::Shader* shader_vertex = get_shader(state, RHI_Shader_Type::Vertex);
            metal_shader::Shader* shader_pixel  = get_shader(state, RHI_Shader_Type::Pixel);
            if (!shader_vertex)
                return nullptr;

            MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
            descriptor->setVertexFunction(shader_vertex->function);
            descriptor->setFragmentFunction(shader_pixel ? shader_pixel->function : nullptr);
            descriptor->setInputPrimitiveTopology(state.primitive_topology == RHI_PrimitiveTopology::LineList ? MTL::PrimitiveTopologyClassLine :
                                                  state.primitive_topology == RHI_PrimitiveTopology::PointList ? MTL::PrimitiveTopologyClassPoint : MTL::PrimitiveTopologyClassTriangle);

            if (MTL::VertexDescriptor* vertex_descriptor = create_vertex_descriptor(state.shaders[RHI_Shader_Type::Vertex], shader_vertex))
            {
                descriptor->setVertexDescriptor(vertex_descriptor);
                vertex_descriptor->release();
            }

            set_attachments(descriptor, state);
            MTL::RenderPipelineState* render_state = RHI_Context::device->newRenderPipelineState(descriptor, error);
            descriptor->release();
            return render_state;
        }

        // hlsl mesh shaders without an amplification stage become metal mesh functions with no object stage
        MTL::RenderPipelineState* create_mesh_render_state(const RHI_PipelineState& state, NS::Error** error)
        {
            metal_shader::Shader* shader_mesh  = get_shader(state, RHI_Shader_Type::MeshShader);
            metal_shader::Shader* shader_pixel = get_shader(state, RHI_Shader_Type::Pixel);
            if (!shader_mesh)
                return nullptr;

            MTL::MeshRenderPipelineDescriptor* descriptor = MTL::MeshRenderPipelineDescriptor::alloc()->init();
            descriptor->setMeshFunction(shader_mesh->function);
            descriptor->setFragmentFunction(shader_pixel ? shader_pixel->function : nullptr);

            set_attachments(descriptor, state);
            MTL::RenderPipelineState* render_state = RHI_Context::device->newRenderPipelineState(descriptor, MTL::PipelineOptionNone, nullptr, error);
            descriptor->release();
            return render_state;
        }

        metal_pipeline::Pipeline* create_graphics_pipeline(const RHI_PipelineState& state)
        {
            if (state.shaders[RHI_Shader_Type::Hull] || state.shaders[RHI_Shader_Type::Domain])
            {
                SP_LOG_WARNING("Pipeline '%s' needs tessellation stages, which the metal backend doesn't support yet", state.name);
                return nullptr;
            }

            NS::Error* error                   = nullptr;
            const bool is_mesh                 = state.HasMeshShaders();
            metal_pipeline::Pipeline* pipeline = new metal_pipeline::Pipeline();
            pipeline->render                   = is_mesh ? create_mesh_render_state(state, &error) : create_vertex_render_state(state, &error);
            if (!pipeline->render)
            {
                SP_LOG_ERROR("Failed to create graphics pipeline '%s': %s", state.name, error ? error->localizedDescription()->utf8String() : "unknown error");
                metal_pipeline::Destroy(pipeline);
                return nullptr;
            }

            pipeline->depth_stencil  = create_depth_stencil_state(state.depth_stencil_state);
            pipeline->primitive_type = metal_primitive_topology[static_cast<uint32_t>(state.primitive_topology)];
            pipeline->cull_none      = is_imgui(state);
            if (is_mesh)
            {
                pipeline->threadgroup_size = get_shader(state, RHI_Shader_Type::MeshShader)->threadgroup_size;
            }

            if (const RHI_RasterizerState* rasterizer = state.rasterizer_state)
            {
                pipeline->fill_mode        = metal_polygon_mode[static_cast<uint32_t>(rasterizer->GetPolygonMode())];
                pipeline->depth_clip_mode  = rasterizer->GetDepthClipEnabled() ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp;
                pipeline->depth_bias       = rasterizer->GetDepthBias();
                pipeline->depth_bias_slope = rasterizer->GetDepthBiasSlopeScaled();
                pipeline->depth_bias_clamp = rasterizer->GetDepthBiasClamp();
            }

            return pipeline;
        }

        // the ray generation kernel calls miss and hit shaders through a visible function table, slot 0 stays empty
        // because the converter reads a zero handle as no shader, the shader table names the slots per record
        metal_pipeline::Pipeline* create_ray_tracing_pipeline(const RHI_PipelineState& state)
        {
            metal_shader::Shader* shader_ray_generation = get_shader(state, RHI_Shader_Type::RayGeneration);
            metal_shader::Shader* shader_miss           = get_shader(state, RHI_Shader_Type::RayMiss);
            metal_shader::Shader* shader_hit            = get_shader(state, RHI_Shader_Type::RayHit);
            if (!shader_ray_generation)
                return nullptr;

            NS::Object* functions[2] = {};
            uint32_t function_count  = 0;
            if (shader_miss)
            {
                functions[function_count++] = shader_miss->function;
            }

            if (shader_hit)
            {
                functions[function_count++] = shader_hit->function;
            }

            NS::Array* function_array                  = NS::Array::alloc()->init(functions, function_count);
            MTL::LinkedFunctions* linked               = MTL::LinkedFunctions::alloc()->init();
            MTL::ComputePipelineDescriptor* descriptor = MTL::ComputePipelineDescriptor::alloc()->init();
            linked->setFunctions(function_array);
            descriptor->setComputeFunction(shader_ray_generation->function);
            descriptor->setLinkedFunctions(linked);
            metal_utility::set_label(descriptor, state.name);

            NS::Error* error                   = nullptr;
            metal_pipeline::Pipeline* pipeline = new metal_pipeline::Pipeline();
            pipeline->compute                  = RHI_Context::device->newComputePipelineState(descriptor, MTL::PipelineOptionNone, nullptr, &error);
            pipeline->threadgroup_size         = MTL::Size(8, 8, 1);
            descriptor->release();
            linked->release();
            function_array->release();
            if (!pipeline->compute)
            {
                SP_LOG_ERROR("Failed to create ray tracing pipeline '%s': %s", state.name, error ? error->localizedDescription()->utf8String() : "unknown error");
                metal_pipeline::Destroy(pipeline);
                return nullptr;
            }

            const uint64_t slot_miss = 1;
            const uint64_t slot_hit  = 2;
            MTL::VisibleFunctionTableDescriptor* function_table_descriptor = MTL::VisibleFunctionTableDescriptor::alloc()->init();
            function_table_descriptor->setFunctionCount(3);
            pipeline->function_table = pipeline->compute->newVisibleFunctionTable(function_table_descriptor);
            function_table_descriptor->release();
            SP_ASSERT_MSG(pipeline->function_table != nullptr, "Failed to create a visible function table");
            if (shader_miss)
            {
                pipeline->function_table->setFunction(pipeline->compute->functionHandle(shader_miss->function), slot_miss);
            }

            if (shader_hit)
            {
                pipeline->function_table->setFunction(pipeline->compute->functionHandle(shader_hit->function), slot_hit);
            }
            metal_residency::Add(metal_residency::Group::Static, pipeline->function_table);

            metal_pipeline::ShaderRecord records[3] = {};
            records[1].shader_handle                = shader_miss ? slot_miss : 0;
            records[2].shader_handle                = shader_hit ? slot_hit : 0;
            pipeline->shader_table                  = RHI_Context::device->newBuffer(records, sizeof(records), MTL::ResourceStorageModeShared | MTL::ResourceHazardTrackingModeUntracked);
            SP_ASSERT_MSG(pipeline->shader_table != nullptr, "Failed to create a shader table");
            metal_utility::set_label(pipeline->shader_table, "shader_table");
            metal_residency::Add(metal_residency::Group::Static, pipeline->shader_table);

            return pipeline;
        }
    }

    void metal_pipeline::Destroy(void* resource)
    {
        Pipeline* pipeline = static_cast<Pipeline*>(resource);
        if (pipeline->render)
        {
            pipeline->render->release();
        }

        if (pipeline->compute)
        {
            pipeline->compute->release();
        }

        if (pipeline->depth_stencil)
        {
            pipeline->depth_stencil->release();
        }

        if (pipeline->function_table)
        {
            metal_residency::Remove(pipeline->function_table);
            pipeline->function_table->release();
        }

        if (pipeline->shader_table)
        {
            metal_residency::Remove(pipeline->shader_table);
            pipeline->shader_table->release();
        }

        delete pipeline;
    }

    void RHI_Pipeline::Create(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        m_state = pipeline_state;

        if (pipeline_state.IsCompute())
        {
            m_rhi_resource = create_compute_pipeline(m_state);
        }
        else if (pipeline_state.IsGraphics())
        {
            m_rhi_resource = create_graphics_pipeline(m_state);
        }
        else if (pipeline_state.IsRayTracing())
        {
            m_rhi_resource = create_ray_tracing_pipeline(m_state);
        }
    }

    RHI_Pipeline::~RHI_Pipeline()
    {
        if (m_compile_task.valid())
        {
            m_compile_task.wait();
        }

        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Pipeline, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }
}
