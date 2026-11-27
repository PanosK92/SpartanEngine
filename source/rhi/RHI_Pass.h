/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======================
#include "RHI_CommandList.h"
#include <array>
#include <cstring>
//==================================

namespace spartan
{
    // single-object RAII pass builder over the currently bound RHI_CommandList
    // owns the pass scope (BeginPass / EndPass) and stages push constants so they flush automatically on draw, dispatch or trace
    class RHI_Pass
    {
    public:
        explicit RHI_Pass(const char* name, const bool gpu_timing = true)
        {
            RHI_CommandList::BeginPass(name, gpu_timing);
        }

        RHI_Pass(const char* name, RHI_Shader* shader, const bool gpu_timing = true)
        {
            RHI_CommandList::BeginPass(name, gpu_timing);
            Shader(shader);
        }

        RHI_Pass(const char* name, RHI_Shader* shader_a, RHI_Shader* shader_b, const bool gpu_timing = true)
        {
            RHI_CommandList::BeginPass(name, gpu_timing);
            Shaders(shader_a, shader_b);
        }

        RHI_Pass(const char* name, RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c, const bool gpu_timing = true)
        {
            RHI_CommandList::BeginPass(name, gpu_timing);
            Shaders(shader_a, shader_b, shader_c);
        }

        ~RHI_Pass()
        {
            RHI_CommandList::EndPass();
        }

        RHI_Pass(const RHI_Pass&)            = delete;
        RHI_Pass& operator=(const RHI_Pass&) = delete;
        RHI_Pass(RHI_Pass&&)                 = delete;
        RHI_Pass& operator=(RHI_Pass&&)      = delete;

        // shaders & pass identity
        void Name(const char* name)
        {
            RHI_CommandList::SetPassName(name);
        }

        void Shader(RHI_Shader* shader, const char* name = nullptr)
        {
            RHI_CommandList::SetShader(shader, name);
            if (m_push_size > 0)
            {
                m_push_dirty = true;
            }
        }

        void Shaders(RHI_Shader* shader_a, RHI_Shader* shader_b = nullptr, RHI_Shader* shader_c = nullptr, RHI_Shader* shader_d = nullptr)
        {
            RHI_CommandList::SetShaders(shader_a, shader_b, shader_c, shader_d);
            if (m_push_size > 0)
            {
                m_push_dirty = true;
            }
        }

        // targets & pipeline state
        void ColorTarget(RHI_Texture* texture, const Color& clear_color = rhi_color_load)
        {
            RHI_CommandList::SetColorTarget(texture, clear_color);
        }

        void ColorTargets(
            RHI_Texture* t0,
            RHI_Texture* t1 = nullptr,
            RHI_Texture* t2 = nullptr,
            RHI_Texture* t3 = nullptr,
            RHI_Texture* t4 = nullptr,
            RHI_Texture* t5 = nullptr,
            RHI_Texture* t6 = nullptr,
            RHI_Texture* t7 = nullptr
        )
        {
            RHI_CommandList::SetColorTargets(t0, t1, t2, t3, t4, t5, t6, t7);
        }

        void DepthTarget(RHI_Texture* texture, const float clear_depth = rhi_depth_load)
        {
            RHI_CommandList::SetDepthTarget(texture, clear_depth);
        }

        void SwapChain(RHI_SwapChain* swapchain, const Color& clear_color = rhi_color_load)
        {
            RHI_CommandList::SetSwapChain(swapchain, clear_color);
        }

        void Blend(const RHI_BlendMode mode)                       { RHI_CommandList::SetBlend(mode); }
        void Rasterizer(const RHI_RasterizerMode mode)             { RHI_CommandList::SetRasterizer(mode); }
        void Depth(const RHI_DepthMode mode)                       { RHI_CommandList::SetDepth(mode); }
        void Topology(const RHI_PrimitiveTopology topology)        { RHI_CommandList::SetPrimitiveTopology(topology); }
        void CullMode(const RHI_CullMode mode)                     { RHI_CommandList::SetCullMode(mode); }
        void ClearColor(const Color& color)                        { RHI_CommandList::SetClearColor(color); }
        void ClearColor(const uint32_t index, const Color& color)  { RHI_CommandList::SetClearColor(index, color); }
        void ClearDepth(const float depth)                         { RHI_CommandList::SetClearDepth(depth); }
        void Viewport(const RHI_Viewport& viewport)                { RHI_CommandList::SetViewport(viewport); }
        void Scissor(const math::Rectangle& rect)                  { RHI_CommandList::SetScissorRectangle(rect); }
        void Vrs(RHI_Texture* texture)                             { RHI_CommandList::SetVrsTexture(texture); }
        void ResolutionScale(const bool enabled)                   { RHI_CommandList::SetResolutionScale(enabled); }
        void Multiview(const bool enabled)                         { RHI_CommandList::SetMultiview(enabled); }
        void ArrayIndex(const uint32_t index)                      { RHI_CommandList::SetArrayIndex(index); }
        void StandardResources(const bool enabled)                 { RHI_CommandList::SetUseStandardResources(enabled); }
        void VertexBuffer(const RHI_Buffer* vertex, RHI_Buffer* instance = nullptr) { RHI_CommandList::SetBufferVertex(vertex, instance); }
        void IndexBuffer(const RHI_Buffer* index)                  { RHI_CommandList::SetBufferIndex(index); }
        void EndRenderPass()                                       { RHI_CommandList::RenderPassEnd(); }
        bool Ready() const                                         { return RHI_CommandList::IsPipelineReady(); }

