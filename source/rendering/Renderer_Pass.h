/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include "../rhi/RHI_Pass.h"
//=================================

namespace spartan
{
    // renderer-level pass builder: one object for shaders, targets, typed bindings, Pcb_Pass constants and execution
    // eliminates juggling RHI_CommandList::*, Renderer::Bind*, GetShader/GetBuffer/GetRenderTarget, and global m_pcb_pass_cpu
    class RenderPass : public RHI_Pass
    {
    public:
        explicit RenderPass(const char* name, const bool gpu_timing = true)
            : RHI_Pass(name, gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, const Renderer_Shader shader, const bool gpu_timing = true)
            : RHI_Pass(name, Renderer::GetShader(shader), gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, RHI_Shader* shader, const bool gpu_timing = true)
            : RHI_Pass(name, shader, gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, const Renderer_Shader shader_a, const Renderer_Shader shader_b, const bool gpu_timing = true)
            : RHI_Pass(name, Renderer::GetShader(shader_a), Renderer::GetShader(shader_b), gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, RHI_Shader* shader_a, RHI_Shader* shader_b, const bool gpu_timing = true)
            : RHI_Pass(name, shader_a, shader_b, gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, const Renderer_Shader shader_a, const Renderer_Shader shader_b, const Renderer_Shader shader_c, const bool gpu_timing = true)
            : RHI_Pass(name, Renderer::GetShader(shader_a), Renderer::GetShader(shader_b), Renderer::GetShader(shader_c), gpu_timing)
        {
            InitPcb();
        }

        RenderPass(const char* name, RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c, const bool gpu_timing = true)
            : RHI_Pass(name, shader_a, shader_b, shader_c, gpu_timing)
        {
            InitPcb();
        }

        // shaders
        using RHI_Pass::Shader;
        using RHI_Pass::Shaders;

        void Shader(const Renderer_Shader shader, const char* name = nullptr)
        {
            RHI_Pass::Shader(Renderer::GetShader(shader), name);
        }

        void Shaders(const Renderer_Shader shader_a, const Renderer_Shader shader_b)
        {
            RHI_Pass::Shaders(Renderer::GetShader(shader_a), Renderer::GetShader(shader_b));
        }

        void Shaders(const Renderer_Shader shader_a, const Renderer_Shader shader_b, const Renderer_Shader shader_c)
        {
            RHI_Pass::Shaders(Renderer::GetShader(shader_a), Renderer::GetShader(shader_b), Renderer::GetShader(shader_c));
        }

        void Shaders(const Renderer_Shader shader_a, const Renderer_Shader shader_b, const Renderer_Shader shader_c, const Renderer_Shader shader_d)
        {
            RHI_Pass::Shaders(Renderer::GetShader(shader_a), Renderer::GetShader(shader_b), Renderer::GetShader(shader_c), Renderer::GetShader(shader_d));
        }

        // targets
        using RHI_Pass::ColorTarget;
        using RHI_Pass::DepthTarget;

        void ColorTarget(const Renderer_RenderTarget rt, const Color& clear_color = rhi_color_load)
        {
            RHI_Pass::ColorTarget(Renderer::GetRenderTarget(rt), clear_color);
        }

        void DepthTarget(const Renderer_RenderTarget rt, const float clear_depth = rhi_depth_load)
        {
            RHI_Pass::DepthTarget(Renderer::GetRenderTarget(rt), clear_depth);
        }

        // common renderer textures
        void CommonTextures(const uint32_t eye_layer = rhi_all_mips, const bool bind_ssao = true)
        {
            Renderer::SetCommonTextures(eye_layer, bind_ssao);
        }

        // unified resource bindings
        using RHI_Pass::Bind;

        void Bind(const Renderer_BindingsSrv slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            Read(static_cast<uint32_t>(slot), texture, mip_index, mip_range, array_layer);
        }

        void Bind(const Renderer_BindingsSrv slot, const Renderer_RenderTarget rt, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            Read(static_cast<uint32_t>(slot), Renderer::GetRenderTarget(rt), mip_index, mip_range, array_layer);
        }

        void Bind(const Renderer_BindingsSrv slot, const Renderer_StandardTexture texture)
        {
            Read(static_cast<uint32_t>(slot), Renderer::GetStandardTexture(texture));
        }

        void Bind(const Renderer_BindingsSrv slot, RHI_AccelerationStructure* tlas)
        {
            RHI_Pass::Bind(static_cast<uint32_t>(slot), tlas);
        }

        void Bind(const Renderer_BindingsUav slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            Write(static_cast<uint32_t>(slot), texture, mip_index, mip_range, array_layer);
        }

        void Bind(const Renderer_BindingsUav slot, const Renderer_RenderTarget rt, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            Write(static_cast<uint32_t>(slot), Renderer::GetRenderTarget(rt), mip_index, mip_range, array_layer);
        }

