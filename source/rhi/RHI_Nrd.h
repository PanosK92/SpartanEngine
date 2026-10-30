/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========================
#include "RHI_VendorTechnology.h"
#include "../rendering/Renderer_Buffers.h"
//====================================

// include after NRD.h
#ifdef _WIN32
namespace spartan
{
    namespace nrd_common
    {
        constexpr nrd::Identifier id_gi              = 0;
        constexpr nrd::Identifier id_reflections     = 1;
        constexpr nrd::Identifier id_shadows         = 2;
        constexpr nrd::Identifier id_shadows_local0  = 3;

        // engine uses row vectors (v * m), nrd uses column vectors (m * v), so pass the transpose
        inline void copy_matrix_for_nrd(float destination[16], const math::Matrix& matrix)
        {
            const math::Matrix transposed = matrix.Transposed();
            memcpy(destination, transposed.Data(), sizeof(float) * 16);
        }

        // the frame buffer has no previous view for the right eye, so a right eye instance remembers its own
        struct view_history
        {
            math::Matrix world_to_view;
            math::Matrix view_to_clip;
            bool valid = false;
        };

        inline void fill_common_settings(
            nrd::CommonSettings& settings,
            const Cb_Frame* cb_frame,
            uint32_t width,
            uint32_t height,
            uint32_t rect_width,
            uint32_t rect_height,
            uint32_t rect_width_prev,
            uint32_t rect_height_prev,
            bool reset_history,
            Nrd_Preset preset,
            uint32_t eye,
            view_history& history
        )
        {
            settings = {};

            // unjittered projection only, view_projection_unjittered = view * proj
            const bool right_eye             = eye == 1 && cb_frame->is_multiview != 0;
            const math::Matrix world_to_view = right_eye ? cb_frame->view_right : cb_frame->view;
            const math::Matrix view_to_clip  = math::Matrix::Invert(world_to_view) * (right_eye ? cb_frame->view_projection_unjittered_right : cb_frame->view_projection_unjittered);

            math::Matrix world_to_view_prev = cb_frame->view_previous;
            math::Matrix view_to_clip_prev  = math::Matrix::Invert(cb_frame->view_previous) * cb_frame->view_projection_previous_unjittered;
            if (right_eye)
            {
                const bool has_history = history.valid && !reset_history;
                world_to_view_prev     = has_history ? history.world_to_view : world_to_view;
                view_to_clip_prev      = has_history ? history.view_to_clip : view_to_clip;
            }
            history.world_to_view = world_to_view;
            history.view_to_clip  = view_to_clip;
            history.valid         = true;

            copy_matrix_for_nrd(settings.viewToClipMatrix, view_to_clip);
            copy_matrix_for_nrd(settings.viewToClipMatrixPrev, view_to_clip_prev);
            copy_matrix_for_nrd(settings.worldToViewMatrix, world_to_view);
            copy_matrix_for_nrd(settings.worldToViewMatrixPrev, world_to_view_prev);

            settings.motionVectorScale[0] = 1.0f;
            settings.motionVectorScale[1] = 1.0f;
            // ReSTIR supplies previous-minus-current view depth as well as UV
            // motion. Other presets still rely on NRD's camera-only Z fallback.
            settings.motionVectorScale[2] = preset == Nrd_Preset::Gi ? 1.0f : 0.0f;

            // NRD wants the sample offset in pixels, opposite to the projected geometry displacement.
            // Convert NDC to the denoising grid, including reduced-resolution denoisers.
            auto clamp_nrd_jitter = [](float value) -> float
            {
                if (!std::isfinite(value))
                {
                    return 0.0f;
                }

                return (std::max)(-0.5f, (std::min)(value, 0.5f));
            };

            settings.cameraJitter[0]     = clamp_nrd_jitter(-cb_frame->taa_jitter_current.x * 0.5f * rect_width);
            settings.cameraJitter[1]     = clamp_nrd_jitter(cb_frame->taa_jitter_current.y * 0.5f * rect_height);
            settings.cameraJitterPrev[0] = clamp_nrd_jitter(-cb_frame->taa_jitter_previous.x * 0.5f * rect_width_prev);
            settings.cameraJitterPrev[1] = clamp_nrd_jitter(cb_frame->taa_jitter_previous.y * 0.5f * rect_height_prev);

            // the textures stay full size and resolution scale renders into their top left corner, nrd reconstructs
            // positions and reprojects over the rect, so it has to be the scaled area the image actually covers
            settings.resourceSize[0]     = static_cast<uint16_t>(width);
            settings.resourceSize[1]     = static_cast<uint16_t>(height);
            settings.resourceSizePrev[0] = static_cast<uint16_t>(width);
            settings.resourceSizePrev[1] = static_cast<uint16_t>(height);
            settings.rectSize[0]         = static_cast<uint16_t>(rect_width);
            settings.rectSize[1]         = static_cast<uint16_t>(rect_height);
            settings.rectSizePrev[0]     = static_cast<uint16_t>(rect_width_prev);
            settings.rectSizePrev[1]     = static_cast<uint16_t>(rect_height_prev);

            settings.timeDeltaBetweenFrames     = cb_frame->delta_time * 1000.0f;
            settings.denoisingRange             = (std::max)(cb_frame->camera_far * 0.99f, 1.0f);
            settings.disocclusionThreshold      = preset == Nrd_Preset::Shadows ? 0.02f : 0.01f;
            settings.frameIndex                 = cb_frame->frame;
            settings.accumulationMode           = reset_history ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
            settings.isMotionVectorInWorldSpace = false;
        }

