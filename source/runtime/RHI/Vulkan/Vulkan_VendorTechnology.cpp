/*
Copyright(c) 2015-2025 Panos Karabelas

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


//= INCLUDES ==================================
#include "pch.h"
#include "../RHI_VendorTechnology.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Shader.h"
#include "../Rendering/Renderer.h"
#include "../World/Components/Camera.h"
#include "../Core/Debugging.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_sssr.h>
#include <FidelityFX/host/ffx_breadcrumbs.h>
#include <xess/xess_vk.h>
#endif
SP_WARNINGS_ON
//=============================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    #ifdef _WIN32
    namespace common
    {
        uint32_t resolution_render_width  = 0;
        uint32_t resolution_render_height = 0;
        uint32_t resolution_output_width  = 0;
        uint32_t resolution_output_height = 0;
        bool reset_history                = false;
        float resolution_scale            = 1.0f; // baked into resolution_render_width and resolution_render_height
    }

    namespace intel
    {
        xess_context_handle_t context           = nullptr;
        xess_vk_init_params_t params_init       = {};
        xess_vk_execute_params_t params_execute = {};
        Vector2 jitter                          = Vector2::Zero;
        const float responsive_mask_value_max   = 0.05f;
        const float exposure_scale              = 0.05f;
        xess_quality_settings_t quality         = XESS_QUALITY_SETTING_BALANCED;

        xess_quality_settings_t get_quality(const float scale_factor)
        {
            struct QualitySetting
            {
                xess_quality_settings_t quality;
                float scale_factor;
            };

            const QualitySetting quality_settings[] =
            {
                { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE,  0.25f }, // ~50% per dimension (0.5 * 0.5)
                { XESS_QUALITY_SETTING_PERFORMANCE,        0.36f }, // ~60% per dimension (0.6 * 0.6)
                { XESS_QUALITY_SETTING_BALANCED,           0.49f }, // ~70% per dimension (0.7 * 0.7)
                { XESS_QUALITY_SETTING_QUALITY,            0.64f }, // ~80% per dimension (0.8 * 0.8)
                { XESS_QUALITY_SETTING_ULTRA_QUALITY,      0.81f }, // ~90% per dimension (0.9 * 0.9)
                { XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, 0.91f }, // ~95% per dimension (0.95 * 0.95)
                { XESS_QUALITY_SETTING_AA,                 1.0f  }  // 100% (no upscaling)
            };

            // find the quality setting with the closest scale factor
            quality = XESS_QUALITY_SETTING_BALANCED; // default fallback
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
            if (!RHI_Device::PropertyIsXessSupported())
                return;

            // create
            context_destroy();
            SP_ASSERT(xessVKCreateContext(RHI_Context::instance, RHI_Context::device_physical, RHI_Context::device, &context) == xess_result_t::XESS_RESULT_SUCCESS);

            // calculate the scaling factor
            uint32_t render_area = common::resolution_render_width * common::resolution_render_height;
            uint32_t output_area = common::resolution_output_width * common::resolution_output_height;
            float scale_factor   = static_cast<float>(render_area) / static_cast<float>(output_area);

            // initialize
            intel::params_init.outputResolution.x = common::resolution_output_width;
            intel::params_init.outputResolution.y = common::resolution_output_height;
            intel::params_init.qualitySetting     = intel::get_quality(scale_factor);
            intel::params_init.initFlags          = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH;
            intel::params_init.creationNodeMask   = 0;
            intel::params_init.visibleNodeMask    = 0;
            intel::params_init.tempBufferHeap     = VK_NULL_HANDLE;
            intel::params_init.bufferHeapOffset   = 0;
            intel::params_init.tempTextureHeap    = VK_NULL_HANDLE;
            intel::params_init.textureHeapOffset  = 0;
            intel::params_init.pipelineCache      = VK_NULL_HANDLE;
            SP_ASSERT(xessVKInit(intel::context, &intel::params_init) == xess_result_t::XESS_RESULT_SUCCESS);

            // configure
            SP_ASSERT(xessSetVelocityScale(intel::context, -1.0f, -1.0f) == xess_result_t::XESS_RESULT_SUCCESS);
            SP_ASSERT(xessSetMaxResponsiveMaskValue(intel::context, intel::responsive_mask_value_max) == xess_result_t::XESS_RESULT_SUCCESS);
        }

        uint32_t get_sample_count()
        {
            uint32_t count = 0;

            switch (quality)
            {
            case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            case XESS_QUALITY_SETTING_ULTRA_QUALITY:
                count = 48;
                break;
            case XESS_QUALITY_SETTING_QUALITY:
                count = 48;
                break;
            case XESS_QUALITY_SETTING_BALANCED:
                count = 64;
                break;
            case XESS_QUALITY_SETTING_PERFORMANCE:
                count = 80;
                break;
            case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
                count = 96;
                break;
            case XESS_QUALITY_SETTING_AA:
                count = 32;
                break;
            }

            return count;
        }

        xess_vk_image_view_info to_xess_image_view(RHI_Texture* texture)
        {
            xess_vk_image_view_info info          = {};
            info.image                            = static_cast<VkImage>(texture->GetRhiResource()); 
            info.imageView                        = static_cast<VkImageView>(texture->GetRhiSrv());
            info.subresourceRange                 = {};
            info.subresourceRange.aspectMask      = texture->IsDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.aspectMask     |= texture->IsStencilFormat() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
            info.subresourceRange.baseMipLevel    = 0;
            info.subresourceRange.levelCount      = 1;
            info.subresourceRange.baseArrayLayer  = 0;
            info.subresourceRange.layerCount      = 1;
            info.format                           = vulkan_format[static_cast<uint32_t>(texture->GetFormat())];
            info.width                            = texture->GetWidth();
            info.height                           = texture->GetHeight();
        
            return info;
        }
    }

    namespace amd
    {
        FfxInterface ffx_interface             = {};
        Matrix view                            = Matrix::Identity;
        Matrix view_previous                   = Matrix::Identity;
        Matrix projection                      = Matrix::Identity;
        Matrix projection_previous             = Matrix::Identity;
        Matrix view_projection                 = Matrix::Identity;
        Matrix view_inverted                   = Matrix::Identity;
        Matrix projection_inverted             = Matrix::Identity;
        Matrix view_projection_previous        = Matrix::Identity;
        Matrix view_projection_inverted        = Matrix::Identity;
        shared_ptr<RHI_Texture> texture_skybox = nullptr;

        void message_callback(FfxMsgType type, const wchar_t* message)
        {
            if (type == FFX_MESSAGE_TYPE_ERROR)
            {
                SP_LOG_ERROR("AMD FidelityFX: %ls", message);
            }
            else if (type == FFX_MESSAGE_TYPE_WARNING)
            {
                SP_LOG_WARNING("AMD FidelityFX: %ls", message);
            }
        }

        FfxSurfaceFormat to_format(const RHI_Format format)
        {
            switch (format)
            {
            case RHI_Format::R32G32B32A32_Float:
                return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
            case RHI_Format::R16G16B16A16_Float:
                return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
            case RHI_Format::R32G32_Float:
                return FFX_SURFACE_FORMAT_R32G32_FLOAT;
            case RHI_Format::R8_Uint:
                return FFX_SURFACE_FORMAT_R8_UINT;
            case RHI_Format::R32_Uint:
                return FFX_SURFACE_FORMAT_R32_UINT;
            case RHI_Format::R8G8B8A8_Unorm:
                return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
            case RHI_Format::R11G11B10_Float:
                return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
            case RHI_Format::R16G16_Float:
                return FFX_SURFACE_FORMAT_R16G16_FLOAT;
            case RHI_Format::R16_Uint:
                return FFX_SURFACE_FORMAT_R16_UINT;
            case RHI_Format::R16_Float:
                return FFX_SURFACE_FORMAT_R16_FLOAT;
            case RHI_Format::R16_Unorm:
                return FFX_SURFACE_FORMAT_R16_UNORM;
            case RHI_Format::R8_Unorm:
                return FFX_SURFACE_FORMAT_R8_UNORM;
            case RHI_Format::R8G8_Unorm:
                return FFX_SURFACE_FORMAT_R8G8_UNORM;
            case RHI_Format::R32_Float:
                return FFX_SURFACE_FORMAT_R32_FLOAT;
            case RHI_Format::D32_Float:
                return FFX_SURFACE_FORMAT_R32_FLOAT; // shouldn't this be FFX_SURFACE_FORMAT_R32_TYPELESS?
            default:
                SP_ASSERT_MSG(false, "Unsupported format");
                return FFX_SURFACE_FORMAT_UNKNOWN;
            }
        }

        RHI_Format to_rhi_format(const FfxSurfaceFormat format)
        {
            switch (format)
            {
            case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
                return RHI_Format::R32G32B32A32_Float;
            case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
                return RHI_Format::R16G16B16A16_Float;
            case FFX_SURFACE_FORMAT_R32G32_FLOAT:
                return RHI_Format::R32G32_Float;
            case FFX_SURFACE_FORMAT_R8_UINT:
                return RHI_Format::R8_Uint;
            case FFX_SURFACE_FORMAT_R32_UINT:
                return RHI_Format::R32_Uint;
            case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
                return RHI_Format::R8G8B8A8_Unorm;
            case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
                return RHI_Format::R11G11B10_Float;
            case FFX_SURFACE_FORMAT_R16G16_FLOAT:
                return RHI_Format::R16G16_Float;
            case FFX_SURFACE_FORMAT_R16_UINT:
                return RHI_Format::R16_Uint;
            case FFX_SURFACE_FORMAT_R16_FLOAT:
                return RHI_Format::R16_Float;
            case FFX_SURFACE_FORMAT_R16_UNORM:
                return RHI_Format::R16_Unorm;
            case FFX_SURFACE_FORMAT_R8_UNORM:
                return RHI_Format::R8_Unorm;
            case FFX_SURFACE_FORMAT_R8G8_UNORM:
                return RHI_Format::R8G8_Unorm;
            case FFX_SURFACE_FORMAT_R32_FLOAT:
                return RHI_Format::R32_Float;
            default:
                SP_ASSERT_MSG(false, "Unsupported FFX format");
                return RHI_Format::Max;
            }
        }

        FfxResourceStates to_resource_state(RHI_Image_Layout layout)
        {
            switch (layout)
            {
            case RHI_Image_Layout::General:
                return FFX_RESOURCE_STATE_COMMON;
            case RHI_Image_Layout::Attachment:
                return FFX_RESOURCE_STATE_RENDER_TARGET;
            case RHI_Image_Layout::Shader_Read:
                return FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
            case RHI_Image_Layout::Transfer_Source:
                return FFX_RESOURCE_STATE_COPY_SRC;
            case RHI_Image_Layout::Transfer_Destination:
                return FFX_RESOURCE_STATE_COPY_DEST;
            case RHI_Image_Layout::Present_Source:
                return FFX_RESOURCE_STATE_PRESENT;
            default:
                SP_ASSERT_MSG(false, "Unsupported layout");
                return FFX_RESOURCE_STATE_COMMON;
            }
        }

        template<typename T>
        FfxResource to_resource(T* resource, const wchar_t* name)
        {
            FfxResourceDescription description = {};
            FfxResourceStates state            = FFX_RESOURCE_STATE_COMMON;
            void* rhi_resource                 = const_cast<T*>(resource)->GetRhiResource();

            if constexpr (is_same_v<remove_const_t<T>, RHI_Texture>)
            {
                state = to_resource_state(resource->GetLayout(0));

                uint32_t usage = FFX_RESOURCE_USAGE_READ_ONLY;
                if (resource->IsDepthFormat())
                    usage |= FFX_RESOURCE_USAGE_DEPTHTARGET;
                if (resource->IsUav())
                    usage |= FFX_RESOURCE_USAGE_UAV;
                if (resource->GetType() == RHI_Texture_Type::Type2DArray || resource->GetType() == RHI_Texture_Type::TypeCube)
                    usage |= FFX_RESOURCE_USAGE_ARRAYVIEW;
                if (resource->IsRtv())
                    usage |= FFX_RESOURCE_USAGE_RENDERTARGET;

                switch (resource->GetType())
                {
                case RHI_Texture_Type::Type2D:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
                    break;
                case RHI_Texture_Type::Type3D:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE3D;
                    break;
                case RHI_Texture_Type::TypeCube:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE_CUBE;
                    break;
                default:
                    SP_ASSERT_MSG(false, "Unsupported texture type");
                    break;
                }

                description.width    = resource->GetWidth();
                description.height   = resource->GetHeight();
                description.depth    = resource->GetDepth();
                description.mipCount = resource->GetMipCount();
                description.format   = to_format(resource->GetFormat());
                description.usage    = static_cast<FfxResourceUsage>(usage);
            }
            else if constexpr (is_same_v<remove_const_t<T>, RHI_Buffer>)
            {
                description.type   = FFX_RESOURCE_TYPE_BUFFER;
                description.usage  = FFX_RESOURCE_USAGE_UAV;
                description.size   = static_cast<uint32_t>(resource->GetObjectSize());
                description.stride = resource->GetStride();
                state              = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            else
            {
                static_assert(is_same_v<T, RHI_Texture> || is_same_v<T, RHI_Buffer>, "Unsupported resource type");
            }

            return ffxGetResourceVK(rhi_resource, description, const_cast<wchar_t*>(name), state);
        }

        FfxResource to_resource(nullptr_t, const wchar_t* name)
        {
            FfxResourceDescription description = {};
            description.type                   = FFX_RESOURCE_TYPE_TEXTURE1D;
            description.width                  = 0;
            description.height                 = 0;
            description.depth                  = 0;
            description.mipCount               = 0;
            description.format                 = FFX_SURFACE_FORMAT_UNKNOWN;
            description.usage                  = static_cast<FfxResourceUsage>(FFX_RESOURCE_USAGE_READ_ONLY);
        
            return ffxGetResourceVK(nullptr, description, const_cast<wchar_t*>(name), FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        }

        FfxCommandList to_cmd_list(RHI_CommandList* cmd_list)
        {
            return ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
        }

        FfxPipeline to_pipeline(RHI_Pipeline* pipeline)
        {
            return ffxGetPipelineVK(static_cast<VkPipeline>(pipeline->GetRhiResource()));
        }

        void set_float3(FfxFloat32x3& dest, const Vector3& source)
        {
            dest[0] = source.x;
            dest[1] = source.y;
            dest[2] = source.z;
        }

        void set_float16(float* ffx_matrix, const Matrix& matrix)
        {
            const float* data = matrix.Data();
            memcpy(ffx_matrix, data, sizeof(Matrix));
        }

        Matrix to_matrix_view(const Matrix& matrix)
        {
            // sssr:          column-major, column-major memory layout, right-handed
            // brixelizer gi: row-major,    column-major memory layout, right-handed
            // engine:        row-major,    column-major memory layout, left-handed

            // note: ffx probably has invalid documentation, as the
            // below conversions work for both sssr and brixelizer gi

            // 1. transpose
            Matrix adjusted = matrix.Transposed();

            // 2. switch handedness
            adjusted.m20 = -adjusted.m20;
            adjusted.m21 = -adjusted.m21;
            adjusted.m22 = -adjusted.m22;
            adjusted.m23 = -adjusted.m23;

            return adjusted;
        };

        Matrix to_matrix_projection(const Matrix& matrix)
        {
            // sssr:          column-major, column-major memory layout, right-handed
            // brixelizer gi: row-major,    column-major memory layout, right-handed
            // engine:        row-major,    column-major memory layout, left-handed

            // note: ffx probably has invalid documentation, as the
            // below conversions work for both sssr and brixelizer gi

            // 1 . transpose
            Matrix adjusted = matrix.Transposed();

            // 2. switch handedness
            adjusted.m22 = 0.0f;
            adjusted.m23 = matrix.m32;
            adjusted.m32 = -1.0f;
            adjusted.m33 = 0.0f;

            return adjusted;
        }

        string convert_wchar_to_char(const wchar_t* wchar_str)
        {
            if (!wchar_str)
                return {};
        
            size_t size_needed = 0;
            errno_t err = wcstombs_s(&size_needed, nullptr, 0, wchar_str, 0);
            if (err != 0 || size_needed == 0)
                return {};
        
            string char_str(size_needed, '\0'); // size_needed includes null terminator
        
            err = wcstombs_s(&size_needed, &char_str[0], size_needed, wchar_str, size_needed - 1);
            if (err != 0)
                return {};
        
            char_str.resize(size_needed - 1); // remove null terminator from string length
        
            return char_str;
        }

        namespace upscaler
        {
            // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/super-resolution-upscaler.md
            // requires:      VK_KHR_get_memory_requirements2

            bool                                       context_created              = false;
            FfxFsr3UpscalerContext                     context                      = {};
            FfxFsr3UpscalerContextDescription          description_context          = {};
            FfxFsr3UpscalerDispatchDescription         description_dispatch         = {};
            FfxFsr3UpscalerGenerateReactiveDescription description_reactive_mask    = {};
            FfxFsr3UpscalerSharedResourceDescriptions  description_shared_resources = {};
            uint32_t                                   jitter_index                 = 0;
            float velocity_factor                                                   = 1.0f; // controls temporal stability of bright pixels [0.0f, 1.0f]

            // resources
            shared_ptr<RHI_Texture> texture_depth_previous_nearest_reconstructed = nullptr;
            shared_ptr<RHI_Texture> texture_depth_dilated                        = nullptr;
            shared_ptr<RHI_Texture> texture_motion_vectors_dilated               = nullptr;

            void context_destroy()
            {
                if (context_created)
                {
                    SP_ASSERT(ffxFsr3UpscalerContextDestroy(&context) == FFX_OK);
                    context_created = false;

                    texture_depth_previous_nearest_reconstructed = nullptr;
                    texture_depth_dilated                        = nullptr;
                    texture_motion_vectors_dilated               = nullptr;
                }
            }

            void context_create()
            {
                context_destroy();

                // description
                description_context.maxRenderSize.width    = common::resolution_render_width;
                description_context.maxRenderSize.height   = common::resolution_render_height;
                description_context.maxUpscaleSize.width   = common::resolution_output_width;
                description_context.maxUpscaleSize.height  = common::resolution_output_height;
                description_context.flags                  = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
                description_context.flags                 |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE; // hdr input
                #ifdef DEBUG
                description_context.flags                 |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
                description_context.fpMessage              = &message_callback;
                #endif
                description_context.backendInterface       = ffx_interface;

                // context
                SP_ASSERT(ffxFsr3UpscalerContextCreate(&context, &description_context) == FFX_OK);
                context_created = true;
                
                // create shared resources (between upscaler and interpolator)
                {
                    ffxFsr3UpscalerGetSharedResourceDescriptions(&context, &description_shared_resources);
                
                    FfxCreateResourceDescription resource = description_shared_resources.reconstructedPrevNearestDepth;
                    texture_depth_previous_nearest_reconstructed = make_shared<RHI_Texture>(
                         RHI_Texture_Type::Type2D,
                         resource.resourceDescription.width,
                         resource.resourceDescription.height,
                         resource.resourceDescription.depth,
                         resource.resourceDescription.mipCount,
                         to_rhi_format(resource.resourceDescription.format),
                         RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_ClearBlit,
                         convert_wchar_to_char(resource.name).c_str()
                    );
                
                    resource = description_shared_resources.dilatedDepth;
                    texture_depth_dilated = make_shared<RHI_Texture>(
                        RHI_Texture_Type::Type2D,
                        resource.resourceDescription.width,
                        resource.resourceDescription.height,
                        resource.resourceDescription.depth,
                        resource.resourceDescription.mipCount,
                        to_rhi_format(resource.resourceDescription.format),
                        RHI_Texture_Srv | RHI_Texture_Uav,
                        convert_wchar_to_char(resource.name).c_str()
                    );
                
                    resource = description_shared_resources.dilatedMotionVectors;
                    texture_motion_vectors_dilated = make_shared<RHI_Texture>(
                         RHI_Texture_Type::Type2D,
                         resource.resourceDescription.width,
                         resource.resourceDescription.height,
                         resource.resourceDescription.depth,
                         resource.resourceDescription.mipCount, 
                         to_rhi_format(resource.resourceDescription.format),
                         RHI_Texture_Srv | RHI_Texture_Uav,
                         convert_wchar_to_char(resource.name).c_str()
                    );
                }
                
                // set velocity factor [0, 1], this controls the temporal stability of bright pixels
                ffxFsr3UpscalerSetConstant(&context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, static_cast<void*>(&velocity_factor));
                
                // reset jitter index
                jitter_index = 0;
            }
        }

        namespace ssr
        {
            bool                       context_created      = false;
            FfxSssrContext             context              = {};
            FfxSssrContextDescription  description_context  = {};
            FfxSssrDispatchDescription description_dispatch = {};

            void context_destroy()
            {
                if (context_created)
                {
                    RHI_Device::QueueWaitAll();

                    SP_ASSERT(ffxSssrContextDestroy(&context) == FFX_OK);
                    context_created = false;
                }
            }

            void context_create()
            {
                context_destroy();

                description_context.renderSize.width           = common::resolution_render_width;
                description_context.renderSize.height          = common::resolution_render_height;
                description_context.normalsHistoryBufferFormat = to_format(RHI_Format::R16G16B16A16_Float);
                description_context.flags                      = FFX_SSSR_ENABLE_DEPTH_INVERTED;
                description_context.backendInterface           = ffx_interface;
                
                SP_ASSERT(ffxSssrContextCreate(&context, &description_context) == FFX_OK);
                context_created = true;
            }
        }

        namespace breadcrumbs
        {
            bool                  context_created = false;
            FfxBreadcrumbsContext context         = {};
            array<uint32_t, 3> gpu_queue_indices  = {};
            unordered_map<uint64_t, bool> registered_cmd_lists;

            void context_destroy()
            {
                if (context_created)
                {
                    RHI_Device::QueueWaitAll();

                    SP_ASSERT(ffxBreadcrumbsContextDestroy(&context) == FFX_OK);
                    context_created = false;
                }
            }

            void context_create()
            {
                context_destroy();

                if (!context_created && Debugging::IsBreadcrumbsEnabled())
                {
                     gpu_queue_indices[0] = RHI_Device::GetQueueIndex(RHI_Queue_Type::Graphics);
                     gpu_queue_indices[1] = RHI_Device::GetQueueIndex(RHI_Queue_Type::Compute);
                     gpu_queue_indices[2] = RHI_Device::GetQueueIndex(RHI_Queue_Type::Copy);

                     FfxBreadcrumbsContextDescription context_description = {};
                     context_description.backendInterface                 = ffx_interface;
                     context_description.maxMarkersPerMemoryBlock         = 100;
                     context_description.usedGpuQueuesCount               = static_cast<uint32_t>(gpu_queue_indices.size());
                     context_description.pUsedGpuQueues                   = gpu_queue_indices.data();
                     context_description.allocCallbacks.fpAlloc           = malloc;
                     context_description.allocCallbacks.fpRealloc         = realloc;
                     context_description.allocCallbacks.fpFree            = free;
                     context_description.frameHistoryLength               = 2;
                     context_description.flags                            = FFX_BREADCRUMBS_PRINT_FINISHED_LISTS    |
                                                                            FFX_BREADCRUMBS_PRINT_NOT_STARTED_LISTS |
                                                                            FFX_BREADCRUMBS_PRINT_FINISHED_NODES    |
                                                                            FFX_BREADCRUMBS_PRINT_NOT_STARTED_NODES |
                                                                            FFX_BREADCRUMBS_PRINT_EXTENDED_DEVICE_INFO |
                                                                            FFX_BREADCRUMBS_ENABLE_THREAD_SYNCHRONIZATION;

                     SP_ASSERT(ffxBreadcrumbsContextCreate(&context, &context_description) == FFX_OK);
                     context_created = true;
                }
            }
        }
    }
    #endif // _WIN32

    void RHI_VendorTechnology::Initialize()
    {
    #ifdef _WIN32
        // register amd
        {
            string ffx_version = to_string(FFX_SDK_VERSION_MAJOR) + "." +
                                 to_string(FFX_SDK_VERSION_MINOR) + "." +
                                 to_string(FFX_SDK_VERSION_PATCH);
            
            Settings::RegisterThirdPartyLib("AMD FidelityFX", ffx_version, "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK");
        }

        // register intel
        {
            xess_version_t version;
            SP_ASSERT(xessGetVersion(&version) == XESS_RESULT_SUCCESS);
            string xess_version = to_string(version.major) + "." + to_string(version.minor) + "." + to_string(version.patch);
            Settings::RegisterThirdPartyLib("Intel XeSS", xess_version, "https://github.com/intel/xess");
        }

        // ffx interface
        {
            // all used contexts need to be accounted for here
            const size_t max_contexts =
                FFX_FSR3_CONTEXT_COUNT +
                FFX_SSSR_CONTEXT_COUNT +
                FFX_BREADCRUMBS_CONTEXT_COUNT;
            
            VkDeviceContext device_context  = {};
            device_context.vkDevice         = RHI_Context::device;
            device_context.vkPhysicalDevice = RHI_Context::device_physical;
            device_context.vkDeviceProcAddr = vkGetDeviceProcAddr;
            
            const size_t scratch_buffer_size = ffxGetScratchMemorySizeVK(RHI_Context::device_physical, max_contexts);
            void* scratch_buffer             = calloc(1, scratch_buffer_size);
            
            SP_ASSERT(ffxGetInterfaceVK(&amd::ffx_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, max_contexts)== FFX_OK);
        }

        // breadcrumbs
        {
            amd::breadcrumbs::context_create();
        }

        // assets
        {
            // empty skybox
            {
                const uint32_t width     = 128;
                const uint32_t height    = 128;
                const uint32_t depth     = 6; // cube faces
                const uint32_t mip_count = 1;
                const RHI_Format format  = RHI_Format::R16G16B16A16_Float;
                const uint32_t flags     = RHI_Texture_Srv | RHI_Texture_Uav;
                const char* name         = "skybox";

                const uint32_t channel_count    = 4;
                const uint32_t bits_per_channel = 16;
                const uint32_t bytes_per_pixel  = (bits_per_channel / 8) * channel_count;
                const size_t slice_size         = static_cast<size_t>(width) * height * bytes_per_pixel;

                // create black data for all 6 faces
                std::vector<RHI_Texture_Slice> data(depth);
                for (uint32_t face = 0; face < depth; face++)
                {
                    RHI_Texture_Mip mip;
                    mip.bytes.resize(slice_size, std::byte(0));
                    data[face].mips.push_back(std::move(mip));
                }

                amd::texture_skybox = make_shared<RHI_Texture>(
                    RHI_Texture_Type::TypeCube,
                    width,
                    height,
                    depth,
                    mip_count,
                    format,
                    flags,
                    name,
                    std::move(data)
                );
            }
        }
    #endif
    }

    void RHI_VendorTechnology::Shutdown()
    {
    #ifdef _WIN32
        amd::upscaler::context_destroy();
        amd::ssr::context_destroy();
        amd::breadcrumbs::context_destroy();

        // ffx interface
        if (amd::ffx_interface.scratchBuffer != nullptr)
        {
            free(amd::ffx_interface.scratchBuffer);
        }

        // shared
        amd::texture_skybox = nullptr;
    #endif
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame, const Vector2& resolution_render, const Vector2& resolution_output, const float resolution_scale)
    {
    #ifdef _WIN32
        // matrices - ffx is right-handed
        {
            amd::view_previous            = amd::view;
            amd::projection_previous      = amd::projection;
            amd::view_projection_previous = amd::view_projection;

            amd::view                     = amd::to_matrix_view(cb_frame->view);
            amd::projection               = amd::to_matrix_projection(cb_frame->projection);
            amd::view_projection          = amd::projection * amd::view;

            amd::view_inverted            = Matrix::Invert(amd::view);
            amd::projection_inverted      = Matrix::Invert(amd::projection);
            amd::view_projection_inverted = Matrix::Invert(amd::view_projection);
        }

        // resize
        {
            common::resolution_scale = resolution_scale;

            // calculate actual render resolution from target render res and scale
            uint32_t render_width =  static_cast<uint32_t>(resolution_render.x * resolution_scale);
            uint32_t render_height = static_cast<uint32_t>(resolution_render.y * resolution_scale);

            // check for changes
            bool resolution_render_changed = render_width != common::resolution_render_width || render_height != common::resolution_render_height;
            bool resolution_output_changed = static_cast<uint32_t>(resolution_output.x) != common::resolution_output_width ||
                static_cast<uint32_t>(resolution_output.y) != common::resolution_output_height;

            // update common resolutions
            common::resolution_render_width  = render_width;
            common::resolution_render_height = render_height;
            common::resolution_output_width  = static_cast<uint32_t>(resolution_output.x);
            common::resolution_output_height = static_cast<uint32_t>(resolution_output.y);

            // re-create resolution dependent contexts
            {
                if (resolution_render_changed)
                {
                    amd::ssr::context_create();
                }

                // todo: make these mutually exlusive
                if ((resolution_render_changed || resolution_output_changed))
                {
                    amd::upscaler::context_create();
                    intel::context_create();
                }
            }
        }

        // breadcrumbs
        if (amd::breadcrumbs::context_created)
        {
            amd::breadcrumbs::registered_cmd_lists.clear();
            SP_ASSERT(ffxBreadcrumbsStartFrame(&amd::breadcrumbs::context) == FFX_OK);
        }
    #endif
    }

    void RHI_VendorTechnology::ResetHistory()
    {
        common::reset_history = true;
    }

    void RHI_VendorTechnology::XeSS_GenerateJitterSample(float* x, float* y)
    {
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
    
        // static storage for halton points and index
        static vector<pair<float, float>> halton_points;
        static size_t halton_index = 0;
    
        // generate halton points (bases 2 and 3, start index 1) if not already done
        if (halton_points.empty())
        {
            const uint32_t xess_sample_limit = 96;

            uint32_t base_x      = 2;
            uint32_t base_y      = 3;
            uint32_t start_index = 1;
            float offset_x       = 0.0f;
            float offset_y       = 0.0f;
            halton_points.reserve(xess_sample_limit);
            for (uint32_t i = start_index; i < start_index + xess_sample_limit; ++i)
            {
                // generate x and y in [0, 1], shift to [-0.5, 0.5] for pixel space
                float jitter_x = get_corput(i, base_x) - 0.5f;
                float jitter_y = get_corput(i, base_y) - 0.5f;
                halton_points.emplace_back(jitter_x, jitter_y);
            }
        }
    
        // get the current jitter sample (pixel space, [-0.5, 0.5])
        auto jitter  = halton_points[halton_index];

        // this is for xessVKExecute which expects [-0.5, 0.5] jitter
        intel::jitter.x = jitter.first;
        intel::jitter.y = jitter.second;

        // write scaled jitter for projection matrix
        *x =  2.0f * jitter.first  / static_cast<float>(common::resolution_render_width);
        *y = -2.0f * jitter.second / static_cast<float>(common::resolution_render_height);

        // advance to the next sample, cycling back to 0
        uint32_t sample_count_at_current_quality_level = intel::get_sample_count();
        halton_index = (halton_index + 1) % sample_count_at_current_quality_level;
    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
    #ifdef _WIN32
        tex_color->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_velocity->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        tex_output->SetLayout(RHI_Image_Layout::General, cmd_list);
        cmd_list->InsertPendingBarrierGroup();

        intel::params_execute.colorTexture               = intel::to_xess_image_view(tex_color);
        intel::params_execute.depthTexture               = intel::to_xess_image_view(tex_depth);
        intel::params_execute.velocityTexture            = intel::to_xess_image_view(tex_velocity);
        intel::params_execute.outputTexture              = intel::to_xess_image_view(tex_output);
        intel::params_execute.exposureScaleTexture       = intel::to_xess_image_view(Renderer::GetStandardTexture(Renderer_StandardTexture::Black)); // neutralize and control via float
        intel::params_execute.responsivePixelMaskTexture = intel::to_xess_image_view(Renderer::GetStandardTexture(Renderer_StandardTexture::Black)); // neutralize and control via float
        intel::params_execute.jitterOffsetX              = intel::jitter.x;
        intel::params_execute.jitterOffsetY              = intel::jitter.y;
        intel::params_execute.exposureScale              = intel::exposure_scale;
        intel::params_execute.inputWidth                 = common::resolution_render_width;
        intel::params_execute.inputHeight                = common::resolution_render_height;
        intel::params_execute.inputColorBase             = { 0, 0 };
        intel::params_execute.inputMotionVectorBase      = { 0, 0 };
        intel::params_execute.inputDepthBase             = { 0, 0 };
        intel::params_execute.inputResponsiveMaskBase    = { 0, 0 };
        intel::params_execute.outputColorBase            = { 0, 0 };
        intel::params_execute.reserved0                  = { 0, 0 };

        intel::params_execute.resetHistory = common::reset_history;
        common::reset_history              = false;

        _xess_result_t result = xessVKExecute(intel::context, static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()), &intel::params_execute);
        SP_ASSERT(result == XESS_RESULT_SUCCESS);
    #endif
    }

    void RHI_VendorTechnology::FSR3_GenerateJitterSample(float* x, float* y)
    {
    #ifdef _WIN32
        // get jitter phase count
        const uint32_t resolution_render_x = static_cast<uint32_t>(amd::upscaler::description_context.maxRenderSize.width);
        const uint32_t resolution_render_y = static_cast<uint32_t>(amd::upscaler::description_context.maxRenderSize.height);
        const int32_t jitter_phase_count   = ffxFsr3GetJitterPhaseCount(resolution_render_x, resolution_render_x);

        // ensure fsr3_jitter_index is properly wrapped around the jitter_phase_count
        amd::upscaler::jitter_index = (amd::upscaler::jitter_index + 1) % jitter_phase_count;

        // generate jitter sample
        FfxErrorCode result = ffxFsr3GetJitterOffset(&amd::upscaler::description_dispatch.jitterOffset.x, &amd::upscaler::description_dispatch.jitterOffset.y, amd::upscaler::jitter_index, jitter_phase_count);
        SP_ASSERT(result == FFX_OK);

        *x =  2.0f * amd::upscaler::description_dispatch.jitterOffset.x / resolution_render_x;
        *y = -2.0f * amd::upscaler::description_dispatch.jitterOffset.y / resolution_render_y;
    #endif
    }

    void RHI_VendorTechnology::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
    #ifdef _WIN32
        // set resources (no need for the transparency or reactive masks as we do them later, full res)
        amd::upscaler::description_dispatch.commandList                   = amd::to_cmd_list(cmd_list);
        amd::upscaler::description_dispatch.color                         = amd::to_resource(tex_color,                                                         L"fsr3_color");
        amd::upscaler::description_dispatch.depth                         = amd::to_resource(tex_depth,                                                         L"fsr3_depth");
        amd::upscaler::description_dispatch.motionVectors                 = amd::to_resource(tex_velocity,                                                      L"fsr3_velocity");
        amd::upscaler::description_dispatch.exposure                      = amd::to_resource(nullptr,                                                           L"fsr3_exposure");
        amd::upscaler::description_dispatch.reactive                      = amd::to_resource(nullptr,                                                           L"fsr3_reactive");
        amd::upscaler::description_dispatch.transparencyAndComposition    = amd::to_resource(nullptr,                                                           L"fsr3_transaprency_and_composition");
        amd::upscaler::description_dispatch.dilatedDepth                  = amd::to_resource(amd::upscaler::texture_depth_dilated.get(),                        L"fsr3_depth_dilated");
        amd::upscaler::description_dispatch.dilatedMotionVectors          = amd::to_resource(amd::upscaler::texture_motion_vectors_dilated.get(),               L"fsr3_motion_vectors_dilated");
        amd::upscaler::description_dispatch.reconstructedPrevNearestDepth = amd::to_resource(amd::upscaler::texture_depth_previous_nearest_reconstructed.get(), L"fsr3_depth_nearest_previous_reconstructed");
        amd::upscaler::description_dispatch.output                        = amd::to_resource(tex_output,                                                        L"fsr3_output");
        
        // configure
        amd::upscaler::description_dispatch.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth())  * 0.5f;
        amd::upscaler::description_dispatch.motionVectorScale.y    =  static_cast<float>(tex_velocity->GetHeight()) * 0.5f;
        amd::upscaler::description_dispatch.enableSharpening       = sharpness != 0.0f;        // sdk issue: redundant parameter
        amd::upscaler::description_dispatch.sharpness              = sharpness;
        amd::upscaler::description_dispatch.frameTimeDelta         = delta_time_sec * 1000.0f; // seconds to milliseconds
        amd::upscaler::description_dispatch.preExposure            = 1.0f;                     // the exposure value if not using FFX_FSR3_ENABLE_AUTO_EXPOSURE
        amd::upscaler::description_dispatch.renderSize.width       = common::resolution_render_width;
        amd::upscaler::description_dispatch.renderSize.height      = common::resolution_render_height;
        amd::upscaler::description_dispatch.cameraNear             = camera->GetFarPlane();    // far as near because we are using reverse-z
        amd::upscaler::description_dispatch.cameraFar              = camera->GetNearPlane();   // near as far because we are using reverse-z
        amd::upscaler::description_dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();
        
        // reset history
        amd::upscaler::description_dispatch.reset = common::reset_history;
        common::reset_history = false;
        
        // dispatch
        SP_ASSERT(ffxFsr3UpscalerContextDispatch(&amd::upscaler::context, &amd::upscaler::description_dispatch) == FFX_OK);
        amd::upscaler::description_dispatch.reset = false;
    #endif
    }

    void RHI_VendorTechnology::SSSR_Dispatch(
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_reflection_source,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        RHI_Texture* tex_brdf,
        RHI_Texture* tex_output
    )
    {
    #ifdef _WIN32
        // comply with sssr expectations
        SP_ASSERT(tex_reflection_source->GetBitsPerChannel() > 8);  // hdr color, expect float16+
        SP_ASSERT(tex_depth->GetFormat() == RHI_Format::D32_Float); // single float depth
        SP_ASSERT(tex_velocity->GetBitsPerChannel() >= 16);         // 2x float
        SP_ASSERT(tex_normal->GetBitsPerChannel() >= 16);           // 3x float
        SP_ASSERT(tex_material->GetBitsPerChannel() >= 8);          // 1x float roughness
        SP_ASSERT(tex_brdf->GetBitsPerChannel() >= 16);             // 2x float
        SP_ASSERT(tex_output->GetBitsPerChannel() >= 16);           // 3x float output
        cmd_list->ClearTexture(tex_output, Color::standard_black);

        // set resources
        amd::ssr::description_dispatch.commandList        = amd::to_cmd_list(cmd_list);
        amd::ssr::description_dispatch.color              = amd::to_resource(tex_reflection_source,     L"sssr_reflection_source");
        amd::ssr::description_dispatch.depth              = amd::to_resource(tex_depth,                 L"sssr_depth");
        amd::ssr::description_dispatch.motionVectors      = amd::to_resource(tex_velocity,              L"sssr_velocity");
        amd::ssr::description_dispatch.normal             = amd::to_resource(tex_normal,                L"sssr_normal");
        amd::ssr::description_dispatch.materialParameters = amd::to_resource(tex_material,              L"sssr_roughness");
        amd::ssr::description_dispatch.environmentMap     = amd::to_resource(amd::texture_skybox.get(), L"sssr_environment");
        amd::ssr::description_dispatch.brdfTexture        = amd::to_resource(tex_brdf,                  L"sssr_brdf");
        amd::ssr::description_dispatch.output             = amd::to_resource(tex_output,                L"sssr_output");
 
        // set render size
        amd::ssr::description_dispatch.renderSize.width  = common::resolution_render_width;
        amd::ssr::description_dispatch.renderSize.height = common::resolution_render_height;

        // set sssr specific parameters
        amd::ssr::description_dispatch.motionVectorScale.x                  = 0.5f;    // maps [-1,1] NDC delta to [-0.5, 0.5]
        amd::ssr::description_dispatch.motionVectorScale.y                  = -0.5f;   // same as above, but also flips Y
        amd::ssr::description_dispatch.normalUnPackMul                      = 1.0f;
        amd::ssr::description_dispatch.normalUnPackAdd                      = 0.0f;
        amd::ssr::description_dispatch.depthBufferThickness                 = 2.5f;    // hit acceptance bias, larger values can cause streaks, lower values can cause holes
        amd::ssr::description_dispatch.varianceThreshold                    = 0.0001f; // luminance differences between history results will trigger an additional ray if they are greater than this threshold value
        amd::ssr::description_dispatch.maxTraversalIntersections            = 100;     // caps the maximum number of lookups that are performed from the depth buffer hierarchy, most rays should end after about 20 lookups
        amd::ssr::description_dispatch.minTraversalOccupancy                = 1;       // exit the core loop early if less than this number of threads are running
        amd::ssr::description_dispatch.mostDetailedMip                      = 0;
        amd::ssr::description_dispatch.temporalStabilityFactor              = 1.0f;    // the accumulation of history values, higher values reduce noise, but are more likely to exhibit ghosting artifacts
        amd::ssr::description_dispatch.temporalVarianceGuidedTracingEnabled = true;    // whether a ray should be spawned on pixels where a temporal variance is detected or not
        amd::ssr::description_dispatch.samplesPerQuad                       = 4;       // the minimum number of rays per quad, variance guided tracing can increase this up to a maximum of 4
        amd::ssr::description_dispatch.iblFactor                            = 0.0f;
        amd::ssr::description_dispatch.roughnessChannel                     = 0;
        amd::ssr::description_dispatch.isRoughnessPerceptual                = true;
        amd::ssr::description_dispatch.roughnessThreshold                   = 0.5f;    // regions with a roughness value greater than this threshold won't spawn rays

        // set camera matrices
        amd::set_float16(amd::ssr::description_dispatch.view,               amd::view);
        amd::set_float16(amd::ssr::description_dispatch.invView,            amd::view_inverted);
        amd::set_float16(amd::ssr::description_dispatch.projection,         amd::projection);
        amd::set_float16(amd::ssr::description_dispatch.invProjection,      amd::projection_inverted);
        amd::set_float16(amd::ssr::description_dispatch.invViewProjection,  amd::view_projection_inverted);
        amd::set_float16(amd::ssr::description_dispatch.prevViewProjection, amd::view_projection_previous);

        // dispatch
        FfxErrorCode error_code = ffxSssrContextDispatch(&amd::ssr::context, &amd::ssr::description_dispatch);
        SP_ASSERT(error_code == FFX_OK);
    #endif
    }
   
    void RHI_VendorTechnology::Breadcrumbs_RegisterCommandList(RHI_CommandList* cmd_list, const RHI_Queue* queue, const char* name)
    {
        #ifdef _MSC_VER

        SP_ASSERT(amd::breadcrumbs::context_created);
        SP_ASSERT(name != nullptr);

        // note #1: command lists need to register per frame
        // note #2: the map check is here in case because the same command lists can be re-used before frames start to be produced (e.g. during initialization)
        if (amd::breadcrumbs::registered_cmd_lists.find(cmd_list->GetObjectId()) != amd::breadcrumbs::registered_cmd_lists.end())
            return;

        FfxBreadcrumbsCommandListDescription description = {};
        description.commandList                          = amd::to_cmd_list(cmd_list);
        description.queueType                            = RHI_Device::GetQueueIndex(queue->GetType());
        description.name                                 = { name, true };
        description.pipeline                             = nullptr;
        description.submissionIndex                      = 0;
    
        SP_ASSERT(ffxBreadcrumbsRegisterCommandList(&amd::breadcrumbs::context, &description) == FFX_OK);
        amd::breadcrumbs::registered_cmd_lists[cmd_list->GetObjectId()] = true;

        #endif
    }

    void RHI_VendorTechnology::Breadcrumbs_RegisterPipeline(RHI_Pipeline* pipeline)
    {
        #ifdef _MSC_VER
        // note: pipelines need to register only once
        SP_ASSERT(amd::breadcrumbs::context_created);

        FfxBreadcrumbsPipelineStateDescription description = {};
        description.pipeline                               = amd::to_pipeline(pipeline);

        RHI_PipelineState* pso = pipeline->GetState();
        description.name       = { pso->name, true};

        if (pso->shaders[RHI_Shader_Type::Vertex])
        { 
            description.vertexShader = { pso->shaders[RHI_Shader_Type::Vertex]->GetObjectName().c_str(), true};
        }

        if (pso->shaders[RHI_Shader_Type::Pixel])
        { 
            description.pixelShader = { pso->shaders[RHI_Shader_Type::Pixel]->GetObjectName().c_str(), true};
        }

        if (pso->shaders[RHI_Shader_Type::Compute])
        { 
            description.computeShader = { pso->shaders[RHI_Shader_Type::Compute]->GetObjectName().c_str(), true};
        }

        if (pso->shaders[RHI_Shader_Type::Hull])
        { 
            description.hullShader = { pso->shaders[RHI_Shader_Type::Hull]->GetObjectName().c_str(), true};
        }

        if (pso->shaders[RHI_Shader_Type::Domain])
        { 
            description.domainShader = { pso->shaders[RHI_Shader_Type::Domain]->GetObjectName().c_str(), true};
        }

        SP_ASSERT(ffxBreadcrumbsRegisterPipeline(&amd::breadcrumbs::context, &description) == FFX_OK);

        #endif
    }

    void RHI_VendorTechnology::Breadcrumbs_SetPipelineState(RHI_CommandList* cmd_list, RHI_Pipeline* pipeline)
    {
        #ifdef _MSC_VER
        SP_ASSERT(amd::breadcrumbs::context_created);

        SP_ASSERT(ffxBreadcrumbsSetPipeline(&amd::breadcrumbs::context, amd::to_cmd_list(cmd_list), amd::to_pipeline(pipeline)) == FFX_OK);

        #endif
    }

    void RHI_VendorTechnology::Breadcrumbs_MarkerBegin(RHI_CommandList* cmd_list, const AMD_FFX_Marker marker, const char* name)
    {
        #ifdef _MSC_VER

        SP_ASSERT(amd::breadcrumbs::context_created);
        SP_ASSERT(name != nullptr);

         FfxBreadcrumbsMarkerType marker_type = FFX_BREADCRUMBS_MARKER_PASS;
         if (marker == AMD_FFX_Marker::Dispatch)
         {
             marker_type = FFX_BREADCRUMBS_MARKER_DISPATCH;
         }
         else if (marker == AMD_FFX_Marker::DrawIndexed)
         {
             marker_type = FFX_BREADCRUMBS_MARKER_DRAW_INDEXED;
         }

        const FfxBreadcrumbsNameTag name_tag = { name, true };
        SP_ASSERT(ffxBreadcrumbsBeginMarker(&amd::breadcrumbs::context, amd::to_cmd_list(cmd_list), marker_type, &name_tag) == FFX_OK);

        #endif
    }

    void RHI_VendorTechnology::Breadcrumbs_MarkerEnd(RHI_CommandList* cmd_list)
    {
        #ifdef _MSC_VER

        SP_ASSERT(amd::breadcrumbs::context_created);

        SP_ASSERT(ffxBreadcrumbsEndMarker(&amd::breadcrumbs::context, amd::to_cmd_list(cmd_list)) == FFX_OK);

        #endif
    }

    void RHI_VendorTechnology::Breadcrumbs_OnDeviceRemoved()
    {
        #ifdef _MSC_VER

        SP_ASSERT(amd::breadcrumbs::context_created);

        FfxBreadcrumbsMarkersStatus marker_status = {};
        SP_ASSERT(ffxBreadcrumbsPrintStatus(&amd::breadcrumbs::context, &marker_status) == FFX_OK);

        ofstream fout("gpu_crash.txt", ios::binary);
        SP_ASSERT_MSG(fout.good(), "Failed to create gpu_crash.txt");

        if (fout.good())
        {
            fout.write(marker_status.pBuffer, marker_status.bufferSize);
            fout.close();
        }

        FFX_SAFE_FREE(marker_status.pBuffer, free);

        SP_INFO_WINDOW("A gpu crash report has been saved to 'gpu_crash.txt'");

        #endif
    }
}
