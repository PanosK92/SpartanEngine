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

#include "../Rendering/Renderer_Buffers.h"
#include <cstring>
#include <algorithm>

// shared nrd helpers for d3d12 and vulkan vendor technology backends

namespace spartan::nrd_common
{
    inline constexpr nrd::Identifier denoiser_id = 0;

    // engine uses row vectors (v * m), nrd uses column vectors (m * v), so pass the transpose
    inline void copy_matrix_for_nrd(float dst[16], const math::Matrix& matrix)
    {
        const math::Matrix transposed = matrix.Transposed();
        memcpy(dst, transposed.Data(), sizeof(float) * 16);
    }

    inline void fill_common_settings(nrd::CommonSettings& settings, const Cb_Frame* cb_frame, uint32_t width, uint32_t height, bool reset_history)
    {
        settings = {};

        // unjittered projection only, view_projection_unjittered = view * proj
        const math::Matrix view_to_clip      = math::Matrix::Invert(cb_frame->view) * cb_frame->view_projection_unjittered;
        const math::Matrix view_to_clip_prev = math::Matrix::Invert(cb_frame->view_previous) * cb_frame->view_projection_previous_unjittered;

        copy_matrix_for_nrd(settings.viewToClipMatrix, view_to_clip);
        copy_matrix_for_nrd(settings.viewToClipMatrixPrev, view_to_clip_prev);
        copy_matrix_for_nrd(settings.worldToViewMatrix, cb_frame->view);
        copy_matrix_for_nrd(settings.worldToViewMatrixPrev, cb_frame->view_previous);

        settings.motionVectorScale[0] = 1.0f;
        settings.motionVectorScale[1] = 1.0f;
        settings.motionVectorScale[2] = 0.0f;

        // taa jitter is clip space xy, nrd wants uv space sample offset in [-0.5, 0.5]
        settings.cameraJitter[0]     = cb_frame->taa_jitter_current.x * 0.5f;
        settings.cameraJitter[1]     = -cb_frame->taa_jitter_current.y * 0.5f;
        settings.cameraJitterPrev[0] = cb_frame->taa_jitter_previous.x * 0.5f;
        settings.cameraJitterPrev[1] = -cb_frame->taa_jitter_previous.y * 0.5f;

        settings.resourceSize[0]     = static_cast<uint16_t>(width);
        settings.resourceSize[1]     = static_cast<uint16_t>(height);
        settings.resourceSizePrev[0] = static_cast<uint16_t>(width);
        settings.resourceSizePrev[1] = static_cast<uint16_t>(height);
        settings.rectSize[0]         = static_cast<uint16_t>(width);
        settings.rectSize[1]         = static_cast<uint16_t>(height);
        settings.rectSizePrev[0]     = static_cast<uint16_t>(width);
        settings.rectSizePrev[1]     = static_cast<uint16_t>(height);

        settings.timeDeltaBetweenFrames = cb_frame->delta_time * 1000.0f;
        settings.denoisingRange         = (std::max)(cb_frame->camera_far * 0.99f, 1.0f);
        settings.frameIndex             = cb_frame->frame;
        settings.accumulationMode       = reset_history ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
        settings.isMotionVectorInWorldSpace = false;
    }

    // nrd integration asserts frameIndex advances by 1, scene loads and minimize skip frames
    inline bool resolve_history_reset(bool reset_requested, uint32_t engine_frame, uint32_t& last_settings_frame)
    {
        bool reset = reset_requested;
        if (last_settings_frame != UINT32_MAX && engine_frame != last_settings_frame && engine_frame != last_settings_frame + 1)
        {
            reset = true;
        }
        last_settings_frame = engine_frame;
        return reset;
    }

    // restir is already reused, kill grain with temporal history, keep spatial tiny so smooth surfaces keep lighting variation
    inline void fill_reblur_settings_for_restir(nrd::ReblurSettings& settings)
    {
        settings = {};
        settings.diffusePrepassBlurRadius          = 0.0f;
        settings.specularPrepassBlurRadius         = 0.0f;
        settings.maxBlurRadius                     = 5.0f;
        settings.minBlurRadius                     = 0.0f;
        settings.maxAccumulatedFrameNum            = 48;
        settings.maxFastAccumulatedFrameNum        = 8;
        settings.minHitDistanceWeight              = 0.05f;
        settings.fireflySuppressorMinRelativeScale = 3.0f;
        settings.enableAntiFirefly                 = false;
    }
}