        inline uint32_t get_accumulated_frame_num(float accumulation_time, uint32_t max_frame_num, float delta_time)
        {
            const float fps          = delta_time > 0.0f ? 1.0f / delta_time : 60.0f;
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

        // Unified ReSTIR diffuse/specular, temporal heavy with lobe-aware prefiltering
        inline void fill_preset_gi(nrd::ReblurSettings& settings, float delta_time)
        {
            settings                                   = {};
            // must match the curve restir_pt_nrd_pack encodes, the defaults saturate every hit
            // past a few meters to 1 so reblur loses all contact information and blurs the whole
            // frame at max radius, which reads as colored blobs and kills occlusion darkening
            settings.hitDistanceParameters.A           = restir_hit_distance_a;
            settings.hitDistanceParameters.B           = restir_hit_distance_b;
            settings.hitDistanceParameters.C           = restir_hit_distance_c;
            settings.diffusePrepassBlurRadius          = 8.0f;
            // The unified reservoir supplies a stochastic glossy signal too.
            // Filter it before temporal accumulation, as in the reflection preset.
            settings.specularPrepassBlurRadius         = 30.0f;
            settings.maxBlurRadius                     = 16.0f;
            settings.minBlurRadius                     = 1.0f;
            // Quarter-resolution indirect light needs a longer history than reflections.
            // ReBLUR antilag and disocclusion still discard stale lighting and moving surfaces.
            settings.maxAccumulatedFrameNum            = get_accumulated_frame_num(2.0f, nrd::REBLUR_MAX_HISTORY_FRAME_NUM, delta_time);
            settings.maxFastAccumulatedFrameNum        = (std::min)(settings.maxAccumulatedFrameNum, (std::max)(4u, get_accumulated_frame_num(0.25f, settings.maxAccumulatedFrameNum, delta_time)));
            // History reconstruction requires fewer frames than fast history, including at low FPS.
            settings.historyFixFrameNum                = (std::min)(3u, settings.maxFastAccumulatedFrameNum - 1u);
            settings.minHitDistanceWeight              = 0.08f;
            settings.fireflySuppressorMinRelativeScale = 2.5f;
            settings.enableAntiFirefly                 = true;
        }

        // rt reflections, roughness guided specular reblur
        inline void fill_preset_reflections(nrd::ReblurSettings& settings, float delta_time)
        {
            settings                                   = {};
            settings.diffusePrepassBlurRadius          = 0.0f;
            settings.specularPrepassBlurRadius         = 30.0f;
            settings.maxBlurRadius                     = 30.0f;
            settings.minBlurRadius                     = 1.0f;
            settings.maxAccumulatedFrameNum            = get_accumulated_frame_num(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME, nrd::REBLUR_MAX_HISTORY_FRAME_NUM, delta_time);
            settings.maxFastAccumulatedFrameNum        = get_accumulated_frame_num(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME / 5.0f, settings.maxAccumulatedFrameNum, delta_time);
            settings.minHitDistanceWeight              = 0.1f;
            settings.lobeAngleFraction                 = 0.15f;
            settings.fireflySuppressorMinRelativeScale = 2.0f;
            settings.enableAntiFirefly                 = true;
            // taau already stabilizes the frame, the extra 4k pass is not worth it
            settings.maxStabilizedFrameNum             = 0;
        }

        // directional rt shadows, sigma penumbra reconstruction
        inline void fill_preset_shadows(nrd::SigmaSettings& settings, const float light_direction[3], bool is_local)
        {
            settings                          = {};
            settings.lightDirection[0]        = is_local ? 0.0f : light_direction[0];
            settings.lightDirection[1]        = is_local ? 0.0f : light_direction[1];
            settings.lightDirection[2]        = is_local ? 0.0f : light_direction[2];
            settings.planeDistanceSensitivity = is_local ? 0.04f : 0.02f;
            // the stabilize pass reprojects a receiver's history with the receiver's motion, so the shadow
            // of a moving occluder (the player car) trails behind it and smears out with speed, taau
            // already stabilizes the jittered sun penumbra
            settings.maxStabilizedFrameNum    = 0;
        }
    }
}
#endif