        void Bind(const Renderer_BindingsUav slot, RHI_Buffer* buffer)
        {
            RHI_Pass::Bind(static_cast<uint32_t>(slot), buffer);
        }

        void Bind(const Renderer_BindingsUav slot, const Renderer_Buffer buffer)
        {
            RHI_Pass::Bind(static_cast<uint32_t>(slot), Renderer::GetBuffer(buffer));
        }

        void Bind(const Renderer_BindingsCb slot, RHI_Buffer* constant_buffer)
        {
            BindConstant(static_cast<uint32_t>(slot), constant_buffer);
        }

        void Bind(const Renderer_BindingsCb slot, const Renderer_Buffer constant_buffer)
        {
            BindConstant(static_cast<uint32_t>(slot), Renderer::GetBuffer(constant_buffer));
        }

        void Bind(const char* name, const Renderer_RenderTarget rt, const uint32_t mip_index = rhi_all_mips, const uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips)
        {
            RHI_Pass::Bind(name, Renderer::GetRenderTarget(rt), mip_index, mip_range, array_layer);
        }

        void Bind(const char* name, const Renderer_StandardTexture texture)
        {
            RHI_Pass::Bind(name, Renderer::GetStandardTexture(texture));
        }

        void Bind(const char* name, const Renderer_Buffer buffer)
        {
            RHI_Pass::Bind(name, Renderer::GetBuffer(buffer));
        }

        // pass push constants (local Pcb_Pass, auto-pushed before every draw, dispatch or trace)
        template<typename T>
        void Set(const uint32_t slot, const T& value)
        {
            Pcb().set(slot, value);
            m_push_dirty = true;
        }

        void DrawIndex(const uint32_t index)
        {
            Pcb().draw_index = index;
            m_push_dirty     = true;
        }

        void MaterialIndex(const uint32_t index)
        {
            Pcb().material_index = index;
            m_push_dirty         = true;
        }

        void Transparent(const bool transparent)
        {
            Pcb().is_transparent = transparent ? 1u : 0u;
            m_push_dirty         = true;
        }

        void TransparentRaw(const uint32_t value)
        {
            Pcb().is_transparent = value;
            m_push_dirty         = true;
        }

        void Eye(const uint32_t eye_index)
        {
            Pcb().eye_index = eye_index;
            m_push_dirty    = true;
        }

        void ResetConstants()
        {
            const uint32_t eye = Pcb().eye_index;
            Pcb()              = Pcb_Pass{};
            Pcb().eye_index    = eye;
            m_push_dirty       = true;
        }

        // execution overloads accepting Renderer_RenderTarget / Renderer_Buffer directly
        using RHI_Pass::DrawIndirect;
        using RHI_Pass::DrawIndexedIndirect;
        using RHI_Pass::DrawMeshTasksIndirect;
        using RHI_Pass::Dispatch;
        using RHI_Pass::DispatchIndirect;

        void DrawIndirect(const Renderer_Buffer args_buffer, const uint32_t args_offset = 0)
        {
            RHI_Pass::DrawIndirect(Renderer::GetBuffer(args_buffer), args_offset);
        }

        void DrawIndexedIndirect(const Renderer_Buffer args_buffer, const uint32_t args_offset = 0, const uint32_t draw_count = 1)
        {
            RHI_Pass::DrawIndexedIndirect(Renderer::GetBuffer(args_buffer), args_offset, draw_count);
        }

        void DrawMeshTasksIndirect(const Renderer_Buffer args_buffer, const uint32_t args_offset = 0)
        {
            RHI_Pass::DrawMeshTasksIndirect(Renderer::GetBuffer(args_buffer), args_offset);
        }

        void Dispatch(const Renderer_RenderTarget rt, const float resolution_scale = 1.0f)
        {
            RHI_Pass::Dispatch(Renderer::GetRenderTarget(rt), resolution_scale);
        }

        void DispatchIndirect(const Renderer_Buffer args_buffer, const uint32_t args_offset = 0)
        {
            RHI_Pass::DispatchIndirect(Renderer::GetBuffer(args_buffer), args_offset);
        }

    private:
        void InitPcb()
        {
            static_assert(sizeof(Pcb_Pass) <= 256, "Pcb_Pass exceeds RHI_Pass staging buffer");
            static_assert(std::is_trivially_copyable_v<Pcb_Pass>, "Pcb_Pass must be trivially copyable");
            Pcb()           = Pcb_Pass{};
            Pcb().eye_index = Renderer::m_pcb_pass_cpu.eye_index;
            m_push_size     = static_cast<uint32_t>(sizeof(Pcb_Pass));
            m_push_dirty    = true;
        }

        Pcb_Pass& Pcb()
        {
            return *reinterpret_cast<Pcb_Pass*>(m_push_data.data());
        }
    };
}
