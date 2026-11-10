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
#include "WebGPU_Internal.h"
#include <thread>
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        webgpu::Shader* get_shader(const RHI_PipelineState& state, const RHI_Shader_Type type)
        {
            RHI_Shader* shader = state.shaders[type];
            return shader ? static_cast<webgpu::Shader*>(shader->GetRhiResource()) : nullptr;
        }

        WGPUVertexFormat to_vertex_format(const RHI_Format format)
        {
            switch (format)
            {
                case RHI_Format::R32_Uint:           return WGPUVertexFormat_Uint32;
                case RHI_Format::R32G32_Float:       return WGPUVertexFormat_Float32x2;
                case RHI_Format::R32G32B32_Float:    return WGPUVertexFormat_Float32x3;
                case RHI_Format::R32G32B32A32_Float: return WGPUVertexFormat_Float32x4;
                case RHI_Format::R8G8B8A8_Unorm:     return WGPUVertexFormat_Unorm8x4;
                default:                             return WGPUVertexFormat_Float32x4;
            }
        }

        bool is_integer_format(const WGPUTextureFormat format)
        {
            const WGPUTextureSampleType sample_type = webgpu::sample_type_of(format);
            return sample_type == WGPUTextureSampleType_Uint || sample_type == WGPUTextureSampleType_Sint;
        }

        bool is_blendable_format(const WGPUTextureFormat format)
        {
            if (is_integer_format(format))
                return false;

            if (format == WGPUTextureFormat_R32Float || format == WGPUTextureFormat_RG32Float || format == WGPUTextureFormat_RGBA32Float)
                return webgpu::has_feature(WGPUFeatureName_Float32Blendable);

            return true;
        }

        // every stage's view of a group merged into one list, the layout must satisfy all of them
        vector<webgpu::Binding> merge_bindings(const RHI_PipelineState& state, const uint32_t group)
        {
            vector<webgpu::Binding> merged;
            for (uint32_t type = 0; type < static_cast<uint32_t>(RHI_Shader_Type::Max); type++)
            {
                webgpu::Shader* shader = get_shader(state, static_cast<RHI_Shader_Type>(type));
                if (!shader)
                    continue;

                for (const webgpu::Binding& binding : shader->bindings)
                {
                    if (binding.group != group)
                        continue;

                    auto existing = find_if(merged.begin(), merged.end(), [&binding](const webgpu::Binding& other) { return other.binding == binding.binding; });
                    if (existing == merged.end())
                    {
                        merged.push_back(binding);
                        continue;
                    }

                    existing->visibility   |= binding.visibility;
                    existing->sampler_mask |= binding.sampler_mask;
                    if (existing->kind == webgpu::Kind::StorageRead && binding.kind == webgpu::Kind::Storage)
                    {
                        existing->kind = webgpu::Kind::Storage;
                    }
                }
            }

            sort(merged.begin(), merged.end(), [](const webgpu::Binding& a, const webgpu::Binding& b) { return a.binding < b.binding; });
            return merged;
        }

        // samplers come from the bindless table, their filtering decides the type of every texture they sample
        void resolve_sample_types(vector<webgpu::Binding>& bindings)
        {
            for (webgpu::Binding& binding : bindings)
            {
                if (binding.kind == webgpu::Kind::Sampler)
                {
                    binding.filtering = binding.binding > webgpu::binding_samplers ? (webgpu::bindless_sampler_filtering(binding.binding) ? 1 : 0) : 1;
                }
            }

            for (webgpu::Binding& binding : bindings)
            {
                if (binding.kind != webgpu::Kind::Texture || binding.sample_type != WGPUTextureSampleType_Float)
                    continue;

                bool filtering = false;
                for (uint32_t i = 1; i < 64; i++)
                {
                    if ((binding.sampler_mask & (1ull << i)) && webgpu::bindless_sampler_filtering(webgpu::binding_samplers + i))
                    {
                        filtering = true;
                    }
                }

                binding.sample_type = filtering ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
            }
        }

        WGPUBindGroupLayout create_group_layout(const vector<webgpu::Binding>& bindings, const char* name)
        {
            vector<WGPUBindGroupLayoutEntry> entries;
            entries.reserve(bindings.size());
            for (const webgpu::Binding& binding : bindings)
            {
                WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                entry.binding                  = binding.binding;
                entry.visibility               = binding.visibility;

                // vertex shaders can't see writable storage, the vertex stage only ever reads it
                const bool writable = binding.kind == webgpu::Kind::Storage || (binding.kind == webgpu::Kind::StorageTexture && binding.access != WGPUStorageTextureAccess_ReadOnly);
                if (writable)
                {
                    entry.visibility &= ~WGPUShaderStage_Vertex;
                }

                switch (binding.kind)
                {
                    case webgpu::Kind::Uniform:
                        entry.buffer.type = WGPUBufferBindingType_Uniform;
                        break;
                    case webgpu::Kind::Storage:
                        entry.buffer.type = WGPUBufferBindingType_Storage;
                        break;
                    case webgpu::Kind::StorageRead:
                        entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                        break;
                    case webgpu::Kind::Texture:
                        entry.texture.sampleType    = binding.sample_type;
                        entry.texture.viewDimension = binding.dimension;
                        entry.texture.multisampled  = binding.multisampled != 0;
                        break;
                    case webgpu::Kind::StorageTexture:
                        entry.storageTexture.access        = binding.access;
                        entry.storageTexture.format        = binding.storage_format;
                        entry.storageTexture.viewDimension = binding.dimension;
                        break;
                    case webgpu::Kind::Sampler:
                        entry.sampler.type = binding.filtering ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
                        break;
                    case webgpu::Kind::SamplerComparison:
                        entry.sampler.type = WGPUSamplerBindingType_Comparison;
                        break;
                }

                if (entry.visibility == 0)
                {
                    entry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
                }
                entries.push_back(entry);
            }

            WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            descriptor.label                         = webgpu::label(name);
            descriptor.entryCount                    = entries.size();
            descriptor.entries                       = entries.data();
            return wgpuDeviceCreateBindGroupLayout(RHI_Context::device, &descriptor);
        }

        bool create_layout(webgpu::Pipeline* pipeline, const RHI_PipelineState& state, const vector<webgpu::BindingOverride>& overrides)
        {
            for (uint32_t group = 0; group < webgpu::group_count; group++)
            {
                if (group == webgpu::group_push)
                {
                    pipeline->group_layouts[group] = webgpu::push_constants_layout();
                    wgpuBindGroupLayoutAddRef(pipeline->group_layouts[group]);
                    continue;
                }

                pipeline->bindings[group] = merge_bindings(state, group);
                resolve_sample_types(pipeline->bindings[group]);
                if (group == webgpu::group_pass)
                {
                    for (const webgpu::BindingOverride& entry : overrides)
                    {
                        for (webgpu::Binding& binding : pipeline->bindings[group])
                        {
                            if (binding.binding != entry.binding)
                                continue;

                            if (binding.kind == webgpu::Kind::StorageTexture && entry.storage_format != WGPUTextureFormat_Undefined)
                            {
                                binding.storage_format = entry.storage_format;
                            }
                            else if (binding.kind == webgpu::Kind::Texture && entry.sample_type != WGPUTextureSampleType_Undefined)
                            {
                                binding.sample_type = entry.sample_type;
                            }
                        }
                    }
                }
                pipeline->group_layouts[group] = create_group_layout(pipeline->bindings[group], state.name);
                if (!pipeline->group_layouts[group])
                    return false;
            }

            WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            descriptor.label                        = webgpu::label(state.name);
            descriptor.bindGroupLayoutCount         = webgpu::group_count;
            descriptor.bindGroupLayouts             = pipeline->group_layouts.data();
            pipeline->layout                        = wgpuDeviceCreatePipelineLayout(RHI_Context::device, &descriptor);
            return pipeline->layout != nullptr;
        }

        bool is_imgui(const RHI_PipelineState& state)
        {
            return state.name && strcmp(state.name, "imgui") == 0;
        }

        // the input layout says where each semantic lives in the vertex, inputs it doesn't have read zeros
        struct VertexLayout
        {
            vector<WGPUVertexAttribute> attributes_vertex;
            vector<WGPUVertexAttribute> attributes_zero;
            array<WGPUVertexBufferLayout, 2> buffers = {};
            uint32_t buffer_count                    = 0;
        };

        void create_vertex_layout(VertexLayout& layout, webgpu::Pipeline* pipeline, RHI_Shader* shader_vertex, const webgpu::Shader* shader)
        {
            RHI_InputLayout* input_layout = shader_vertex->GetInputLayout().get();
            for (const webgpu::VertexInput& input : shader->vertex_inputs)
            {
                const VertexAttribute* match = nullptr;
                if (input_layout)
                {
                    string semantic = input.semantic;
                    while (!semantic.empty() && isdigit(static_cast<unsigned char>(semantic.back())))
                    {
                        semantic.pop_back();
                    }

                    for (const VertexAttribute& attribute : input_layout->GetAttributeDescriptions())
                    {
                        if (attribute.name.size() == semantic.size() && equal(attribute.name.begin(), attribute.name.end(), semantic.begin(), [](char a, char b) { return tolower(a) == tolower(b); }))
                        {
                            match = &attribute;
                        }
                    }
                }

                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                attribute.shaderLocation      = input.location;
                if (match)
                {
                    attribute.format = to_vertex_format(match->format);
                    attribute.offset = match->offset;
                    layout.attributes_vertex.push_back(attribute);
                }
                else
                {
                    attribute.format = input.format;
                    attribute.offset = 0;
                    layout.attributes_zero.push_back(attribute);
                }
            }

            if (!layout.attributes_vertex.empty())
            {
                WGPUVertexBufferLayout& buffer = layout.buffers[layout.buffer_count++];
                buffer                         = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
                buffer.stepMode                = WGPUVertexStepMode_Vertex;
                buffer.arrayStride             = input_layout->GetVertexSize();
                buffer.attributeCount          = layout.attributes_vertex.size();
                buffer.attributes              = layout.attributes_vertex.data();
                pipeline->uses_vertex_buffer   = true;
            }

            if (!layout.attributes_zero.empty())
            {
                pipeline->zero_buffer_slot     = layout.buffer_count;
                WGPUVertexBufferLayout& buffer = layout.buffers[layout.buffer_count++];
                buffer                         = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
                buffer.stepMode                = WGPUVertexStepMode_Vertex;
                buffer.arrayStride             = 0;
                buffer.attributeCount          = layout.attributes_zero.size();
                buffer.attributes              = layout.attributes_zero.data();
                pipeline->uses_zero_buffer     = true;
            }
        }

        void on_render_pipeline(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline render, WGPUStringView message, void* userdata1, void*)
        {
            webgpu::Pipeline* pipeline = static_cast<webgpu::Pipeline*>(userdata1);
            if (status == WGPUCreatePipelineAsyncStatus_Success && render)
            {
                pipeline->render = render;
                pipeline->status.store(webgpu::PipelineStatus::Ready, memory_order_release);
                return;
            }

            SP_LOG_ERROR("Failed to create render pipeline '%s': %.*s", pipeline->name.c_str(), static_cast<int>(message.length == WGPU_STRLEN ? strlen(message.data) : message.length), message.data ? message.data : "");
            if (render)
            {
                wgpuRenderPipelineRelease(render);
            }
            pipeline->status.store(webgpu::PipelineStatus::Failed, memory_order_release);
        }

        void on_compute_pipeline(WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline compute, WGPUStringView message, void* userdata1, void*)
        {
            webgpu::Pipeline* pipeline = static_cast<webgpu::Pipeline*>(userdata1);
            if (status == WGPUCreatePipelineAsyncStatus_Success && compute)
            {
                pipeline->compute = compute;
                pipeline->status.store(webgpu::PipelineStatus::Ready, memory_order_release);
                return;
            }

            SP_LOG_ERROR("Failed to create compute pipeline '%s': %.*s", pipeline->name.c_str(), static_cast<int>(message.length == WGPU_STRLEN ? strlen(message.data) : message.length), message.data ? message.data : "");
            if (compute)
            {
                wgpuComputePipelineRelease(compute);
            }
            pipeline->status.store(webgpu::PipelineStatus::Failed, memory_order_release);
        }

        // native builds compile on worker threads and can wait, the browser answers between frames
        void wait_for_creation(webgpu::Pipeline* pipeline)
        {
#if !defined(__EMSCRIPTEN__)
            while (pipeline->status.load(memory_order_acquire) == webgpu::PipelineStatus::Pending)
            {
                webgpu::process_events();
                this_thread::sleep_for(chrono::microseconds(200));
            }
#endif
        }

        webgpu::Pipeline* create_compute_pipeline(const RHI_PipelineState& state, const vector<webgpu::BindingOverride>& overrides)
        {
            RHI_Shader* rhi_shader = state.shaders[RHI_Shader_Type::Compute];
            webgpu::Shader* shader = get_shader(state, RHI_Shader_Type::Compute);
            if (!shader)
                return nullptr;

            webgpu::Pipeline* pipeline = new webgpu::Pipeline();
            pipeline->name             = state.name ? state.name : "";
            pipeline->state            = state;
            if (!create_layout(pipeline, state, overrides))
            {
                webgpu::destroy_pipeline(pipeline);
                return nullptr;
            }

            // the pipeline keeps its own reference to the module
            WGPUShaderModule module = webgpu::create_module_variant(shader, overrides, state.name);

            WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            descriptor.label                         = webgpu::label(state.name);
            descriptor.layout                        = pipeline->layout;
            descriptor.compute.module                = module;
            descriptor.compute.entryPoint            = webgpu::label(rhi_shader->GetEntryPoint());

            WGPUCreateComputePipelineAsyncCallbackInfo callback = WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
            callback.mode                                       = WGPUCallbackMode_AllowSpontaneous;
            callback.callback                                   = on_compute_pipeline;
            callback.userdata1                                  = pipeline;
            wgpuDeviceCreateComputePipelineAsync(RHI_Context::device, &descriptor, callback);
            wgpuShaderModuleRelease(module);
            wait_for_creation(pipeline);

            return pipeline;
        }

        webgpu::Pipeline* create_graphics_pipeline(const RHI_PipelineState& state, const vector<webgpu::BindingOverride>& overrides)
        {
            if (state.HasMeshShaders() || state.shaders[RHI_Shader_Type::Hull] || state.shaders[RHI_Shader_Type::Domain])
            {
                SP_LOG_WARNING("Pipeline '%s' needs stages webgpu doesn't have", state.name);
                return nullptr;
            }

            RHI_Shader* rhi_shader_vertex       = state.shaders[RHI_Shader_Type::Vertex];
            RHI_Shader* rhi_shader_pixel        = state.shaders[RHI_Shader_Type::Pixel];
            webgpu::Shader* shader_vertex       = get_shader(state, RHI_Shader_Type::Vertex);
            webgpu::Shader* shader_pixel        = get_shader(state, RHI_Shader_Type::Pixel);
            if (!shader_vertex)
                return nullptr;

            webgpu::Pipeline* pipeline = new webgpu::Pipeline();
            pipeline->name             = state.name ? state.name : "";
            pipeline->imgui            = is_imgui(state);
            pipeline->state            = state;
            if (!create_layout(pipeline, state, overrides))
            {
                webgpu::destroy_pipeline(pipeline);
                return nullptr;
            }

            // the pipeline keeps its own references to the modules
            WGPUShaderModule module_vertex = webgpu::create_module_variant(shader_vertex, overrides, state.name);
            WGPUShaderModule module_pixel  = shader_pixel ? webgpu::create_module_variant(shader_pixel, overrides, state.name) : nullptr;

            WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            descriptor.label                        = webgpu::label(state.name);
            descriptor.layout                       = pipeline->layout;

            // vertex
            VertexLayout vertex_layout;
            create_vertex_layout(vertex_layout, pipeline, rhi_shader_vertex, shader_vertex);
            descriptor.vertex.module      = module_vertex;
            descriptor.vertex.entryPoint  = webgpu::label(rhi_shader_vertex->GetEntryPoint());
            descriptor.vertex.bufferCount = vertex_layout.buffer_count;
            descriptor.vertex.buffers     = vertex_layout.buffers.data();

            // primitive, d3d's clockwise front faces carry over since both have y up in ndc
            const bool is_line_or_point            = state.primitive_topology == RHI_PrimitiveTopology::LineList || state.primitive_topology == RHI_PrimitiveTopology::PointList;
            descriptor.primitive.topology          = webgpu_primitive_topology[static_cast<uint32_t>(state.primitive_topology)];
            descriptor.primitive.frontFace         = WGPUFrontFace_CW;
            descriptor.primitive.cullMode          = (pipeline->imgui || is_line_or_point) ? WGPUCullMode_None : webgpu_cull_mode[static_cast<uint32_t>(state.cull_mode)];
            const RHI_RasterizerState* rasterizer  = state.rasterizer_state;
            if (rasterizer && !rasterizer->GetDepthClipEnabled() && webgpu::has_feature(WGPUFeatureName_DepthClipControl))
            {
                descriptor.primitive.unclippedDepth = true;
            }

            // depth and stencil
            WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            if (state.render_target_depth_texture)
            {
                const RHI_DepthStencilState* depth = state.depth_stencil_state;
                depth_stencil.format               = webgpu::to_format(state.depth_format);
                const bool depth_test              = depth && depth->GetDepthTestEnabled();
                depth_stencil.depthWriteEnabled    = (depth_test && depth->GetDepthWriteEnabled()) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                depth_stencil.depthCompare         = depth_test ? webgpu_comparison_function[static_cast<uint32_t>(depth->GetDepthComparisonFunction())] : WGPUCompareFunction_Always;

                WGPUStencilFaceState stencil = WGPU_STENCIL_FACE_STATE_INIT;
                stencil.compare              = WGPUCompareFunction_Always;
                stencil.failOp               = WGPUStencilOperation_Keep;
                stencil.depthFailOp          = WGPUStencilOperation_Keep;
                stencil.passOp               = WGPUStencilOperation_Keep;
                depth_stencil.stencilReadMask  = 0;
                depth_stencil.stencilWriteMask = 0;
                if (webgpu::has_stencil(depth_stencil.format) && depth && (depth->GetStencilTestEnabled() || depth->GetStencilWriteEnabled()))
                {
                    stencil.compare                = webgpu_comparison_function[static_cast<uint32_t>(depth->GetStencilComparisonFunction())];
                    stencil.failOp                 = webgpu_stencil_operation[static_cast<uint32_t>(depth->GetStencilFailOperation())];
                    stencil.depthFailOp            = webgpu_stencil_operation[static_cast<uint32_t>(depth->GetStencilDepthFailOperation())];
                    stencil.passOp                 = webgpu_stencil_operation[static_cast<uint32_t>(depth->GetStencilPassOperation())];
                    depth_stencil.stencilReadMask  = depth->GetStencilReadMask();
                    depth_stencil.stencilWriteMask = depth->GetStencilWriteMask();
                }
                depth_stencil.stencilFront = stencil;
                depth_stencil.stencilBack  = stencil;

                // lines and points can't have depth bias
                if (rasterizer && !is_line_or_point)
                {
                    depth_stencil.depthBias           = static_cast<int32_t>(rasterizer->GetDepthBias());
                    depth_stencil.depthBiasSlopeScale = rasterizer->GetDepthBiasSlopeScaled();
                    depth_stencil.depthBiasClamp      = rasterizer->GetDepthBiasClamp();
                }
                descriptor.depthStencil = &depth_stencil;
            }

            // fragment, attachment indices must line up with the render pass so gaps stay undefined
            array<WGPUColorTargetState, rhi_max_render_target_count> targets = {};
            array<WGPUBlendState, rhi_max_render_target_count> blends        = {};
            uint32_t target_count                                            = 0;
            const RHI_BlendState* blend                                      = state.blend_state;
            auto set_target = [&](const uint32_t index, const WGPUTextureFormat format)
            {
                WGPUColorTargetState& target = targets[index];
                target                       = WGPU_COLOR_TARGET_STATE_INIT;
                target.format                = format;
                target.writeMask             = pipeline->imgui ? (WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue) : WGPUColorWriteMask_All;
                if (!shader_pixel || !(shader_pixel->fragment_outputs & (1u << index)))
                {
                    target.writeMask = WGPUColorWriteMask_None;
                }

                if (blend && blend->GetBlendEnabled() && is_blendable_format(format))
                {
                    blends[index].color.srcFactor = webgpu_blend_factor[static_cast<uint32_t>(blend->GetSourceBlend())];
                    blends[index].color.dstFactor = webgpu_blend_factor[static_cast<uint32_t>(blend->GetDestBlend())];
                    blends[index].color.operation = webgpu_blend_operation[static_cast<uint32_t>(blend->GetBlendOp())];
                    blends[index].alpha.srcFactor = webgpu_blend_factor[static_cast<uint32_t>(blend->GetSourceBlendAlpha())];
                    blends[index].alpha.dstFactor = webgpu_blend_factor[static_cast<uint32_t>(blend->GetDestBlendAlpha())];
                    blends[index].alpha.operation = webgpu_blend_operation[static_cast<uint32_t>(blend->GetBlendOpAlpha())];
                    target.blend                  = &blends[index];
                }
                target_count = max(target_count, index + 1);
            };

            if (state.render_target_swapchain)
            {
                set_target(0, webgpu::surface_format());
            }
            else
            {
                for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                {
                    targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
                    if (state.render_target_color_textures[i])
                    {
                        set_target(i, webgpu::texture_format(state.render_target_color_textures[i]));
                    }
                }
            }

            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            if (shader_pixel)
            {
                fragment.module      = module_pixel;
                fragment.entryPoint  = webgpu::label(rhi_shader_pixel->GetEntryPoint());
                fragment.targetCount = target_count;
                fragment.targets     = targets.data();
                descriptor.fragment  = &fragment;
            }

            WGPUCreateRenderPipelineAsyncCallbackInfo callback = WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
            callback.mode                                      = WGPUCallbackMode_AllowSpontaneous;
            callback.callback                                  = on_render_pipeline;
            callback.userdata1                                 = pipeline;
            wgpuDeviceCreateRenderPipelineAsync(RHI_Context::device, &descriptor, callback);
            wgpuShaderModuleRelease(module_vertex);
            if (module_pixel)
            {
                wgpuShaderModuleRelease(module_pixel);
            }
            wait_for_creation(pipeline);

            return pipeline;
        }
    }

    void webgpu::destroy_pipeline(void* resource)
    {
        Pipeline* pipeline = static_cast<Pipeline*>(resource);

        // the creation callback still holds the pointer
        while (pipeline->status.load(memory_order_acquire) == PipelineStatus::Pending)
        {
            process_events();
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(1);
#else
            this_thread::sleep_for(chrono::microseconds(200));
#endif
        }

        for (auto& [key, variant] : pipeline->variants)
        {
            destroy_pipeline(variant);
        }
        pipeline->variants.clear();

        if (pipeline->render)
        {
            wgpuRenderPipelineRelease(pipeline->render);
        }

        if (pipeline->compute)
        {
            wgpuComputePipelineRelease(pipeline->compute);
        }

        if (pipeline->bindless_group)
        {
            wgpuBindGroupRelease(pipeline->bindless_group);
        }

        if (pipeline->layout)
        {
            wgpuPipelineLayoutRelease(pipeline->layout);
        }

        for (WGPUBindGroupLayout layout : pipeline->group_layouts)
        {
            if (layout)
            {
                wgpuBindGroupLayoutRelease(layout);
            }
        }

        delete pipeline;
    }

    webgpu::Pipeline* webgpu::pipeline_variant(Pipeline* base, const vector<BindingOverride>& overrides)
    {
        string key;
        for (const BindingOverride& entry : overrides)
        {
            key += to_string(entry.binding) + ":" + to_string(static_cast<uint32_t>(entry.storage_format)) + ":" + to_string(static_cast<uint32_t>(entry.sample_type)) + ";";
        }

        Pipeline* variant = nullptr;
        {
            lock_guard<mutex> lock(base->variants_mutex);
            auto it = base->variants.find(key);
            if (it != base->variants.end())
            {
                variant = it->second;
            }
            else
            {
                variant = base->state.IsCompute() ? create_compute_pipeline(base->state, overrides) : create_graphics_pipeline(base->state, overrides);
                base->variants[key] = variant;
            }
        }

        if (!variant || variant->status.load(memory_order_acquire) != PipelineStatus::Ready)
            return base;

        return variant;
    }

    void RHI_Pipeline::Create(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        m_state = pipeline_state;

        if (pipeline_state.IsCompute())
        {
            m_rhi_resource = create_compute_pipeline(m_state, {});
        }
        else if (pipeline_state.IsGraphics())
        {
            m_rhi_resource = create_graphics_pipeline(m_state, {});
        }
        else if (pipeline_state.IsRayTracing())
        {
            SP_LOG_WARNING("Pipeline '%s' needs ray tracing, which webgpu doesn't have", pipeline_state.name);
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
