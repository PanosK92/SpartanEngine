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

//= INCLUDES =================================
#include "pch.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <xess/xess_d3d12.h>
#define NRD_STATIC_LIBRARY
#include "NRD.h"
#include "NRI.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "NRDIntegration.hpp"
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#endif
SP_WARNINGS_ON
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Texture.h"
#include "../../rendering/Renderer.h"
#include "../../rendering/Renderer_Buffers.h"
#include "../../world/World.h"
#include "../../world/components/Camera.h"
#include <cmath>
#include <filesystem>
//============================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    #ifdef _WIN32
    namespace nrd_common
    {
        constexpr nrd::Identifier id_gi              = 0;
        constexpr nrd::Identifier id_reflections     = 1;
        constexpr nrd::Identifier id_shadows         = 2;
        constexpr nrd::Identifier id_shadows_local0  = 3;

        // engine uses row vectors (v * m), nrd uses column vectors (m * v), so pass the transpose
        void copy_matrix_for_nrd(float destination[16], const math::Matrix& matrix)
        {
            const math::Matrix transposed = matrix.Transposed();
            memcpy(destination, transposed.Data(), sizeof(float) * 16);
        }

        void fill_common_settings(
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

            settings.timeDeltaBetweenFrames     = cb_frame->delta_time * 1000.0f;
            settings.denoisingRange             = (std::max)(cb_frame->camera_far * 0.99f, 1.0f);
            settings.disocclusionThreshold      = preset == Nrd_Preset::Shadows ? 0.02f : 0.01f;
            settings.frameIndex                 = cb_frame->frame;
            settings.accumulationMode           = reset_history ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
            settings.isMotionVectorInWorldSpace = false;
        }

        uint32_t get_accumulated_frame_num(float accumulation_time, uint32_t max_frame_num, float delta_time)
        {
            const float fps          = delta_time > 0.0f ? 1.0f / delta_time : 60.0f;
            const uint32_t frame_num = nrd::GetMaxAccumulatedFrameNum(accumulation_time, fps);

            return (std::min)((std::max)(frame_num, 1u), max_frame_num);
        }

        // nrd integration asserts frameIndex advances by 1, scene loads and minimize skip frames
        bool resolve_history_reset(bool reset_requested, uint32_t engine_frame, uint32_t& last_settings_frame)
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
        void fill_preset_gi(nrd::ReblurSettings& settings, float delta_time)
        {
            settings                                   = {};
            // must match the curve restir_pt_nrd_pack encodes, the defaults saturate every hit
            // past a few meters to 1 so reblur loses all contact information and blurs the whole
            // frame at max radius, which reads as colored blobs and kills occlusion darkening
            settings.hitDistanceParameters.A           = restir_hit_distance_a;
            settings.hitDistanceParameters.B           = restir_hit_distance_b;
            settings.hitDistanceParameters.C           = restir_hit_distance_c;
            settings.diffusePrepassBlurRadius          = 0.0f;
            settings.specularPrepassBlurRadius         = 0.0f;
            settings.maxBlurRadius                     = 8.0f;
            settings.minBlurRadius                     = 1.0f;
            settings.maxAccumulatedFrameNum            = get_accumulated_frame_num(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME, nrd::REBLUR_MAX_HISTORY_FRAME_NUM, delta_time);
            settings.maxFastAccumulatedFrameNum        = get_accumulated_frame_num(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME / 5.0f, settings.maxAccumulatedFrameNum, delta_time);
            settings.minHitDistanceWeight              = 0.08f;
            settings.fireflySuppressorMinRelativeScale = 2.5f;
            settings.enableAntiFirefly                 = true;
        }

        // rt reflections, roughness guided specular reblur
        void fill_preset_reflections(nrd::ReblurSettings& settings, float delta_time)
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
        void fill_preset_shadows(nrd::SigmaSettings& settings, const float light_direction[3], float delta_time, bool is_local)
        {
            settings                          = {};
            settings.lightDirection[0]        = is_local ? 0.0f : light_direction[0];
            settings.lightDirection[1]        = is_local ? 0.0f : light_direction[1];
            settings.lightDirection[2]        = is_local ? 0.0f : light_direction[2];
            settings.planeDistanceSensitivity = is_local ? 0.04f : 0.02f;
            // local lights trace to the emitter center so the input is already stable, the
            // stabilize pass would only reproject the penumbra of a mover and smear it
            settings.maxStabilizedFrameNum    = is_local
                ? 0
                : get_accumulated_frame_num(nrd::SIGMA_DEFAULT_ACCUMULATION_TIME, nrd::SIGMA_MAX_HISTORY_FRAME_NUM, delta_time);
        }
    }

    namespace common
    {
        uint32_t resolution_render_width      = 0; // scaled (render * scale), used for per-frame dispatch
        uint32_t resolution_render_height     = 0;
        uint32_t resolution_render_max_width  = 0; // unscaled base render resolution
        uint32_t resolution_render_max_height = 0;
        uint32_t resolution_output_width      = 0;
        uint32_t resolution_output_height     = 0;
        bool reset_history                    = false;
        float resolution_scale                = 1.0f;
        Cb_Frame* cb_frame                    = nullptr;

        uint32_t get_upscaler_sample_count()
        {
            float render_w = static_cast<float>((std::max)(1u, resolution_render_width));
            float scale    = static_cast<float>(resolution_output_width) / render_w;
            float raw      = 8.0f * scale * scale;
            uint32_t count = static_cast<uint32_t>(std::ceil(raw));
            return (std::max)(1u, count);
        }

        void write_projection_jitter(float pixel_x, float pixel_y, float* x, float* y)
        {
            *x =  2.0f * pixel_x / static_cast<float>((std::max)(1u, resolution_render_width));
            *y = -2.0f * pixel_y / static_cast<float>((std::max)(1u, resolution_render_height));
        }
    }

    namespace intel
    {
        xess_context_handle_t context              = nullptr;
        xess_d3d12_init_params_t params_init       = {};
        xess_d3d12_execute_params_t params_execute = {};
        Vector2 jitter                             = Vector2::Zero;
        const float responsive_mask_value_max      = 0.05f;
        const float exposure_scale                 = 1.0f;
        xess_quality_settings_t quality            = XESS_QUALITY_SETTING_BALANCED;

        xess_quality_settings_t get_quality(const float scale_factor)
        {
            struct QualitySetting
            {
                xess_quality_settings_t quality;
                float scale_factor;
            };

            const QualitySetting quality_settings[] =
            {
                { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,  0.25f },
                { XESS_QUALITY_SETTING_PERFORMANCE,        0.36f },
                { XESS_QUALITY_SETTING_BALANCED,           0.49f },
                { XESS_QUALITY_SETTING_QUALITY,            0.64f },
                { XESS_QUALITY_SETTING_ULTRA_QUALITY,      0.81f },
                { XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, 0.91f },
                { XESS_QUALITY_SETTING_AA,                 1.0f  }
            };

            quality              = XESS_QUALITY_SETTING_BALANCED;
            float min_difference = numeric_limits<float>::max();

            for (const auto& setting : quality_settings)
            {
                float difference = abs(scale_factor - setting.scale_factor);
                if (difference < min_difference)
                {
                    min_difference = difference;
                    quality        = setting.quality;
                }
            }

            return quality;
        }

        void context_destroy()
        {
            if (context)
            {
                xessDestroyContext(context);
                context = nullptr;
            }
        }

        void context_create()
        {
            if (!RHI_Device::IsSupportedXess())
            {
                return;
            }

            context_destroy();

            // real support probe, context creation fails on drivers or hardware that can not run xess,
            // bail gracefully instead of asserting so the renderer falls back to another upscaler
            if (xessD3D12CreateContext(RHI_Context::device, &context) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS context creation failed, disabling XeSS");
                context = nullptr;
                return;
            }

            // calculate the scaling factor using the base (unscaled) render resolution
            uint32_t render_area = common::resolution_render_max_width * common::resolution_render_max_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float scale_factor   = static_cast<float>(render_area) / static_cast<float>(output_area);

            intel::params_init                      = {};
            intel::params_init.outputResolution.x   = common::resolution_output_width;
            intel::params_init.outputResolution.y   = common::resolution_output_height;
            intel::params_init.qualitySetting       = intel::get_quality(scale_factor);
            // xess computes its own exposure, feeding it the tonemapper's value would lag its training assumptions
            intel::params_init.initFlags            = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
            intel::params_init.creationNodeMask     = 0;
            intel::params_init.visibleNodeMask      = 0;
            intel::params_init.pTempBufferHeap      = nullptr;
            intel::params_init.bufferHeapOffset     = 0;
            intel::params_init.pTempTextureHeap     = nullptr;
            intel::params_init.textureHeapOffset    = 0;
            // keep xess off the engine pipeline library, its cold load names spam the debug layer
            intel::params_init.pPipelineLibrary     = nullptr;

            // xess manages its own shader-visible descriptor heap internally when none is supplied at execute time,
            // so no engine heap needs to be handed over here, this keeps the bindless heap free of xess descriptors
            if (xessD3D12Init(intel::context, &intel::params_init) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS initialization failed, disabling XeSS");
                context_destroy();
                return;
            }

            if (xessSetVelocityScale(intel::context, -1.0f, -1.0f) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS velocity scale configuration failed");
            }

            if (xessSetMaxResponsiveMaskValue(intel::context, intel::responsive_mask_value_max) != xess_result_t::XESS_RESULT_SUCCESS)
            {
                SP_LOG_WARNING("XeSS responsive mask configuration failed");
            }
        }

        uint32_t get_sample_count()
        {
            uint32_t count = 0;

            switch (quality)
            {
            case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            case XESS_QUALITY_SETTING_ULTRA_QUALITY:    count = 48; break;
            case XESS_QUALITY_SETTING_QUALITY:          count = 48; break;
            case XESS_QUALITY_SETTING_BALANCED:         count = 64; break;
            case XESS_QUALITY_SETTING_PERFORMANCE:      count = 80; break;
            case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:count = 96; break;
            case XESS_QUALITY_SETTING_AA:               count = 32; break;
            }

            return count;
        }
    }

    namespace dlss
    {
        bool sdk_ready                        = false;
        bool create_failed                    = false;
        NVSDK_NGX_Parameter* parameters       = nullptr;
        NVSDK_NGX_Handle* handle              = nullptr;
        Vector2 jitter                        = Vector2::Zero;
        wstring data_path;
        const wchar_t* dll_dir                = nullptr;
        NVSDK_NGX_PerfQuality_Value quality   = NVSDK_NGX_PerfQuality_Value_Balanced;

        NVSDK_NGX_PerfQuality_Value get_quality(const float scale_factor)
        {
            struct QualitySetting
            {
                NVSDK_NGX_PerfQuality_Value quality;
                float scale_factor;
            };

            const QualitySetting quality_settings[] =
            {
                { NVSDK_NGX_PerfQuality_Value_UltraPerformance, 0.11f },
                { NVSDK_NGX_PerfQuality_Value_MaxPerf,          0.25f },
                { NVSDK_NGX_PerfQuality_Value_Balanced,         0.34f },
                { NVSDK_NGX_PerfQuality_Value_MaxQuality,       0.44f },
                { NVSDK_NGX_PerfQuality_Value_UltraQuality,     0.59f },
                { NVSDK_NGX_PerfQuality_Value_DLAA,             1.0f  }
            };

            quality              = NVSDK_NGX_PerfQuality_Value_Balanced;
            float min_difference = numeric_limits<float>::max();
            for (const auto& setting : quality_settings)
            {
                float difference = abs(scale_factor - setting.scale_factor);
                if (difference < min_difference)
                {
                    min_difference = difference;
                    quality        = setting.quality;
                }
            }

            return quality;
        }

        void feature_destroy()
        {
            if (handle)
            {
                NVSDK_NGX_D3D12_ReleaseFeature(handle);
                handle = nullptr;
            }
            create_failed = false;
        }

        void sdk_shutdown()
        {
            feature_destroy();
            if (parameters)
            {
                NVSDK_NGX_D3D12_DestroyParameters(parameters);
                parameters = nullptr;
            }
            if (sdk_ready)
            {
                NVSDK_NGX_D3D12_Shutdown1(RHI_Context::device);
                sdk_ready = false;
            }
        }

        void sdk_init()
        {
            if (!RHI_Device::IsSupportedDlss() || sdk_ready)
            {
                return;
            }

            data_path = filesystem::path(FileSystem::GetExecutableDirectory()).wstring();
            dll_dir   = data_path.c_str();

            NVSDK_NGX_FeatureCommonInfo info = {};
            info.PathListInfo.Path           = &dll_dir;
            info.PathListInfo.Length         = 1;

            NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(
                RHI_VendorTechnology::dlss_project_id,
                NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                RHI_VendorTechnology::dlss_engine_version,
                data_path.c_str(),
                RHI_Context::device,
                &info
            );
            if (NVSDK_NGX_FAILED(result))
            {
                SP_LOG_WARNING("DLSS ngx init failed: 0x%x", static_cast<unsigned int>(result));
                return;
            }

            result = NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters);
            if (NVSDK_NGX_FAILED(result) || !parameters)
            {
                SP_LOG_WARNING("DLSS capability parameters failed: 0x%x", static_cast<unsigned int>(result));
                NVSDK_NGX_D3D12_Shutdown1(RHI_Context::device);
                parameters = nullptr;
                return;
            }

            sdk_ready = true;

            unsigned int available    = 0;
            unsigned int needs_driver = 0;
            int init_result           = 0;
            NVSDK_NGX_Parameter_GetUI(parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
            NVSDK_NGX_Parameter_GetUI(parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
            NVSDK_NGX_Parameter_GetI(parameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
            if (!available)
            {
                SP_LOG_WARNING("DLSS super sampling unavailable, needs_driver=%u, init_result=0x%x", needs_driver, static_cast<unsigned int>(init_result));
            }
        }

        void set_dlss4_presets()
        {
            const unsigned int preset_quality = static_cast<unsigned int>(NVSDK_NGX_DLSS_Hint_Render_Preset_K);
            const unsigned int preset_perf    = static_cast<unsigned int>(NVSDK_NGX_DLSS_Hint_Render_Preset_M);
            const unsigned int preset_ultra   = static_cast<unsigned int>(NVSDK_NGX_DLSS_Hint_Render_Preset_L);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,             preset_quality);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,          preset_quality);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,         preset_quality);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,     preset_quality);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,      preset_perf);
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset_ultra);
        }

        void feature_create(ID3D12GraphicsCommandList* cmd_list)
        {
            if (!sdk_ready || handle || create_failed || !cmd_list || common::resolution_render_max_width == 0)
            {
                return;
            }

            uint32_t render_area = common::resolution_render_max_width * common::resolution_render_max_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float scale_factor   = static_cast<float>(render_area) / static_cast<float>((std::max)(1u, output_area));
            quality              = get_quality(scale_factor);

            set_dlss4_presets();

            unsigned int opt_w = 0;
            unsigned int opt_h = 0;
            unsigned int max_w = 0;
            unsigned int max_h = 0;
            unsigned int min_w = 0;
            unsigned int min_h = 0;
            float sharpness    = 0.0f;
            NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
                parameters,
                common::resolution_output_width,
                common::resolution_output_height,
                quality,
                &opt_w, &opt_h, &max_w, &max_h, &min_w, &min_h, &sharpness
            );
            if (NVSDK_NGX_FAILED(result))
            {
                SP_LOG_WARNING("DLSS optimal settings failed: 0x%x", static_cast<unsigned int>(result));
                create_failed = true;
                return;
            }

            uint32_t in_w = common::resolution_render_max_width;
            uint32_t in_h = common::resolution_render_max_height;
            if (max_w != 0 && max_h != 0)
            {
                in_w = (std::min)((std::max)(in_w, min_w), max_w);
                in_h = (std::min)((std::max)(in_h, min_h), max_h);
            }

            NVSDK_NGX_DLSS_Create_Params create = {};
            create.Feature.InWidth            = in_w;
            create.Feature.InHeight           = in_h;
            create.Feature.InTargetWidth      = common::resolution_output_width;
            create.Feature.InTargetHeight     = common::resolution_output_height;
            create.Feature.InPerfQualityValue = quality;
            create.InFeatureCreateFlags       = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                                                NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                                NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
                                                NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            create.InEnableOutputSubrects     = false;

            result = NGX_D3D12_CREATE_DLSS_EXT(cmd_list, 1, 1, &handle, parameters, &create);
            if (NVSDK_NGX_FAILED(result))
            {
                SP_LOG_WARNING("DLSS feature creation failed: 0x%x", static_cast<unsigned int>(result));
                handle        = nullptr;
                create_failed = true;
            }
        }
    }

    namespace nvidia
    {
        // gi is restir sized, reflections and shadows are render sized
        // every preset owns its own instance, CommandBufferD3D12Desc carries no queue type so nri derives it from the
        // command list it is handed, and an instance fed from two list types would see its internal textures left in
        // non pixel only by one dispatch and claimed as non pixel plus pixel by the next
        struct nrd_pool
        {
            nrd::Integration integration;
            bool initialized             = false;
            uint32_t width               = 0;
            uint32_t height              = 0;
            bool reset_history           = false;
            uint32_t last_frame_index    = UINT32_MAX;
            uint32_t last_settings_frame = UINT32_MAX;
            RHI_Queue_Type queue_type    = RHI_Queue_Type::Max;
            ID3D12CommandQueue* queue    = nullptr;
        };

        nrd_pool pool_gi;
        nrd_pool pool_reflections;
        nrd_pool pool_shadows;

        nrd::Resource make_resource(RHI_Texture* texture, bool storage)
        {
            nrd::Resource resource = {};
            resource.d3d12.resource = static_cast<ID3D12Resource*>(texture->GetRhiResource());
            resource.d3d12.format   = static_cast<DXGIFormat>(d3d12_format[rhi_format_to_index(texture->GetFormat())]);
            resource.state = storage
                ? nri::AccessLayoutStage{ nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::Layout::GENERAL, nri::StageBits::COMPUTE_SHADER }
                : nri::AccessLayoutStage{ nri::AccessBits::SHADER_RESOURCE, nri::Layout::GENERAL, nri::StageBits::COMPUTE_SHADER };
            resource.userArg        = texture;
            return resource;
        }

        void pool_destroy(nrd_pool& pool)
        {
            if (pool.initialized)
            {
                pool.integration.Destroy();
                pool.initialized = false;
            }
            pool.width               = 0;
            pool.height              = 0;
            pool.last_frame_index    = UINT32_MAX;
            pool.last_settings_frame = UINT32_MAX;
        }

        void context_destroy()
        {
            pool_destroy(pool_gi);
            pool_destroy(pool_reflections);
            pool_destroy(pool_shadows);
        }

        void request_history_reset()
        {
            pool_gi.reset_history          = true;
            pool_reflections.reset_history = true;
            pool_shadows.reset_history     = true;
        }

        bool pool_create(nrd_pool& pool, uint32_t resource_width, uint32_t resource_height, Nrd_Preset preset, RHI_Queue_Type queue_type)
        {
            pool_destroy(pool);
            pool.queue_type = queue_type;

            pool.queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(queue_type));
            if (!RHI_Context::device || !pool.queue || resource_width == 0 || resource_height == 0)
            {
                return false;
            }

            // the family has to describe the queue this preset is actually dispatched from, nri uses it for its own
            // idle waits and for turning access bits into legacy resource states
            nri::QueueFamilyD3D12Desc queue_family = {};
            queue_family.d3d12Queues = &pool.queue;
            queue_family.queueNum    = 1;
            queue_family.queueType   = queue_type == RHI_Queue_Type::Graphics ? nri::QueueType::GRAPHICS : nri::QueueType::COMPUTE;

            nri::DeviceCreationD3D12Desc device_desc = {};
            device_desc.d3d12Device                  = RHI_Context::device;
            device_desc.queueFamilies                = &queue_family;
            device_desc.queueFamilyNum               = 1;
            device_desc.disableD3D12EnhancedBarriers = true;

            nrd::DenoiserDesc denoisers[5];
            uint32_t denoiser_count = 1;
            denoisers[0]            = { nrd_common::id_gi, nrd::Denoiser::REBLUR_DIFFUSE };
            const char* pool_name   = "NRD_GI";
            if (preset == Nrd_Preset::Reflections)
            {
                denoisers[0] = { nrd_common::id_reflections, nrd::Denoiser::REBLUR_SPECULAR };
                pool_name    = "NRD_Reflections";
            }
            else if (preset == Nrd_Preset::Shadows)
            {
                denoisers[0] = { nrd_common::id_shadows, nrd::Denoiser::SIGMA_SHADOW };
                denoisers[1] = { nrd_common::id_shadows_local0, nrd::Denoiser::SIGMA_SHADOW };
                denoisers[2] = { nrd_common::id_shadows_local0 + 1, nrd::Denoiser::SIGMA_SHADOW };
                denoisers[3] = { nrd_common::id_shadows_local0 + 2, nrd::Denoiser::SIGMA_SHADOW };
                denoisers[4] = { nrd_common::id_shadows_local0 + 3, nrd::Denoiser::SIGMA_SHADOW };
                denoiser_count = 5;
                pool_name      = "NRD_Shadows";
            }

            nrd::InstanceCreationDesc instance_desc = {};
            instance_desc.denoisers                 = denoisers;
            instance_desc.denoisersNum              = denoiser_count;

            nrd::IntegrationCreationDesc integration_desc = {};
            strncpy_s(integration_desc.name, pool_name, _TRUNCATE);
            integration_desc.queuedFrameNum                       = 3;
            integration_desc.enableWholeLifetimeDescriptorCaching = false;
            integration_desc.autoWaitForIdle                      = true;
            integration_desc.resourceWidth                        = static_cast<uint16_t>(resource_width);
            integration_desc.resourceHeight                       = static_cast<uint16_t>(resource_height);

            if (pool.integration.RecreateD3D12(integration_desc, instance_desc, device_desc) != nrd::Result::SUCCESS)
            {
                SP_LOG_WARNING("NRD recreate failed");
                return false;
            }

            pool.width       = resource_width;
            pool.height      = resource_height;
            pool.initialized = true;
            return true;
        }

        nrd_pool& pool_for_preset(Nrd_Preset preset)
        {
            if (preset == Nrd_Preset::Gi)
            {
                return pool_gi;
            }

            return preset == Nrd_Preset::Reflections ? pool_reflections : pool_shadows;
        }
    }
    #endif

    void RHI_VendorTechnology::Initialize()
    {
    #ifdef _WIN32
        dlss::sdk_init();
    #endif
    }

    void RHI_VendorTechnology::Shutdown()
    {
    #ifdef _WIN32
        intel::context_destroy();
        dlss::sdk_shutdown();
        nvidia::context_destroy();
    #endif
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
    #ifdef _WIN32
        common::cb_frame         = cb_frame;
        common::resolution_scale = resolution_scale;

        common::resolution_render_width  = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.x), resolution_scale);
        common::resolution_render_height = Renderer::GetScaledDimension(static_cast<uint32_t>(resolution_render.y), resolution_scale);

        uint32_t base_render_width  = static_cast<uint32_t>(resolution_render.x);
        uint32_t base_render_height = static_cast<uint32_t>(resolution_render.y);
        uint32_t output_width       = static_cast<uint32_t>(resolution_output.x);
        uint32_t output_height      = static_cast<uint32_t>(resolution_output.y);

        bool base_render_changed = base_render_width  != common::resolution_render_max_width ||
                                   base_render_height != common::resolution_render_max_height;
        bool output_changed      = output_width  != common::resolution_output_width ||
                                   output_height != common::resolution_output_height;

        common::resolution_render_max_width  = base_render_width;
        common::resolution_render_max_height = base_render_height;
        common::resolution_output_width      = output_width;
        common::resolution_output_height     = output_height;

        if (base_render_changed || output_changed)
        {
            RHI_Device::QueueWaitAll();
            intel::context_create();
            dlss::feature_destroy();
            common::reset_history = true;
            nvidia::request_history_reset();
            nvidia::context_destroy();
        }
    #endif
    }

    void RHI_VendorTechnology::ResetHistory()
    {
    #ifdef _WIN32
        common::reset_history = true;
        nvidia::request_history_reset();
    #endif
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {
    #ifdef _WIN32
        // generate a single halton value for a given base and index
        auto get_corput = [](uint32_t index, uint32_t base) -> float
        {
            float result = 0.0f;
            float bk     = 1.0f;
            while (index > 0)
            {
                bk     /= static_cast<float>(base);
                result += static_cast<float>(index % base) * bk;
                index  /= base;
            }
            return result;
        };

        static vector<pair<float, float>> halton_points;
        static size_t halton_index = 0;

        // restart the sequence whenever history is wiped so the first accumulated frame
        // samples a known sub-pixel position rather than continuing from an arbitrary phase
        if (common::reset_history)
        {
            halton_index = 0;
        }

        if (halton_points.empty())
        {
            const uint32_t xess_sample_limit = 96;

            uint32_t base_x      = 2;
            uint32_t base_y      = 3;
            uint32_t start_index = 1;
            halton_points.reserve(xess_sample_limit);
            for (uint32_t i = start_index; i < start_index + xess_sample_limit; ++i)
            {
                float jitter_x = get_corput(i, base_x) - 0.5f;
                float jitter_y = get_corput(i, base_y) - 0.5f;
                halton_points.emplace_back(jitter_x, jitter_y);
            }
        }

        auto jitter = halton_points[halton_index];

        intel::jitter.x = jitter.first;
        intel::jitter.y = jitter.second;

        *x =  2.0f * jitter.first  / static_cast<float>(common::resolution_render_width);
        *y = -2.0f * jitter.second / static_cast<float>(common::resolution_render_height);

        uint32_t sample_count_at_current_quality_level = intel::get_sample_count();
        halton_index = (halton_index + 1) % sample_count_at_current_quality_level;
    #endif
    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
    #ifdef _WIN32
        if (!intel::context)
        {
            return;
        }

        // d3d12 xess wants non_pixel_shader_resource for inputs and unordered_access for output,
        // shader_read includes pixel which xess silently strips, desyncing our state tracker
        RHI_Texture* tex_mask = Renderer::GetStandardTexture(Renderer_StandardTexture::Black);
        cmd_list->EnsureComputeShaderResource(tex_color);
        cmd_list->EnsureComputeShaderResource(tex_velocity);
        cmd_list->EnsureComputeShaderResource(tex_depth);
        cmd_list->EnsureComputeShaderResource(tex_mask);
        cmd_list->PrepareForExternalWrite(tex_output);
        cmd_list->FlushBarriers();

        intel::params_execute                              = {};
        intel::params_execute.pColorTexture                = static_cast<ID3D12Resource*>(tex_color->GetRhiResource());
        intel::params_execute.pVelocityTexture             = static_cast<ID3D12Resource*>(tex_velocity->GetRhiResource());
        intel::params_execute.pDepthTexture                = static_cast<ID3D12Resource*>(tex_depth->GetRhiResource());
        intel::params_execute.pExposureScaleTexture        = nullptr; // ignored, autoexposure flag is set
        intel::params_execute.pResponsivePixelMaskTexture  = static_cast<ID3D12Resource*>(tex_mask->GetRhiResource());
        intel::params_execute.pOutputTexture               = static_cast<ID3D12Resource*>(tex_output->GetRhiResource());
        intel::params_execute.jitterOffsetX                = intel::jitter.x;
        intel::params_execute.jitterOffsetY                = intel::jitter.y;
        intel::params_execute.exposureScale                = intel::exposure_scale;
        intel::params_execute.resetHistory                 = common::reset_history ? 1 : 0;
        intel::params_execute.inputWidth                   = common::resolution_render_width;
        intel::params_execute.inputHeight                  = common::resolution_render_height;
        intel::params_execute.inputColorBase               = { 0, 0 };
        intel::params_execute.inputMotionVectorBase        = { 0, 0 };
        intel::params_execute.inputDepthBase               = { 0, 0 };
        intel::params_execute.inputResponsiveMaskBase      = { 0, 0 };
        intel::params_execute.reserved0                    = { 0, 0 };
        intel::params_execute.outputColorBase              = { 0, 0 };
        intel::params_execute.pDescriptorHeap              = nullptr;
        intel::params_execute.descriptorHeapOffset         = 0;

        common::reset_history = false;

        ID3D12GraphicsCommandList* d3d12_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        xess_result_t result = xessD3D12Execute(intel::context, d3d12_cmd_list, &intel::params_execute);
        if (result != XESS_RESULT_SUCCESS)
        {
            SP_LOG_WARNING("XeSS dispatch failed with result %d", static_cast<int>(result));
        }

        // xess may have rebarriered, adopt the states it leaves so later transitions use a correct state_before
        cmd_list->AdoptComputeShaderResource(tex_color);
        cmd_list->AdoptComputeShaderResource(tex_velocity);
        cmd_list->AdoptComputeShaderResource(tex_depth);
        cmd_list->AdoptComputeShaderResource(tex_mask);
        cmd_list->AdoptUnorderedAccess(tex_output);

        // xess binds its own descriptor heap when pDescriptorHeap is null, restore engine heaps/root sig state
        cmd_list->restore_after_external_pass();
    #endif
    }

    void RHI_VendorTechnology::DLSS_GenerateJitterSample(float* x, float* y)
    {
    #ifdef _WIN32
        auto get_corput = [](uint32_t index, uint32_t base) -> float
        {
            float result = 0.0f;
            float bk     = 1.0f;
            while (index > 0)
            {
                bk     /= static_cast<float>(base);
                result += static_cast<float>(index % base) * bk;
                index  /= base;
            }
            return result;
        };

        static vector<pair<float, float>> halton_points;
        static size_t halton_index = 0;

        if (common::reset_history)
        {
            halton_index = 0;
        }

        if (halton_points.empty())
        {
            const uint32_t sample_limit = 96;
            halton_points.reserve(sample_limit);
            for (uint32_t i = 1; i < 1 + sample_limit; ++i)
            {
                halton_points.emplace_back(get_corput(i, 2) - 0.5f, get_corput(i, 3) - 0.5f);
            }
        }

        auto sample = halton_points[halton_index];
        dlss::jitter.x = sample.first;
        dlss::jitter.y = sample.second;
        common::write_projection_jitter(sample.first, sample.second, x, y);
        halton_index = (halton_index + 1) % common::get_upscaler_sample_count();
    #endif
    }

    void RHI_VendorTechnology::DLSS_Dispatch(
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output,
        RHI_Texture* tex_bias
    )
    {
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
    #ifdef _WIN32
        if (!dlss::sdk_ready || !cmd_list)
        {
            return;
        }

        ID3D12GraphicsCommandList* d3d12_cmd_list = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());
        dlss::feature_create(d3d12_cmd_list);
        if (!dlss::handle)
        {
            return;
        }

        cmd_list->EnsureComputeShaderResource(tex_color);
        cmd_list->EnsureComputeShaderResource(tex_velocity);
        cmd_list->EnsureComputeShaderResource(tex_depth);
        if (tex_bias)
        {
            cmd_list->EnsureComputeShaderResource(tex_bias);
        }
        cmd_list->PrepareForExternalWrite(tex_output);
        cmd_list->FlushBarriers();

        const float render_w = static_cast<float>(common::resolution_render_width);
        const float render_h = static_cast<float>(common::resolution_render_height);

        NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
        eval.Feature.pInColor                 = static_cast<ID3D12Resource*>(tex_color->GetRhiResource());
        eval.Feature.pInOutput                = static_cast<ID3D12Resource*>(tex_output->GetRhiResource());
        eval.pInDepth                         = static_cast<ID3D12Resource*>(tex_depth->GetRhiResource());
        eval.pInMotionVectors                 = static_cast<ID3D12Resource*>(tex_velocity->GetRhiResource());
        eval.InJitterOffsetX                  = dlss::jitter.x;
        eval.InJitterOffsetY                  = dlss::jitter.y;
        eval.InRenderSubrectDimensions.Width  = common::resolution_render_width;
        eval.InRenderSubrectDimensions.Height = common::resolution_render_height;
        eval.InReset                          = common::reset_history ? 1 : 0;
        // engine velocity is unjittered ndc curr minus prev, y up
        // dlss wants pixel motion as prev equals curr plus mv, y down
        eval.InMVScaleX                       = -0.5f * render_w;
        eval.InMVScaleY                       =  0.5f * render_h;
        eval.InPreExposure                    = 1.0f;
        eval.InExposureScale                  = 1.0f;
        if (tex_bias)
        {
            eval.pInBiasCurrentColorMask = static_cast<ID3D12Resource*>(tex_bias->GetRhiResource());
        }
        if (common::cb_frame)
        {
            eval.InFrameTimeDeltaInMsec = common::cb_frame->delta_time * 1000.0f;
        }

        common::reset_history = false;

        NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(d3d12_cmd_list, dlss::handle, dlss::parameters, &eval);
        if (NVSDK_NGX_FAILED(result))
        {
            SP_LOG_WARNING("DLSS dispatch failed: 0x%x", static_cast<unsigned int>(result));
            cmd_list->AdoptComputeShaderResource(tex_color);
            cmd_list->AdoptComputeShaderResource(tex_velocity);
            cmd_list->AdoptComputeShaderResource(tex_depth);
            if (tex_bias)
            {
                cmd_list->AdoptComputeShaderResource(tex_bias);
            }
            cmd_list->AdoptUnorderedAccess(tex_output);
            cmd_list->restore_after_external_pass();
            return;
        }

        cmd_list->AdoptComputeShaderResource(tex_color);
        cmd_list->AdoptComputeShaderResource(tex_velocity);
        cmd_list->AdoptComputeShaderResource(tex_depth);
        if (tex_bias)
        {
            cmd_list->AdoptComputeShaderResource(tex_bias);
        }
        cmd_list->AdoptUnorderedAccess(tex_output);
        cmd_list->restore_after_external_pass();
    #endif
    }

    bool RHI_VendorTechnology::NRD_Dispatch(
        Nrd_Preset preset,
        RHI_Texture* tex_mv,
        RHI_Texture* tex_normal_roughness,
        RHI_Texture* tex_view_z,
        RHI_Texture* tex_signal_in,
        RHI_Texture* tex_signal_out,
        const math::Vector3* light_direction,
        uint32_t shadow_denoiser_index
    )
    {
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
    #ifdef _WIN32
        if (!common::cb_frame || !tex_mv || !tex_normal_roughness || !tex_view_z || !tex_signal_in || !tex_signal_out)
        {
            return false;
        }

        if (preset == Nrd_Preset::Shadows && !light_direction)
        {
            return false;
        }

        nvidia::nrd_pool& pool          = nvidia::pool_for_preset(preset);
        const RHI_Queue_Type queue_type = cmd_list->GetQueue()->GetType();
        const uint32_t width            = tex_mv->GetWidth();
        const uint32_t height           = tex_mv->GetHeight();
        if (!pool.initialized || pool.width != width || pool.height != height || pool.queue_type != queue_type)
        {
            RHI_Device::QueueWaitAll();
            if (!nvidia::pool_create(pool, width, height, preset, queue_type))
            {
                return false;
            }
            pool.reset_history = true;
        }

        // make_resource declares these to nri as AccessBits::SHADER_RESOURCE, which nri expands to non pixel plus pixel
        // on a direct list and to non pixel only on a compute one, so the inputs must carry whichever set matches
        const bool include_pixel_stage = queue_type == RHI_Queue_Type::Graphics;
        cmd_list->EnsureComputeShaderResource(tex_mv, include_pixel_stage);
        cmd_list->EnsureComputeShaderResource(tex_normal_roughness, include_pixel_stage);
        cmd_list->EnsureComputeShaderResource(tex_view_z, include_pixel_stage);
        cmd_list->EnsureComputeShaderResource(tex_signal_in, include_pixel_stage);
        cmd_list->PrepareForExternalWrite(tex_signal_out);
        cmd_list->FlushBarriers();

        const uint32_t engine_frame = common::cb_frame->frame;
        if (pool.last_frame_index != engine_frame)
        {
            pool.integration.NewFrame();
            pool.last_frame_index = engine_frame;
        }

        const bool already_set   = pool.last_settings_frame == engine_frame;
        const bool reset_history = nrd_common::resolve_history_reset(pool.reset_history, engine_frame, pool.last_settings_frame);
        pool.reset_history = false;

        // setcommonsettings once per pool per frame, nrd integration asserts frameindex advances by 1
        if (!already_set || reset_history)
        {
            nrd::CommonSettings common_settings = {};
            nrd_common::fill_common_settings(
                common_settings,
                common::cb_frame,
                width,
                height,
                reset_history,
                preset
            );
            if (pool.integration.SetCommonSettings(common_settings) != nrd::Result::SUCCESS)
            {
                SP_LOG_WARNING("NRD SetCommonSettings failed");
                return false;
            }
        }

        nrd::Identifier denoiser_id = nrd_common::id_gi;
        nrd::ResourceSnapshot snapshot = {};
        snapshot.restoreInitialState = true;
        snapshot.SetResource(nrd::ResourceType::IN_MV, nvidia::make_resource(tex_mv, false));
        snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, nvidia::make_resource(tex_normal_roughness, false));
        snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, nvidia::make_resource(tex_view_z, false));

        if (preset == Nrd_Preset::Gi)
        {
            denoiser_id = nrd_common::id_gi;
            nrd::ReblurSettings reblur = {};
            nrd_common::fill_preset_gi(reblur, common::cb_frame->delta_time);
            if (pool.integration.SetDenoiserSettings(denoiser_id, &reblur) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_in, false));
            snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_out, true));
        }
        else if (preset == Nrd_Preset::Reflections)
        {
            denoiser_id = nrd_common::id_reflections;
            nrd::ReblurSettings reblur = {};
            nrd_common::fill_preset_reflections(reblur, common::cb_frame->delta_time);
            if (pool.integration.SetDenoiserSettings(denoiser_id, &reblur) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_in, false));
            snapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, nvidia::make_resource(tex_signal_out, true));
        }
        else
        {
            denoiser_id = shadow_denoiser_index == 0
                ? nrd_common::id_shadows
                : nrd_common::id_shadows_local0 + (shadow_denoiser_index - 1);
            const float light_dir[3] = { light_direction->x, light_direction->y, light_direction->z };
            nrd::SigmaSettings sigma = {};
            nrd_common::fill_preset_shadows(
                sigma,
                light_dir,
                common::cb_frame->delta_time,
                shadow_denoiser_index != 0
            );
            if (pool.integration.SetDenoiserSettings(denoiser_id, &sigma) != nrd::Result::SUCCESS)
            {
                return false;
            }
            snapshot.SetResource(nrd::ResourceType::IN_PENUMBRA, nvidia::make_resource(tex_signal_in, false));
            snapshot.SetResource(nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY, nvidia::make_resource(tex_signal_out, true));
        }

        nri::CommandBufferD3D12Desc cmd_desc = {};
        cmd_desc.d3d12CommandList = static_cast<ID3D12GraphicsCommandList*>(cmd_list->GetRhiResource());

        const nrd::Identifier denoisers[] = { denoiser_id };
        pool.integration.DenoiseD3D12(denoisers, 1, cmd_desc, snapshot);

        // restoreInitialState puts them back into the state declared above, so adopt that same set of bits
        cmd_list->AdoptComputeShaderResource(tex_mv, include_pixel_stage);
        cmd_list->AdoptComputeShaderResource(tex_normal_roughness, include_pixel_stage);
        cmd_list->AdoptComputeShaderResource(tex_view_z, include_pixel_stage);
        cmd_list->AdoptComputeShaderResource(tex_signal_in, include_pixel_stage);
        cmd_list->AdoptUnorderedAccess(tex_signal_out);
        cmd_list->restore_after_external_pass();
        return true;
    #else
        return false;
    #endif
    }
}