        // resource bindings
        void Read(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            RHI_CommandList::SetTexture(slot, texture, mip_index, mip_range, false, array_layer);
        }

        void Write(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            RHI_CommandList::SetTexture(slot, texture, mip_index, mip_range, true, array_layer);
        }

        void Bind(const uint32_t slot, RHI_Buffer* buffer)
        {
            RHI_CommandList::SetBuffer(slot, buffer);
        }

        void Bind(const uint32_t slot, RHI_AccelerationStructure* tlas)
        {
            RHI_CommandList::SetAccelerationStructure(slot, tlas);
        }

        void BindConstant(const uint32_t slot, RHI_Buffer* constant_buffer)
        {
            RHI_CommandList::SetConstantBuffer(slot, constant_buffer);
        }

        void Bind(const char* name, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            RHI_CommandList::SetTexture(name, texture, mip_index, mip_range, array_layer);
        }

        void Bind(const char* name, RHI_Buffer* buffer)
        {
            RHI_CommandList::SetBuffer(name, buffer);
        }

        void Bind(const char* name, RHI_AccelerationStructure* tlas)
        {
            RHI_CommandList::SetAccelerationStructure(name, tlas);
        }

        void BindConstant(const char* name, RHI_Buffer* constant_buffer)
        {
            RHI_CommandList::SetConstantBuffer(name, constant_buffer);
        }

        // generic push constants (auto-flushed before every draw, dispatch or trace)
        template<typename T>
        void Push(const T& data)
        {
            static_assert(sizeof(T) <= 256, "Push constant struct exceeds 256-byte staging buffer");
            std::memcpy(m_push_data.data(), &data, sizeof(T));
            m_push_size  = static_cast<uint32_t>(sizeof(T));
            m_push_dirty = true;
        }

        // execution
        void Draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0)
        {
            FlushPushConstants();
            RHI_CommandList::Draw(vertex_count, vertex_start_index);
        }

        void DrawIndexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_index = 0, const uint32_t instance_count = 1)
        {
            FlushPushConstants();
            RHI_CommandList::DrawIndexed(index_count, index_offset, vertex_offset, instance_index, instance_count);
        }

        void DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0)
        {
            FlushPushConstants();
            RHI_CommandList::DrawIndirect(args_buffer, args_offset);
        }

        void DrawIndexedIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0, const uint32_t draw_count = 1)
        {
            FlushPushConstants();
            RHI_CommandList::DrawIndexedIndirect(args_buffer, args_offset, draw_count);
        }

        void DrawMeshTasksIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0)
        {
            FlushPushConstants();
            RHI_CommandList::DrawMeshTasksIndirect(args_buffer, args_offset);
        }

        void Dispatch(const uint32_t x, const uint32_t y, const uint32_t z = 1)
        {
            FlushPushConstants();
            RHI_CommandList::Dispatch(x, y, z);
        }

        void Dispatch(RHI_Texture* texture, const float resolution_scale = 1.0f)
        {
            FlushPushConstants();
            RHI_CommandList::Dispatch(texture, resolution_scale);
        }

        void DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0)
        {
            FlushPushConstants();
            RHI_CommandList::DispatchIndirect(args_buffer, args_offset);
        }

        void TraceRays(const uint32_t width, const uint32_t height)
        {
            FlushPushConstants();
            RHI_CommandList::TraceRays(width, height);
        }

    protected:
        void FlushPushConstants()
        {
            if (m_push_dirty && m_push_size > 0)
            {
                RHI_CommandList::PushConstants(0, m_push_size, m_push_data.data());
                m_push_dirty = false;
            }
        }

        alignas(16) std::array<uint8_t, 256> m_push_data = {};
        uint32_t m_push_size                              = 0;
        bool m_push_dirty                                 = false;
    };
}
