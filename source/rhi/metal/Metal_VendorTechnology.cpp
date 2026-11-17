/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =============================
#include "pch.h"
#include "../RHI_VendorTechnology.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
#include "../../rendering/Renderer.h"
#include "../../rendering/Renderer_Buffers.h"
#include "Metal_Internal.h"
#include <MetalFX/MetalFX.hpp>
//========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace metalfx
    {
        MTL4::Compiler* compiler       = nullptr;
        MTL4FX::TemporalScaler* scaler = nullptr;
        MTL::Fence* fence              = nullptr;
        MTL::Buffer* fence_work        = nullptr; // a command for the encoder that signals the fence, an empty encoder drops the update
        Cb_Frame* cb_frame             = nullptr;
        uint32_t render_width          = 0;       // scaled, what was rendered this frame
        uint32_t render_height         = 0;
        uint32_t render_max_width      = 0;       // the render targets, the scaler is created for them
        uint32_t render_max_height     = 0;
        uint32_t output_width          = 0;
        uint32_t output_height         = 0;
        bool reset_history             = true;
        bool create_failed             = false;   // not retried until the resolution changes

        void scaler_destroy()
        {
            if (scaler)
            {
                scaler->release();
                scaler = nullptr;
            }
        }

        bool has_usage(const char* name, MTL::Texture* texture, const MTL::TextureUsage usage)
        {
            if ((texture->usage() & usage) == usage)
            {
                return true;
            }

            SP_LOG_ERROR("MetalFX: the %s texture lacks usage %llu (has %llu)", name, static_cast<uint64_t>(usage), static_cast<uint64_t>(texture->usage()));
            return false;
        }

        void scaler_create(MTL::Texture* color, MTL::Texture* depth, MTL::Texture* motion, MTL::Texture* output)
        {
            scaler_destroy();
            reset_history = true;

            // the resolution scale shrinks the rendered content inside the render targets, so the scale isn't fixed
            MTLFX::TemporalScalerDescriptor* descriptor = MTLFX::TemporalScalerDescriptor::alloc()->init();
            descriptor->setColorTextureFormat(color->pixelFormat());
            descriptor->setDepthTextureFormat(depth->pixelFormat());
            descriptor->setMotionTextureFormat(motion->pixelFormat());
            descriptor->setOutputTextureFormat(output->pixelFormat());
            descriptor->setInputWidth(render_max_width);
            descriptor->setInputHeight(render_max_height);
            descriptor->setOutputWidth(output_width);
            descriptor->setOutputHeight(output_height);
            descriptor->setInputContentPropertiesEnabled(true);
            descriptor->setInputContentMinScale(MTLFX::TemporalScalerDescriptor::supportedInputContentMinScale(RHI_Context::device));
            descriptor->setInputContentMaxScale(MTLFX::TemporalScalerDescriptor::supportedInputContentMaxScale(RHI_Context::device));
            scaler = descriptor->newTemporalScaler(RHI_Context::device, compiler);
            descriptor->release();

            if (!scaler)
            {
                SP_LOG_ERROR("MetalFX: failed to create the temporal scaler (%ux%u to %ux%u)", render_max_width, render_max_height, output_width, output_height);
                create_failed = true;
                return;
            }
            scaler->setDepthReversed(true);

            bool usage_valid = has_usage("color", color, scaler->colorTextureUsage());
            usage_valid      = has_usage("depth", depth, scaler->depthTextureUsage()) && usage_valid;
            usage_valid      = has_usage("motion", motion, scaler->motionTextureUsage()) && usage_valid;
            usage_valid      = has_usage("output", output, scaler->outputTextureUsage()) && usage_valid;
            if (!usage_valid)
            {
                scaler_destroy();
                create_failed = true;
            }
        }
    }

    // fsr, xess and dlss have no metal backends, metalfx upscales instead, nrd is a passthrough, see NRD_Dispatch
    void RHI_VendorTechnology::Initialize()
    {
        if (!RHI_Device::IsSupportedMetalFx())
        {
            return;
        }

        MTL4::CompilerDescriptor* descriptor = MTL4::CompilerDescriptor::alloc()->init();
        NS::Error* error                     = nullptr;
        metalfx::compiler                    = RHI_Context::device->newCompiler(descriptor, &error);
        descriptor->release();
        if (!metalfx::compiler)
        {
            SP_LOG_ERROR("MetalFX: failed to create a compiler");
            return;
        }

        metalfx::fence      = RHI_Context::device->newFence();
        metalfx::fence_work = RHI_Context::device->newBuffer(16, MTL::ResourceStorageModePrivate | MTL::ResourceHazardTrackingModeUntracked);
        metal_utility::set_label(metalfx::fence, "metalfx");
        metal_utility::set_label(metalfx::fence_work, "metalfx_fence_work");
        metal_residency::Add(metal_residency::Group::Static, metalfx::fence_work);
    }

    void RHI_VendorTechnology::Shutdown()
    {
        metalfx::scaler_destroy();

        if (metalfx::fence_work)
        {
            metal_residency::Remove(metalfx::fence_work);
            metalfx::fence_work->release();
            metalfx::fence_work = nullptr;
        }

        if (metalfx::fence)
        {
            metalfx::fence->release();
            metalfx::fence = nullptr;
        }

        if (metalfx::compiler)
        {
            metalfx::compiler->release();
            metalfx::compiler = nullptr;
        }
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
        metalfx::cb_frame      = cb_frame;
        metalfx::render_width  = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.x), resolution_scale);
        metalfx::render_height = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.y), resolution_scale);

        const uint32_t render_max_width  = static_cast<uint32_t>(resolution_render.x);
        const uint32_t render_max_height = static_cast<uint32_t>(resolution_render.y);
        const uint32_t output_width      = static_cast<uint32_t>(resolution_output.x);
        const uint32_t output_height     = static_cast<uint32_t>(resolution_output.y);
        const bool resolution_changed    =
            render_max_width  != metalfx::render_max_width  ||
            render_max_height != metalfx::render_max_height ||
            output_width      != metalfx::output_width      ||
            output_height     != metalfx::output_height;

        if (!resolution_changed)
        {
            return;
        }

        metalfx::render_max_width  = render_max_width;
        metalfx::render_max_height = render_max_height;
        metalfx::output_width      = output_width;
        metalfx::output_height     = output_height;
        metalfx::create_failed     = false;

        // metal 4 command buffers don't retain what they use, the scaler can only go once the gpu is done with it
        if (metalfx::scaler)
        {
            RHI_Device::QueueWaitAll();
            metalfx::scaler_destroy();
        }
    }

    void RHI_VendorTechnology::ResetHistory()
    {
        metalfx::reset_history = true;
    }

    bool RHI_VendorTechnology::FSR_IsAvailable()
    {
        return false;
    }

    void RHI_VendorTechnology::FSR_Shutdown()
    {
    }

    void RHI_VendorTechnology::FSR_ResetHistory()
    {
    }

    void RHI_VendorTechnology::FSR_GenerateJitterSample(float* x, float* y)
    {
        *x = 0.0f;
        *y = 0.0f;
    }

    bool RHI_VendorTechnology::FSR_Dispatch(RHI_Texture* color, RHI_Texture* depth, RHI_Texture* velocity, RHI_Texture* output, RHI_Texture* reactive)
    {
        return false;
    }

    float RHI_VendorTechnology::XeSS_GetResolutionScaleMin(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        return 0.0f;
    }

    bool RHI_VendorTechnology::DLSS_IsReady()
    {
        return true;
    }

    float RHI_VendorTechnology::DLSS_GetResolutionScaleMin(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        return 0.0f;
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {
        *x = 0.0f;
        *y = 0.0f;
    }

    void RHI_VendorTechnology::DLSS_GenerateJitterSample(float* x, float* y)
    {
        *x = 0.0f;
        *y = 0.0f;
    }

    void RHI_VendorTechnology::XeSS_Dispatch(RHI_Texture* tex_color, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output, RHI_Texture* tex_reactive)
    {
    }

    void RHI_VendorTechnology::DLSS_Dispatch(RHI_Texture* tex_color, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output, RHI_Texture* tex_bias)
    {
    }

    float RHI_VendorTechnology::MetalFx_GetResolutionScaleMin(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        if (!RHI_Device::IsSupportedMetalFx() || resolution_render.x <= 0.0f || resolution_render.y <= 0.0f)
        {
            return 0.0f;
        }

        // metalfx upscales by at most this much per axis, less rendered content than that can't fill the output
        const float scale_max = MTLFX::TemporalScalerDescriptor::supportedInputContentMaxScale(RHI_Context::device);
        const float scale_x   = resolution_output.x / (resolution_render.x * scale_max);
        const float scale_y   = resolution_output.y / (resolution_render.y * scale_max);
        return max(scale_x, scale_y);
    }

    bool RHI_VendorTechnology::MetalFx_Dispatch(RHI_Texture* tex_color, RHI_Texture* tex_depth, RHI_Texture* tex_velocity, RHI_Texture* tex_output)
    {
        if (!metalfx::compiler || !metalfx::cb_frame || metalfx::create_failed)
        {
            return false;
        }

        MTL::Texture* color  = static_cast<MTL::Texture*>(tex_color->GetRhiResource());
        MTL::Texture* depth  = static_cast<MTL::Texture*>(tex_depth->GetRhiResource());
        MTL::Texture* motion = static_cast<MTL::Texture*>(tex_velocity->GetRhiResource());
        MTL::Texture* output = static_cast<MTL::Texture*>(tex_output->GetRhiResource());
        if (!metalfx::scaler)
        {
            metalfx::scaler_create(color, depth, motion, output);
            if (!metalfx::scaler)
            {
                return false;
            }
        }

        RHI_CommandList* cmd_list   = RHI_Device::Cmd();
        metal_encoder::State& state = *static_cast<metal_encoder::State*>(cmd_list->GetRhiState());
        void* cmd_buffer            = cmd_list->GetRhiResource();

        cmd_list->PrepareForExternalWrite(tex_output);
        RHI_CommandList::RenderPassEnd();
        cmd_list->FlushBarriers();

        // metalfx records its own encoders and waits on one fence before it reads, this encoder waits for all earlier work
        // and signals that fence
        MTL4::ComputeCommandEncoder* encoder_before = metal_encoder::compute(cmd_buffer, state, true);
        encoder_before->fillBuffer(metalfx::fence_work, NS::Range(0, 4), 0);
        encoder_before->updateFence(metalfx::fence, MTL::StageDispatch | MTL::StageBlit);
        metal_encoder::end(state);

        // the jitter is the ndc offset of the projection, metalfx wants pixels, y down
        const float width  = static_cast<float>(metalfx::render_width);
        const float height = static_cast<float>(metalfx::render_height);
        metalfx::scaler->setJitterOffsetX(metalfx::cb_frame->taa_jitter_current.x * 0.5f * width);
        metalfx::scaler->setJitterOffsetY(-metalfx::cb_frame->taa_jitter_current.y * 0.5f * height);

        // the velocity is current minus previous in ndc, metalfx wants current to previous in pixels, y down
        metalfx::scaler->setMotionVectorScaleX(-0.5f * width);
        metalfx::scaler->setMotionVectorScaleY(0.5f * height);

        metalfx::scaler->setColorTexture(color);
        metalfx::scaler->setDepthTexture(depth);
        metalfx::scaler->setMotionTexture(motion);
        metalfx::scaler->setOutputTexture(output);
        metalfx::scaler->setInputContentWidth(metalfx::render_width);
        metalfx::scaler->setInputContentHeight(metalfx::render_height);
        metalfx::scaler->setReset(metalfx::reset_history);
        metalfx::scaler->setFence(metalfx::fence);
        metalfx::scaler->encodeToCommandBuffer(static_cast<MTL4::CommandBuffer*>(cmd_buffer));
        metalfx::reset_history = false;

        // metalfx signals the fence again after its last write, the work after it waits for that
        MTL4::ComputeCommandEncoder* encoder_after = metal_encoder::compute(cmd_buffer, state, true);
        encoder_after->waitForFence(metalfx::fence, MTL::StageDispatch | MTL::StageBlit);

        cmd_list->AdoptComputeShaderResource(tex_color);
        cmd_list->AdoptComputeShaderResource(tex_depth);
        cmd_list->AdoptComputeShaderResource(tex_velocity);
        cmd_list->AdoptUnorderedAccess(tex_output);
        cmd_list->restore_after_external_pass();

        return true;
    }

    bool RHI_VendorTechnology::NRD_Dispatch(
        Nrd_Preset preset, RHI_Texture* tex_mv, RHI_Texture* tex_normal_roughness, RHI_Texture* tex_view_z, RHI_Texture* tex_signal_in, RHI_Texture* tex_signal_out,
        const Vector3* light_direction, uint32_t shadow_denoiser_index, uint32_t eye, RHI_Texture* tex_specular_in, RHI_Texture* tex_specular_out, bool checkerboard, RHI_Texture* tex_translucency)
    {
        // a copied checkerboard signal stays packed, the renderer expands it itself when this fails
        if (checkerboard)
            return false;

        // nrd has no metal backend, reblur signals go through unfiltered, pack and unpack are exact inverses so the
        // renderer's unpack still remodulates them and averages a still camera into a converged image
        // sigma shadows use another encoding, they take the renderer's own fallback
        if (preset == Nrd_Preset::Shadows || !tex_signal_in || !tex_signal_out)
            return false;

        RHI_CommandList::Copy(tex_signal_in, tex_signal_out, false);
        if (tex_specular_in && tex_specular_out)
        {
            RHI_CommandList::Copy(tex_specular_in, tex_specular_out, false);
        }

        return true;
    }
}
