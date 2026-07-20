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



//= INCLUDES ===============

#include "../Math/Vector2.h"

#include "../Math/Vector3.h"

#include "RHI_Definitions.h"

//==========================



struct FrameBufferData;



namespace spartan

{

    using Cb_Frame = FrameBufferData;



    // easy nrd entry points, each maps to a tuned denoiser

    enum class Nrd_Preset : uint32_t

    {

        Gi,           // reblur diffuse, restir gi

        Reflections,  // reblur specular, rt reflections

        Shadows       // sigma, directional rt shadows

    };



    class RHI_VendorTechnology

    {

    public:

        static void Initialize();

        static void Shutdown();

        static void Tick(Cb_Frame* cb_frame, const math::Vector2& resolution_render, const math::Vector2& resolution_output, const float reslution_scale);

        static void ResetHistory();



        // xess

        static void XeSS_GenerateJitterSample(float* x, float* y);

        static void XeSS_Dispatch(

            RHI_CommandList* cmd_list,

            RHI_Texture* tex_color,

            RHI_Texture* tex_depth,

            RHI_Texture* tex_velocity,

            RHI_Texture* tex_output

        );



        // nrd, guides + signal in/out, light_direction only for shadows

        static bool NRD_Dispatch(

            RHI_CommandList* cmd_list,

            Nrd_Preset preset,

            RHI_Texture* tex_mv,

            RHI_Texture* tex_normal_roughness,

            RHI_Texture* tex_view_z,

            RHI_Texture* tex_signal_in,

            RHI_Texture* tex_signal_out,

            const math::Vector3* light_direction = nullptr

        );

    };

}



// nrd helpers, available only after nrd.h is included (vulkan/d3d12 vendor backends)

#ifdef NRD_VERSION_MAJOR

#include "../Rendering/Renderer_Buffers.h"

#include <cstring>

#include <algorithm>



namespace spartan::nrd_common

{

    inline constexpr nrd::Identifier id_gi          = 0;

    inline constexpr nrd::Identifier id_reflections = 1;

    inline constexpr nrd::Identifier id_shadows     = 2;



    // engine uses row vectors (v * m), nrd uses column vectors (m * v), so pass the transpose

    inline void copy_matrix_for_nrd(float dst[16], const math::Matrix& matrix)

    {

        const math::Matrix transposed = matrix.Transposed();

        memcpy(dst, transposed.Data(), sizeof(float) * 16);

    }



    inline void fill_common_settings(
        nrd::CommonSettings& settings,
        const Cb_Frame* cb_frame,
        uint32_t width,
        uint32_t height,
        bool reset_history,
        Nrd_Preset preset
    )

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

        settings.disocclusionThreshold  = preset == Nrd_Preset::Shadows ? 0.02f : 0.01f;

        settings.frameIndex             = cb_frame->frame;

        settings.accumulationMode       = reset_history ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

        settings.isMotionVectorInWorldSpace = false;

    }



    inline uint32_t get_accumulated_frame_num(float accumulation_time, uint32_t max_frame_num, float delta_time)

    {

        const float fps = delta_time > 0.0f ? 1.0f / delta_time : 60.0f;

        const uint32_t frame_num = nrd::GetMaxAccumulatedFrameNum(accumulation_time, fps);

        return (std::min)((std::max)(frame_num, 1u), max_frame_num);

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



    // restir diffuse gi, temporal heavy, spatial light so material lighting stays

    inline void fill_preset_gi(nrd::ReblurSettings& settings, float delta_time)

    {

        settings = {};

        settings.diffusePrepassBlurRadius          = 0.0f;

        settings.specularPrepassBlurRadius         = 0.0f;

        settings.maxBlurRadius                     = 8.0f;

        settings.minBlurRadius                     = 1.0f;

        settings.maxAccumulatedFrameNum            = get_accumulated_frame_num(
            nrd::REBLUR_DEFAULT_ACCUMULATION_TIME,
            nrd::REBLUR_MAX_HISTORY_FRAME_NUM,
            delta_time
        );

        settings.maxFastAccumulatedFrameNum        = get_accumulated_frame_num(
            nrd::REBLUR_DEFAULT_ACCUMULATION_TIME / 5.0f,
            settings.maxAccumulatedFrameNum,
            delta_time
        );

        settings.minHitDistanceWeight              = 0.08f;

        settings.fireflySuppressorMinRelativeScale = 2.5f;

        settings.enableAntiFirefly                 = true;

    }



    // rt reflections, roughness guided specular reblur

    inline void fill_preset_reflections(nrd::ReblurSettings& settings, float delta_time)

    {

        settings = {};

        settings.diffusePrepassBlurRadius          = 0.0f;

        settings.specularPrepassBlurRadius         = 30.0f;

        settings.maxBlurRadius                     = 30.0f;

        settings.minBlurRadius                     = 1.0f;

        settings.maxAccumulatedFrameNum            = get_accumulated_frame_num(
            nrd::REBLUR_DEFAULT_ACCUMULATION_TIME,
            nrd::REBLUR_MAX_HISTORY_FRAME_NUM,
            delta_time
        );

        settings.maxFastAccumulatedFrameNum        = get_accumulated_frame_num(
            nrd::REBLUR_DEFAULT_ACCUMULATION_TIME / 5.0f,
            settings.maxAccumulatedFrameNum,
            delta_time
        );

        settings.minHitDistanceWeight              = 0.1f;

        settings.lobeAngleFraction                 = 0.15f;

        settings.fireflySuppressorMinRelativeScale = 2.0f;

        settings.enableAntiFirefly                 = true;

    }



    // directional rt shadows, sigma penumbra reconstruction

    inline void fill_preset_shadows(
        nrd::SigmaSettings& settings,
        const float light_direction[3],
        float delta_time
    )

    {

        settings = {};

        settings.lightDirection[0]        = light_direction[0];

        settings.lightDirection[1]        = light_direction[1];

        settings.lightDirection[2]        = light_direction[2];

        settings.planeDistanceSensitivity = 0.02f;

        settings.maxStabilizedFrameNum    = get_accumulated_frame_num(
            nrd::SIGMA_DEFAULT_ACCUMULATION_TIME,
            nrd::SIGMA_MAX_HISTORY_FRAME_NUM,
            delta_time
        );

    }

}

#endif


