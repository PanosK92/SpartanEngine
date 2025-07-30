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
#include "../RHI_CommandList.h"
#include "../RHI_Texture.h"
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Shader.h"
#include "../Rendering/Renderer_Buffers.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Camera.h"
#include "../World/Entity.h"
#include "../Core/Debugging.h"
#include "../Input/Input.h"
SP_WARNINGS_OFF
#ifdef _WIN32
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_sssr.h>
#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>
#include <FidelityFX/host/ffx_breadcrumbs.h>
#include <xess/xess.h>
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
            uint32_t count = 32;
            switch (quality)
            {
                case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: // 1.3x scaling
                case XESS_QUALITY_SETTING_ULTRA_QUALITY:      // 1.3x
                    count = 32;                               // 8 * (1/1.3)^2 ≈ 8 * 0.5917 ≈ 4.73, use 32 for stability
                    break;
                case XESS_QUALITY_SETTING_QUALITY:            // 1.5x
                    count = 32;                               // 8 * (1/1.5)^2 ≈ 8 * 0.4444 ≈ 3.56, use 32
                    break;
                case XESS_QUALITY_SETTING_BALANCED:           // 1.7x
                    count = 48;                               // 8 * (1/1.7)^2 ≈ 8 * 0.3460 ≈ 2.77, use 48
                    break;
                case XESS_QUALITY_SETTING_PERFORMANCE:        // 2.0x
                    count = 64;                               // 8 * (1/2.0)^2 ≈ 8 * 0.25 = 2, use 64 (guide suggests up to 72)
                    break;
                case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:  // 3.0x
                    count = 72;                               // 8 * (1/3.0)^2 ≈ 8 * 0.1111 ≈ 0.89, use 72 for max stability
                    break;
                case XESS_QUALITY_SETTING_AA:                 // 1.0x
                    count = 16;                               // No upscaling, minimal samples needed
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

        namespace uscaler
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

        namespace gi
        {
            // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/brixelizer.md
            // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/brixelizer-gi.md

            // sdk issue #1: the sdk should keep track of static/dynamic instances and decide what needs to be deleted or created, not the user.
            // sdk issue #2: all the buffers which are needed, should be created and bound internally by the sdk, not the user.
            // sdk issue #3: instance ids are really indices, using actual ids (a big number) will cause an out of bounds crash.
            // sdk issue #4: the previous depth and normal textures, should be created internally using a blit operation, not by the user.
            // sdk issue #5: after a number of instances (a lot) debug drawing the AABB starts to flicker, and the AABBs are not always correct.

            // parameters
            FfxBrixelizerGIInternalResolution internal_resolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT;
            const float    voxel_size               = 0.2f;
            const float    cascade_size_ratio       = 2.0f;
            const uint32_t cascade_count            = 8;               // max is 24
            const uint32_t cascade_offset           = 16;              // 0 - 8 is for static, 8 - 16 is for dynamic, 16 - 24 is for static + dynamic (merged)
            const uint32_t cascade_index_start      = cascade_offset + 0;
            const uint32_t cascade_index_end        = cascade_offset + cascade_count - 1;
            const bool     sdf_center_around_camera = true;
            const float    sdf_ray_normal_offset    = 0.5f;            // distance from a surface along the normal vector to offset the ray origin - below 0.5 I see artifacts
            const float    sdf_ray_epsilon          = 0.5f;            // epsilon value for ray marching to be used with brixelizer for rays
            const uint32_t bricks_per_update_max    = 1 << 14;         // maximum number of bricks to be updated
            const uint32_t triangle_references_max  = 32 * (1 << 20);  // maximum number of triangle voxel references to be stored in the update
            const uint32_t triangle_swap_size       = 300 * (1 << 20); // size of the swap space available to be used for storing triangles in the update
            const float    t_min                    = 0.0f;
            const float    t_max                    = 10000.0f;

            // structs
            bool                                       context_created          = false;
            FfxBrixelizerContext                       context                  = {};
            FfxBrixelizerContextDescription            description_context      = {};
            FfxBrixelizerUpdateDescription             description_update       = {};
            FfxBrixelizerBakedUpdateDescription        description_update_baked = {};
            FfxBrixelizerGIContext                     context_gi               = {};
            FfxBrixelizerGIContextDescription          description_context_gi   = {};
            FfxBrixelizerGIDispatchDescription         description_dispatch_gi  = {};
            FfxBrixelizerDebugVisualizationDescription debug_description        = {};
            FfxBrixelizerGIDebugDescription            debug_description_gi     = {};

            // resources
            shared_ptr<RHI_Texture>                                    texture_sdf_atlas        = nullptr;
            shared_ptr<RHI_Texture>                                    texture_depth_previous   = nullptr;
            shared_ptr<RHI_Texture>                                    texture_normal_previous  = nullptr;
            shared_ptr<RHI_Buffer>                                     buffer_scratch           = nullptr;
            shared_ptr<RHI_Buffer>                                     buffer_brick_aabbs       = nullptr;
            array<shared_ptr<RHI_Buffer>, FFX_BRIXELIZER_MAX_CASCADES> buffer_cascade_aabb_tree = {};
            array<shared_ptr<RHI_Buffer>, FFX_BRIXELIZER_MAX_CASCADES> buffer_cascade_brick_map = {};

            // instances
            unordered_set<uint64_t> static_instances;
            vector<pair<const RHI_Buffer*, uint32_t>> instance_buffers;
            unordered_map<uint64_t, shared_ptr<Entity>> entity_map;
            vector<FfxBrixelizerInstanceDescription> instances_to_create;
            vector<uint32_t> instances_to_delete;

            // debug visualisation
            enum class DebugMode
            {
                Distance,   // brixelizer
                UVW,        // brixelizer
                Iterations, // brixelizer
                Gradient,   // brixelizer
                BrickID,    // brixelizer
                CascadeID,  // brixelizer
                Radiance,   // brixelizer gi
                Irradiance, // brixelizer gi
                Max
            };
            DebugMode debug_mode            = DebugMode::Max; // overwrites light_diffuse_gi render target
            bool debug_mode_arrow_switch    = false;
            bool debug_mode_aabbs_and_stats = false;
            bool debug_mode_log_instances   = false;
            FfxBrixelizerStats debug_stats  = {};
           
            uint32_t& get_or_create_id(uint64_t entity_id)
            {
                static unordered_map<uint64_t, uint32_t> entity_to_id_map;
                static uint32_t next_id = 0;

                // return existing
                auto it = entity_to_id_map.find(entity_id);
                if (it != entity_to_id_map.end())
                    return it->second;

                // return a new one
                entity_to_id_map[entity_id] = next_id++;
                return entity_to_id_map[entity_id];
            }

            uint32_t register_geometry_buffer(const RHI_Buffer* buffer)
            {
                // return existing
                {
                    auto it = find_if(instance_buffers.begin(), instance_buffers.end(), [buffer](const auto& pair)
                    {
                        return pair.first == buffer;
                    });

                    if (it != instance_buffers.end())
                        return it->second;
                }

                // else register a new one (they need VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
                {
                    uint32_t index = 0;

                    FfxBrixelizerBufferDescription buffer_desc = {};
                    buffer_desc.buffer                         = to_resource(buffer, L"brixelizer_gi_buffer");
                    buffer_desc.outIndex                       = &index;
                    SP_ASSERT(ffxBrixelizerRegisterBuffers(&context, &buffer_desc, 1) == FFX_OK);

                    instance_buffers.emplace_back(buffer, index);

                    return index;
                }
            }

            FfxBrixelizerInstanceDescription create_instance_description(const shared_ptr<Entity>& entity, uint32_t instance_index = 0)
            {
                FfxBrixelizerInstanceDescription desc = {};
                Renderable* renderable                = entity->GetComponent<Renderable>();
            
                // aabb: world space, pre-transformed
                const BoundingBox& aabb = renderable->HasInstancing() ? renderable->GetBoundingBoxInstance(instance_index) : renderable->GetBoundingBox();
                desc.aabb.min[0]        = aabb.GetMin().x;
                desc.aabb.min[1]        = aabb.GetMin().y;
                desc.aabb.min[2]        = aabb.GetMin().z;
                desc.aabb.max[0]        = aabb.GetMax().x;
                desc.aabb.max[1]        = aabb.GetMax().y;
                desc.aabb.max[2]        = aabb.GetMax().z;
            
                // transform: world space, row-major
                Matrix transform = entity->GetMatrix();
                if (renderable->HasInstancing())
                {
                     transform *= renderable->GetInstanceTransform(instance_index);
                }
                set_float16(desc.transform, transform);
            
                // vertex buffer
                desc.vertexBuffer       = register_geometry_buffer(renderable->GetVertexBuffer());
                desc.vertexStride       = renderable->GetVertexBuffer()->GetStride();
                desc.vertexBufferOffset = renderable->GetVertexOffset() * desc.vertexStride;
                desc.vertexCount        = renderable->GetVertexCount();
                desc.vertexFormat       = FFX_SURFACE_FORMAT_R32G32B32_FLOAT;
            
                // index buffer
                desc.indexBuffer       = register_geometry_buffer(renderable->GetIndexBuffer());
                desc.indexBufferOffset = renderable->GetIndexOffset() * renderable->GetIndexBuffer()->GetStride();
                desc.triangleCount     = renderable->GetIndexCount() / 3;
                desc.indexFormat       = (renderable->GetIndexBuffer()->GetStride() == sizeof(uint16_t)) ? FFX_INDEX_TYPE_UINT16 : FFX_INDEX_TYPE_UINT32;
            
                // misc
                desc.flags           = entity->GetTimeSinceLastTransform() == 0.0f ? FFX_BRIXELIZER_INSTANCE_FLAG_DYNAMIC : FFX_BRIXELIZER_INSTANCE_FLAG_NONE;
                uint64_t instance_id = renderable->HasInstancing() ? (entity->GetObjectId() | (static_cast<uint64_t>(instance_index) << 32)) : entity->GetObjectId();
                desc.outInstanceID   = &get_or_create_id(instance_id);
            
                return desc;
            }
            
            FfxBrixelizerTraceDebugModes to_ffx_debug_mode(const DebugMode debug_mode)
            {
                if (debug_mode == gi::DebugMode::Distance)   return FFX_BRIXELIZER_TRACE_DEBUG_MODE_DISTANCE;
                if (debug_mode == gi::DebugMode::UVW)        return FFX_BRIXELIZER_TRACE_DEBUG_MODE_UVW;
                if (debug_mode == gi::DebugMode::Iterations) return FFX_BRIXELIZER_TRACE_DEBUG_MODE_ITERATIONS;
                if (debug_mode == gi::DebugMode::Gradient)   return FFX_BRIXELIZER_TRACE_DEBUG_MODE_GRAD;
                if (debug_mode == gi::DebugMode::BrickID)    return FFX_BRIXELIZER_TRACE_DEBUG_MODE_BRICK_ID;
                if (debug_mode == gi::DebugMode::CascadeID)  return FFX_BRIXELIZER_TRACE_DEBUG_MODE_CASCADE_ID;
            
                return FFX_BRIXELIZER_TRACE_DEBUG_MODE_DISTANCE;
            }
            
            string debug_mode_to_string(const DebugMode debug_mode)
            {
               if (debug_mode == gi::DebugMode::Distance)   return "Distance";
               if (debug_mode == gi::DebugMode::UVW)        return "UVW";
               if (debug_mode == gi::DebugMode::Iterations) return "Iterations";
               if (debug_mode == gi::DebugMode::Gradient)   return "Gradient";
               if (debug_mode == gi::DebugMode::BrickID)    return "Brick ID";
               if (debug_mode == gi::DebugMode::CascadeID)  return "Cascade ID";
               if (debug_mode == gi::DebugMode::Radiance)   return "Radiance";
               if (debug_mode == gi::DebugMode::Irradiance) return "Irradiance";

               return "Disabled";
            }

            void context_destroy()
            {
                if (context_created)
                {
                    RHI_Device::QueueWaitAll();

                    SP_ASSERT(ffxBrixelizerContextDestroy(&context) == FFX_OK);
                    SP_ASSERT(ffxBrixelizerGIContextDestroy(&context_gi) == FFX_OK);

                    static_instances.clear();
                    instance_buffers.clear();
                    entity_map.clear();
                    instances_to_create.clear();
                    instances_to_delete.clear();
                    context_created = false;
                }
            }

            void context_create()
            {
                context_destroy();

                // context
                {
                    // sdf
                    set_float3(description_context.sdfCenter, Vector3::Zero);
                
                    // cascades
                    description_context.numCascades = cascade_count;
                    float voxel_size_ = voxel_size;
                    for (uint32_t i = 0; i < cascade_count; ++i)
                    {
                        FfxBrixelizerCascadeDescription* cascade_description   = &description_context.cascadeDescs[i];
                        cascade_description->flags                             = static_cast<FfxBrixelizerCascadeFlag>(FFX_BRIXELIZER_CASCADE_STATIC | FFX_BRIXELIZER_CASCADE_DYNAMIC);
                        cascade_description->voxelSize                         = voxel_size_;
                        voxel_size_                                           *= cascade_size_ratio;
                    }
                
                    // interface
                    description_context.flags            = debug_mode_aabbs_and_stats ? FFX_BRIXELIZER_CONTEXT_FLAG_ALL_DEBUG : static_cast<FfxBrixelizerContextFlags>(0);
                    description_context.backendInterface = ffx_interface;
                
                    SP_ASSERT(ffxBrixelizerContextCreate(&description_context, &context) == FFX_OK);
                }
                
                // context gi
                {
                    description_context_gi.internalResolution = internal_resolution;
                    description_context_gi.displaySize.width  = common::resolution_render_width;
                    description_context_gi.displaySize.height = common::resolution_render_height;
                    description_context_gi.flags              = FfxBrixelizerGIFlags::FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED;
                    description_context_gi.backendInterface   = ffx_interface;
                    
                    SP_ASSERT(ffxBrixelizerGIContextCreate(&context_gi, &description_context_gi) == FFX_OK);
                }
                
                // resources
                {
                    uint32_t flags = RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit;
                    texture_depth_previous  = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, common::resolution_render_width, common::resolution_render_height, 1, 1, RHI_Format::D32_Float, flags, "ffx_deoth_previous");
                    texture_normal_previous = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, common::resolution_render_width, common::resolution_render_height, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "ffx_normal_previous");
                }
                
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

    #endif

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
                FFX_FSR3_CONTEXT_COUNT          +
                FFX_SSSR_CONTEXT_COUNT          +
                FFX_BRIXELIZER_CONTEXT_COUNT    +
                FFX_BRIXELIZER_GI_CONTEXT_COUNT;
                Debugging::IsBreadcrumbsEnabled() ? FFX_BREADCRUMBS_CONTEXT_COUNT : 0;
            
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
            // shared
            {
                amd::texture_skybox = make_shared<RHI_Texture>(RHI_Texture_Type::TypeCube, 128, 128, 6, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv | RHI_Texture_Uav, "skybox");
            }

            // brixelizer gi
            {
                // sdf atlas texture
                amd::gi::texture_sdf_atlas = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type3D,
                    FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE,
                    FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE,
                    FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE,
                    1,
                    RHI_Format::R8_Unorm,
                    RHI_Texture_Srv | RHI_Texture_Uav,
                    "ffx_sdf_atlas"
                );

                // scratch buffer
                amd::gi::buffer_scratch = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Storage,
                    1 << 30, // stride - 1024 MB (will assert if not enough)
                    1,       // element count
                    nullptr,
                    false,
                    "ffx_brixelizer_gi_scratch"
                );

                // brick aabbs buffer
                amd::gi::buffer_brick_aabbs = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Storage,
                    static_cast<uint32_t>(FFX_BRIXELIZER_BRICK_AABBS_STRIDE),                                   // stride
                    static_cast<uint32_t>(FFX_BRIXELIZER_BRICK_AABBS_SIZE / FFX_BRIXELIZER_BRICK_AABBS_STRIDE), // element count
                    nullptr,
                    false,
                    "ffx_brick_aabbs"
                );

                // cascade cascade aabb trees
                for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
                {
                    amd::gi::buffer_cascade_aabb_tree[i] = make_shared<RHI_Buffer>(
                        RHI_Buffer_Type::Storage,
                        FFX_BRIXELIZER_CASCADE_AABB_TREE_STRIDE,                                                                // stride
                        static_cast<uint32_t>(FFX_BRIXELIZER_CASCADE_AABB_TREE_SIZE / FFX_BRIXELIZER_CASCADE_AABB_TREE_STRIDE), // element count
                        nullptr,
                        false,
                        ("ffx_cascade_aabb_tree_" + to_string(i)).c_str()
                    );
                }

                // cascade brick maps
                for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
                {
                    string name = "ffx_cascade_brick_map_" + to_string(i);
                    amd::gi::buffer_cascade_brick_map[i] = make_shared<RHI_Buffer>(
                        RHI_Buffer_Type::Storage,
                        static_cast<uint32_t>(FFX_BRIXELIZER_CASCADE_BRICK_MAP_STRIDE),                                         // stride
                        static_cast<uint32_t>(FFX_BRIXELIZER_CASCADE_BRICK_MAP_SIZE / FFX_BRIXELIZER_CASCADE_BRICK_MAP_STRIDE), // element count
                        nullptr,
                        false,
                        name.c_str()
                    );
                }
            }
        }
    #endif
    }

    void RHI_VendorTechnology::Shutdown()
    {
    #ifdef _WIN32
        amd::uscaler::context_destroy();
        amd::gi::context_destroy();
        amd::ssr::context_destroy();
        amd::breadcrumbs::context_destroy();

        // ffx interface
        if (amd::ffx_interface.scratchBuffer != nullptr)
        {
            free(amd::ffx_interface.scratchBuffer);
        }

        // release static resources now so that they register
        // themselves with the RHI for deletion before engine shutdown
        amd::gi::texture_sdf_atlas       = nullptr;
        amd::gi::buffer_brick_aabbs      = nullptr;
        amd::gi::buffer_scratch          = nullptr;
        amd::gi::texture_depth_previous  = nullptr;
        amd::gi::texture_normal_previous = nullptr;
        amd::gi::buffer_cascade_aabb_tree.fill(nullptr);
        amd::gi::buffer_cascade_brick_map.fill(nullptr);

        // shared
        amd::texture_skybox = nullptr;
    #endif
    }

    void RHI_VendorTechnology::Tick(Cb_Frame* cb_frame)
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

        // brixelizer gi
        if (amd::gi::debug_mode_arrow_switch)
        {
            if (Input::GetKeyDown(KeyCode::Arrow_Left))
            {
                amd::gi::debug_mode = static_cast<amd::gi::DebugMode>(
                    (static_cast<uint32_t>(amd::gi::debug_mode) - 1 + static_cast<uint32_t>(amd::gi::DebugMode::Max) + 1) %
                    (static_cast<uint32_t>(amd::gi::DebugMode::Max) + 1));
                SP_LOG_INFO("Debug mode: %s", amd::gi::debug_mode_to_string(amd::gi::debug_mode));
            }
            else if (Input::GetKeyDown(KeyCode::Arrow_Right))
            {
                amd::gi::debug_mode = static_cast<amd::gi::DebugMode>(
                    (static_cast<uint32_t>(amd::gi::debug_mode) + 1) %
                    (static_cast<uint32_t>(amd::gi::DebugMode::Max) + 1));
                SP_LOG_INFO("Debug mode: %s", amd::gi::debug_mode_to_string(amd::gi::debug_mode));
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

    void RHI_VendorTechnology::Resize(const Vector2& resolution_render, const Vector2& resolution_output)
    {
    #ifdef _WIN32
        bool resolution_render_changed = resolution_render.x != common::resolution_render_width  || resolution_render.y != common::resolution_render_height;
        bool resolution_output_changed = resolution_output.x != common::resolution_output_width  || resolution_output.y != common::resolution_output_height;

        common::resolution_render_width  = static_cast<uint32_t>(resolution_render.x);
        common::resolution_render_height = static_cast<uint32_t>(resolution_render.y);
        common::resolution_output_width  = static_cast<uint32_t>(resolution_output.x);
        common::resolution_output_height = static_cast<uint32_t>(resolution_output.y);

        // re-create resolution dependent contexts
        {
            if (resolution_render_changed)
            {
                amd::ssr::context_create();
                amd::gi::context_create();
            }

            // todo: make these mutually exlusive
            if ((resolution_render_changed || resolution_output_changed)) 
            {
                amd::uscaler::context_create();
                intel::context_create();
            }
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
        auto get_corput = [](std::uint32_t index, std::uint32_t base) -> float
        {
            float result = 0.0f;
            float bk = 1.0f;
            while (index > 0)
            {
                bk /= static_cast<float>(base);
                result += static_cast<float>(index % base) * bk;
                index /= base;
            }
            return result;
        };
    
        // static storage for halton points and index
        static std::vector<std::pair<float, float>> halton_points;
        static size_t halton_index = 0;
    
        // generate 32 halton points (bases 2 and 3, start index 1) if not already done
        if (halton_points.empty())
        {
            std::uint32_t base_x      = 2;
            std::uint32_t base_y      = 3;
            std::uint32_t start_index = 1;
            std::uint32_t count       = intel::get_sample_count();
            float offset_x            = 0.0f;
            float offset_y            = 0.0f;
            halton_points.reserve(count);
            for (std::uint32_t i = start_index; i < start_index + count; ++i)
            {
                // generate x and y in [0, 1], shift to [-0.5, 0.5] for pixel space
                float jitter_x = get_corput(i, base_x) - 0.5f;
                float jitter_y = get_corput(i, base_y) - 0.5f;
                halton_points.emplace_back(jitter_x, jitter_y);
            }
        }
    
        // get the current jitter sample (pixel space, [-0.5, 0.5])
        auto jitter = halton_points[halton_index];

        // this is for xessVKExecute which expects [-0.5, 0.5] jitter
        intel::jitter.x = jitter.first;
        intel::jitter.y = jitter.second;

        // write scaled jitter for projection matrix
        *x =  2.0f * jitter.first  / static_cast<float>(common::resolution_render_width);
        *y = -2.0f * jitter.second / static_cast<float>(common::resolution_render_height);

        // advance to the next sample, cycling back to 0
        halton_index = (halton_index + 1) % halton_points.size();
    }

    void RHI_VendorTechnology::XeSS_Dispatch(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
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
        intel::params_execute.responsivePixelMaskTexture = intel::to_xess_image_view(Renderer::GetStandardTexture(Renderer_StandardTexture::White)); // neutralize and control via float
        intel::params_execute.jitterOffsetX              = intel::jitter.x;
        intel::params_execute.jitterOffsetY              = intel::jitter.y;
        intel::params_execute.exposureScale              = intel::exposure_scale;
        intel::params_execute.inputWidth                 = static_cast<uint32_t>(tex_color->GetWidth() * resolution_scale);
        intel::params_execute.inputHeight                = static_cast<uint32_t>(tex_color->GetHeight() * resolution_scale);
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
        const uint32_t resolution_render_x = static_cast<uint32_t>(amd::uscaler::description_context.maxRenderSize.width);
        const uint32_t resolution_render_y = static_cast<uint32_t>(amd::uscaler::description_context.maxRenderSize.height);
        const int32_t jitter_phase_count   = ffxFsr3GetJitterPhaseCount(resolution_render_x, resolution_render_x);

        // ensure fsr3_jitter_index is properly wrapped around the jitter_phase_count
        amd::uscaler::jitter_index = (amd::uscaler::jitter_index + 1) % jitter_phase_count;

        // generate jitter sample
        FfxErrorCode result = ffxFsr3GetJitterOffset(&amd::uscaler::description_dispatch.jitterOffset.x, &amd::uscaler::description_dispatch.jitterOffset.y, amd::uscaler::jitter_index, jitter_phase_count);
        SP_ASSERT(result == FFX_OK);

        *x =  2.0f * amd::uscaler::description_dispatch.jitterOffset.x / resolution_render_x;
        *y = -2.0f * amd::uscaler::description_dispatch.jitterOffset.y / resolution_render_y;
    #endif
    }

    void RHI_VendorTechnology::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_output
    )
    {
    #ifdef _WIN32
        // output is displayed in the viewport, so add a barrier to ensure any work is done before writing to it
        cmd_list->InsertBarrier(tex_output->GetRhiResource(), tex_output->GetFormat(), 0, 1, 1, tex_output->GetLayout(0));
        cmd_list->InsertPendingBarrierGroup();

        // upscale
        {
            // set resources (no need for the transparency or reactive masks as we do them later, full res)
            amd::uscaler::description_dispatch.commandList                   = amd::to_cmd_list(cmd_list);
            amd::uscaler::description_dispatch.color                         = amd::to_resource(tex_color,                                                        L"fsr3_color");
            amd::uscaler::description_dispatch.depth                         = amd::to_resource(tex_depth,                                                        L"fsr3_depth");
            amd::uscaler::description_dispatch.motionVectors                 = amd::to_resource(tex_velocity,                                                     L"fsr3_velocity");
            amd::uscaler::description_dispatch.exposure                      = amd::to_resource(nullptr,                                                          L"fsr3_exposure");
            amd::uscaler::description_dispatch.reactive                      = amd::to_resource(nullptr,                                                          L"fsr3_reactive");
            amd::uscaler::description_dispatch.transparencyAndComposition    = amd::to_resource(nullptr,                                                          L"fsr3_transaprency_and_composition");
            amd::uscaler::description_dispatch.dilatedDepth                  = amd::to_resource(amd::uscaler::texture_depth_dilated.get(),                        L"fsr3_depth_dilated");
            amd::uscaler::description_dispatch.dilatedMotionVectors          = amd::to_resource(amd::uscaler::texture_motion_vectors_dilated.get(),               L"fsr3_motion_vectors_dilated");
            amd::uscaler::description_dispatch.reconstructedPrevNearestDepth = amd::to_resource(amd::uscaler::texture_depth_previous_nearest_reconstructed.get(), L"fsr3_depth_nearest_previous_reconstructed");
            amd::uscaler::description_dispatch.output                        = amd::to_resource(tex_output,                                                       L"fsr3_output");

            // configure
            amd::uscaler::description_dispatch.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth()) * 0.5f;
            amd::uscaler::description_dispatch.motionVectorScale.y    = static_cast<float>(tex_velocity->GetHeight()) * 0.5f;
            amd::uscaler::description_dispatch.enableSharpening       = sharpness != 0.0f;         // sdk issue: redundant parameter
            amd::uscaler::description_dispatch.sharpness              = sharpness;
            amd::uscaler::description_dispatch.frameTimeDelta         = delta_time_sec * 1000.0f;  // seconds to milliseconds
            amd::uscaler::description_dispatch.preExposure            = 1.0f;                      // the exposure value if not using FFX_FSR3_ENABLE_AUTO_EXPOSURE
            amd::uscaler::description_dispatch.renderSize.width       = static_cast<uint32_t>(tex_velocity->GetWidth() * resolution_scale);
            amd::uscaler::description_dispatch.renderSize.height      = static_cast<uint32_t>(tex_velocity->GetHeight() * resolution_scale);
            amd::uscaler::description_dispatch.cameraNear             = camera->GetFarPlane();     // far as near because we are using reverse-z
            amd::uscaler::description_dispatch.cameraFar              = camera->GetNearPlane();    // near as far because we are using reverse-z
            amd::uscaler::description_dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();

            // reset history
            amd::uscaler::description_dispatch.reset = common::reset_history;
            common::reset_history = false;

            // dispatch
            SP_ASSERT(ffxFsr3UpscalerContextDispatch(&amd::uscaler::context, &amd::uscaler::description_dispatch) == FFX_OK);
            amd::uscaler::description_dispatch.reset = false;
        }
    #endif
    }

    void RHI_VendorTechnology::SSSR_Dispatch(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
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
        SP_ASSERT(amd::ssr::context_created);

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
        amd::ssr::description_dispatch.renderSize.width  = static_cast<uint32_t>(tex_reflection_source->GetWidth()  * resolution_scale);
        amd::ssr::description_dispatch.renderSize.height = static_cast<uint32_t>(tex_reflection_source->GetHeight() * resolution_scale);

        // set sssr specific parameters
        amd::ssr::description_dispatch.motionVectorScale.x                  = 0.25f;  // convert ndc x-velocity delta[-2, 2] to SSSR texture space [-0.5, 0.5] by dividing by 4
        amd::ssr::description_dispatch.motionVectorScale.y                  = -0.25f; // convert ndc y-velocity delta[-2, 2] to [-0.5, 0.5] and flip Y (NDC +Y up to SSSR +Y down)
        amd::ssr::description_dispatch.normalUnPackMul                      = 1.0f;
        amd::ssr::description_dispatch.normalUnPackAdd                      = 0.0f;
        amd::ssr::description_dispatch.depthBufferThickness                 = 0.2f;  // hit acceptance bias, larger values can cause streaks, lower values can cause holes
        amd::ssr::description_dispatch.varianceThreshold                    = 0.04f; // luminance differences between history results will trigger an additional ray if they are greater than this threshold value
        amd::ssr::description_dispatch.maxTraversalIntersections            = 100;   // caps the maximum number of lookups that are performed from the depth buffer hierarchy, most rays should end after about 20 lookups
        amd::ssr::description_dispatch.minTraversalOccupancy                = 4;     // exit the core loop early if less than this number of threads are running
        amd::ssr::description_dispatch.mostDetailedMip                      = 0;
        amd::ssr::description_dispatch.temporalStabilityFactor              = 0.6f;  // the accumulation of history values, higher values reduce noise, but are more likely to exhibit ghosting artifacts
        amd::ssr::description_dispatch.temporalVarianceGuidedTracingEnabled = true;  // whether a ray should be spawned on pixels where a temporal variance is detected or not
        amd::ssr::description_dispatch.samplesPerQuad                       = 1;     // the minimum number of rays per quad, variance guided tracing can increase this up to a maximum of 4
        amd::ssr::description_dispatch.iblFactor                            = 0.0f;
        amd::ssr::description_dispatch.roughnessChannel                     = 0;
        amd::ssr::description_dispatch.isRoughnessPerceptual                = true;
        amd::ssr::description_dispatch.roughnessThreshold                   = 0.5f;  // regions with a roughness value greater than this threshold won't spawn rays

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

    void RHI_VendorTechnology::BrixelizerGI_Update(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
        Cb_Frame* cb_frame,
        const vector<shared_ptr<Entity>>& entities,
        RHI_Texture* tex_debug
    )
    {
    #ifdef _WIN32
        SP_ASSERT(amd::gi::context_created);

        // instances
        {
            amd::gi::instances_to_create.clear();
            amd::gi::instances_to_delete.clear();
            amd::gi::entity_map.clear();
        
            // process entities
            for (const shared_ptr<Entity>& entity : entities)
            {
                if (!entity->GetActive())
                    continue;

                // skip entities that won't contribute yet will kill performance
                Renderable* renderable = entity->GetComponent<Renderable>();
                if (!renderable || renderable->GetMaterial()->GetProperty(MaterialProperty::IsGrassBlade) || renderable->GetMaterial()->IsTransparent())
                    continue;

                uint64_t entity_id                   = entity->GetObjectId();
                amd::gi::entity_map[entity_id] = entity;
                bool is_dynamic                      = entity->GetTimeSinceLastTransform() == 0.0f;
                auto static_it                       = amd::gi::static_instances.find(entity_id);
                bool was_static                      = static_it != amd::gi::static_instances.end();

                if (is_dynamic)
                {
                    if (renderable->HasInstancing())
                    {
                        for (uint32_t instance_index = 0; instance_index < renderable->GetInstanceCount(); instance_index++)
                        {
                            uint64_t instance_id = entity_id | (static_cast<uint64_t>(instance_index) << 32);
                            amd::gi::instances_to_create.push_back(amd::gi::create_instance_description(entity, instance_index));
                            
                            auto static_instance_it = amd::gi::static_instances.find(instance_id);
                            if (static_instance_it != amd::gi::static_instances.end())
                            {
                                amd::gi::instances_to_delete.push_back(amd::gi::get_or_create_id(instance_id));
                                amd::gi::static_instances.erase(static_instance_it);
                                if (amd::gi::debug_mode_log_instances)
                                {
                                    SP_LOG_INFO("Static instance became dynamic: %llu (instance %u)", entity_id, instance_index);
                                }
                            }
                        }
                    }
                    else
                    {
                        amd::gi::instances_to_create.push_back(amd::gi::create_instance_description(entity));
                        
                        if (was_static)
                        {
                            amd::gi::instances_to_delete.push_back(amd::gi::get_or_create_id(entity_id));
                            amd::gi::static_instances.erase(static_it);
                            if (amd::gi::debug_mode_log_instances)
                            {
                                SP_LOG_INFO("Static instance became dynamic: %llu", entity_id);
                            }
                        }
                    }
                }
                else if (!was_static)
                {
                    if (renderable->HasInstancing())
                    {
                        for (uint32_t instance_index = 0; instance_index < renderable->GetInstanceCount(); instance_index++)
                        {
                            uint64_t instance_id = entity_id | (static_cast<uint64_t>(instance_index) << 32);
                            amd::gi::instances_to_create.push_back(amd::gi::create_instance_description(entity, instance_index));
                            amd::gi::static_instances.insert(instance_id);
                            if (amd::gi::debug_mode_log_instances)
                            {
                                SP_LOG_INFO("Added new static instance: %llu (instance %u)", entity_id, instance_index);
                            }
                        }
                    }
                    else
                    {
                        amd::gi::instances_to_create.push_back(amd::gi::create_instance_description(entity));
                        amd::gi::static_instances.insert(entity_id);
                        if (amd::gi::debug_mode_log_instances)
                        {
                            SP_LOG_INFO("Added new static instance: %llu", entity_id);
                        }
                    }
                }
            }
        
            // delete static instances that no longer exist
            for (auto it = amd::gi::static_instances.begin(); it != amd::gi::static_instances.end();)
            {
                uint64_t entity_id = *it;
                if (amd::gi::entity_map.find(entity_id) == amd::gi::entity_map.end())
                {
                    amd::gi::instances_to_delete.push_back(amd::gi::get_or_create_id(entity_id));
                    it = amd::gi::static_instances.erase(it);
                    if (amd::gi::debug_mode_log_instances)
                    {
                        SP_LOG_INFO("Deleted non-existent static instance: %llu", entity_id);
                    }
                }
                else
                {
                    ++it;
                }
            }
        
            // create instances
            if (!amd::gi::instances_to_create.empty())
            {
                SP_ASSERT(ffxBrixelizerCreateInstances(&amd::gi::context, amd::gi::instances_to_create.data(), static_cast<uint32_t>(amd::gi::instances_to_create.size())) == FFX_OK);
            }
        
            // delete instances
            if (!amd::gi::instances_to_delete.empty())
            {
                SP_ASSERT(ffxBrixelizerDeleteInstances(&amd::gi::context, amd::gi::instances_to_delete.data(), static_cast<uint32_t>(amd::gi::instances_to_delete.size())) == FFX_OK);
            }
        }

        // fill in the update description
        for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
        {
            amd::gi::description_update.resources.cascadeResources[i].aabbTree = amd::to_resource(amd::gi::buffer_cascade_aabb_tree[i].get(), L"brixelizer_gi_abbb_tree");
            amd::gi::description_update.resources.cascadeResources[i].brickMap = amd::to_resource(amd::gi::buffer_cascade_brick_map[i].get(), L"brixelizer_gi_brick_map");
        }
        amd::gi::description_update.resources.sdfAtlas   = amd::to_resource(amd::gi::texture_sdf_atlas.get(),  L"brixelizer_gi_sdf_atlas");
        amd::gi::description_update.resources.brickAABBs = amd::to_resource(amd::gi::buffer_brick_aabbs.get(), L"brixelizer_gi_brick_aabbs");
        amd::gi::description_update.frameIndex           = cb_frame->frame;
        amd::gi::description_update.maxReferences        = amd::gi::triangle_references_max;
        amd::gi::description_update.triangleSwapSize     = amd::gi::triangle_swap_size;
        amd::gi::description_update.maxBricksPerBake     = amd::gi::bricks_per_update_max;
        size_t required_scratch_buffer_size                    = 0;
        amd::gi::description_update.outScratchBufferSize = &required_scratch_buffer_size; // the size of the gpu scratch buffer needed for ffxBrixelizerUpdate()
        amd::gi::description_update.outStats             = &amd::gi::debug_stats;   // statistics for the update, stats read back after ffxBrixelizerUpdate()
        amd::set_float3(amd::gi::description_update.sdfCenter, amd::gi::sdf_center_around_camera ? cb_frame->camera_position : Vector3::Zero); // sdf center in world space

        // debug visualization for: distance, uvw, iterations, brick id, cascade id
        bool debug_enabled = amd::gi::debug_mode != amd::gi::DebugMode::Max;
        bool debug_update  = amd::gi::debug_mode != amd::gi::DebugMode::Radiance && amd::gi::debug_mode != amd::gi::DebugMode::Irradiance;
        if (debug_enabled && debug_update)
        {
            FfxBrixelizerPopulateDebugAABBsFlags flags = static_cast<FfxBrixelizerPopulateDebugAABBsFlags>(FFX_BRIXELIZER_POPULATE_AABBS_INSTANCES | FFX_BRIXELIZER_POPULATE_AABBS_CASCADE_AABBS);

            for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
            {
                amd::gi::debug_description.cascadeDebugAABB[0] = FFX_BRIXELIZER_CASCADE_DEBUG_AABB_NONE;
            }

            amd::gi::description_update.populateDebugAABBsFlags = amd::gi::debug_mode_aabbs_and_stats ? flags : FFX_BRIXELIZER_POPULATE_AABBS_NONE;
            amd::gi::description_update.debugVisualizationDesc  = &amd::gi::debug_description;
            amd::gi::debug_description.commandList              = amd::to_cmd_list(cmd_list);
            amd::gi::debug_description.output                   = amd::to_resource(tex_debug, L"brixelizer_gi_tex_debug");
            amd::gi::debug_description.renderWidth              = static_cast<uint32_t>(tex_debug->GetWidth() * resolution_scale);
            amd::gi::debug_description.renderHeight             = static_cast<uint32_t>(tex_debug->GetHeight() * resolution_scale);
            amd::gi::debug_description.debugState               = amd::gi::to_ffx_debug_mode(amd::gi::debug_mode);
            amd::gi::debug_description.startCascadeIndex        = amd::gi::cascade_index_start;
            amd::gi::debug_description.endCascadeIndex          = amd::gi::cascade_index_end;
            amd::gi::debug_description.tMin                     = amd::gi::t_min;
            amd::gi::debug_description.tMax                     = amd::gi::t_max;
            amd::gi::debug_description.sdfSolveEps              = amd::gi::sdf_ray_epsilon;
           
            amd::set_float16(amd::gi::debug_description.inverseViewMatrix,       amd::view_inverted);
            amd::set_float16(amd::gi::debug_description.inverseProjectionMatrix, amd::projection_inverted);
        }
        
        // update
        SP_ASSERT(ffxBrixelizerBakeUpdate(&amd::gi::context, &amd::gi::description_update, &amd::gi::description_update_baked) == FFX_OK);
        SP_ASSERT_MSG(required_scratch_buffer_size <= amd::gi::buffer_scratch->GetObjectSize(), "Create a larger scratch buffer");
        SP_ASSERT(ffxBrixelizerUpdate(&amd::gi::context, &amd::gi::description_update_baked, amd::to_resource(amd::gi::buffer_scratch.get(), L"ffx_brixelizer_gi_scratch"), amd::to_cmd_list(cmd_list)) == FFX_OK);
    #endif
    }

    void RHI_VendorTechnology::BrixelizerGI_Dispatch(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        RHI_Texture* tex_frame,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        array<RHI_Texture*, 8>& tex_noise,
        RHI_Texture* tex_diffuse_gi,
        RHI_Texture* tex_specular_gi,
        RHI_Texture* tex_debug
    )
    {
    #ifdef _WIN32
        SP_ASSERT(amd::gi::context_created);

        bool debug_enabled  = amd::gi::debug_mode != amd::gi::DebugMode::Max;
        bool debug_dispatch = amd::gi::debug_mode == amd::gi::DebugMode::Radiance || amd::gi::debug_mode == amd::gi::DebugMode::Irradiance;
        bool debug_update   = debug_enabled && !debug_dispatch;
        if (debug_update)
            return;

        // end the render pass (if there is one) as third-party code takes over here
        cmd_list->RenderPassEnd();

        // set camera matrices
        amd::set_float16(amd::gi::description_dispatch_gi.view,           amd::view);
        amd::set_float16(amd::gi::description_dispatch_gi.prevView,       amd::view_previous);
        amd::set_float16(amd::gi::description_dispatch_gi.projection,     amd::projection);
        amd::set_float16(amd::gi::description_dispatch_gi.prevProjection, amd::projection_previous);

        // set resources
        amd::gi::description_dispatch_gi.environmentMap   = amd::to_resource(amd::texture_skybox.get(),                     L"brixelizer_gi_environment");
        amd::gi::description_dispatch_gi.prevLitOutput    = amd::to_resource(tex_frame,                                     L"brixelizer_gi_lit_output_previous"); // linear
        amd::gi::description_dispatch_gi.depth            = amd::to_resource(tex_depth,                                     L"brixelizer_gi_depth");
        amd::gi::description_dispatch_gi.historyDepth     = amd::to_resource(amd::gi::texture_depth_previous.get(),         L"brixelizer_gi_depth_previous");
        amd::gi::description_dispatch_gi.normal           = amd::to_resource(tex_normal,                                    L"brixelizer_gi_normal");
        amd::gi::description_dispatch_gi.historyNormal    = amd::to_resource(amd::gi::texture_normal_previous.get(),        L"brixelizer_gi_normal_previous");
        amd::gi::description_dispatch_gi.roughness        = amd::to_resource(tex_material,                                  L"brixelizer_gi_roughness");
        amd::gi::description_dispatch_gi.motionVectors    = amd::to_resource(tex_velocity,                                  L"brixelizer_gi_velocity");
        amd::gi::description_dispatch_gi.noiseTexture     = amd::to_resource(tex_noise[cb_frame->frame % tex_noise.size()], L"brixelizer_gi_noise");
        amd::gi::description_dispatch_gi.outputDiffuseGI  = amd::to_resource(tex_diffuse_gi,                                L"brixelizer_gi_diffuse_gi");
        amd::gi::description_dispatch_gi.outputSpecularGI = amd::to_resource(tex_specular_gi,                               L"brixelizer_gi_specular_gi");
        amd::gi::description_dispatch_gi.sdfAtlas         = amd::to_resource(amd::gi::texture_sdf_atlas.get(),              L"brixelizer_gi_sdf_atlas");
        amd::gi::description_dispatch_gi.bricksAABBs      = amd::to_resource(amd::gi::buffer_brick_aabbs.get(),             L"brixelizer_gi_brick_aabbs");
        for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
        {
            amd::gi::description_dispatch_gi.cascadeAABBTrees[i] = amd::gi::description_update.resources.cascadeResources[i].aabbTree;
            amd::gi::description_dispatch_gi.cascadeBrickMaps[i] = amd::gi::description_update.resources.cascadeResources[i].brickMap;
        }
        
        // set parameters
        amd::gi::description_dispatch_gi.startCascade            = amd::gi::cascade_index_start;
        amd::gi::description_dispatch_gi.endCascade              = amd::gi::cascade_index_end;
        amd::gi::description_dispatch_gi.rayPushoff              = amd::gi::sdf_ray_normal_offset;
        amd::gi::description_dispatch_gi.sdfSolveEps             = amd::gi::sdf_ray_epsilon;
        amd::gi::description_dispatch_gi.specularRayPushoff      = amd::gi::sdf_ray_normal_offset;
        amd::gi::description_dispatch_gi.specularSDFSolveEps     = amd::gi::sdf_ray_epsilon;
        amd::gi::description_dispatch_gi.tMin                    = amd::gi::t_min;
        amd::gi::description_dispatch_gi.tMax                    = amd::gi::t_max;
        amd::gi::description_dispatch_gi.normalsUnpackMul        = 1.0f;
        amd::gi::description_dispatch_gi.normalsUnpackAdd        = 0.0f;
        amd::gi::description_dispatch_gi.isRoughnessPerceptual   = true; // false for squared g-buffer roughness
        amd::gi::description_dispatch_gi.roughnessChannel        = 0;    // the channel to read the roughness from the roughness texture
        amd::gi::description_dispatch_gi.roughnessThreshold      = 0.8f; // regions with a roughness value greater than this threshold won't spawn specular rays
        amd::gi::description_dispatch_gi.environmentMapIntensity = 0.0f; // value to scale the contribution from the environment map
        amd::gi::description_dispatch_gi.motionVectorScale.x     = -0.5f;
        amd::gi::description_dispatch_gi.motionVectorScale.y     = 0.5f;
        amd::set_float3(amd::gi::description_dispatch_gi.cameraPosition, cb_frame->camera_position);

        // dispatch
        SP_ASSERT(ffxBrixelizerGetRawContext(&amd::gi::context, &amd::gi::description_dispatch_gi.brixelizerContext) == FFX_OK);
        SP_ASSERT(ffxBrixelizerGIContextDispatch(&amd::gi::context_gi, &amd::gi::description_dispatch_gi, amd::to_cmd_list(cmd_list)) == FFX_OK);

        // blit the the normal so that we can use it in the next frame as as the previous
        cmd_list->Blit(tex_normal, amd::gi::texture_normal_previous.get(), false);

        // debug visualisation
        if (amd::gi::debug_mode == amd::gi::DebugMode::Radiance || amd::gi::debug_mode == amd::gi::DebugMode::Irradiance)
        {
            // set camera matrices
            amd::set_float16(amd::gi::debug_description_gi.view,       amd::view);
            amd::set_float16(amd::gi::debug_description_gi.projection, amd::projection);

            // set resources
            amd::gi::debug_description_gi.outputDebug   = amd::to_resource(tex_debug, L"brixelizer_gi_debug");
            amd::gi::debug_description_gi.outputSize[0] = tex_debug->GetWidth();
            amd::gi::debug_description_gi.outputSize[1] = tex_debug->GetHeight();
            amd::gi::debug_description_gi.depth         = amd::gi::description_dispatch_gi.depth;
            amd::gi::debug_description_gi.normal        = amd::gi::description_dispatch_gi.normal;
            amd::gi::debug_description_gi.sdfAtlas      = amd::gi::description_dispatch_gi.sdfAtlas;
            amd::gi::debug_description_gi.bricksAABBs   = amd::gi::description_dispatch_gi.bricksAABBs;
            for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
            {
                amd::gi::debug_description_gi.cascadeAABBTrees[i] = amd::gi::description_dispatch_gi.cascadeAABBTrees[i];
                amd::gi::debug_description_gi.cascadeBrickMaps[i] = amd::gi::description_dispatch_gi.cascadeBrickMaps[i];
            }

            // set parameters
            amd::gi::debug_description_gi.startCascade     = amd::gi::description_dispatch_gi.startCascade;
            amd::gi::debug_description_gi.endCascade       = amd::gi::description_dispatch_gi.endCascade;
            amd::gi::debug_description_gi.debugMode        = amd::gi::debug_mode == amd::gi::DebugMode::Radiance ? FFX_BRIXELIZER_GI_DEBUG_MODE_RADIANCE_CACHE : FFX_BRIXELIZER_GI_DEBUG_MODE_IRRADIANCE_CACHE;
            amd::gi::debug_description_gi.normalsUnpackMul = amd::gi::description_dispatch_gi.normalsUnpackMul;
            amd::gi::debug_description_gi.normalsUnpackAdd = amd::gi::description_dispatch_gi.normalsUnpackAdd;

            // dispatch
            amd::gi::debug_description_gi.brixelizerContext = amd::gi::description_dispatch_gi.brixelizerContext;
            SP_ASSERT(ffxBrixelizerGIContextDebugVisualization(&amd::gi::context_gi, &amd::gi::debug_description_gi, amd::to_cmd_list(cmd_list)) == FFX_OK);
        }
    #endif
    }

    void RHI_VendorTechnology::BrixelizerGI_SetResolutionPercentage(const float resolution_percentage)
    {
        #ifdef _MSC_VER
        if (resolution_percentage == 0.25f)
        {
            amd::gi::internal_resolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_25_PERCENT;
        }
        else if (resolution_percentage == 0.5f)
        {
            amd::gi::internal_resolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT;
        }
        else if (resolution_percentage == 0.75f)
        {
            amd::gi::internal_resolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_75_PERCENT;
        }
        else if (resolution_percentage == 1.0f)
        {
            amd::gi::internal_resolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE;
        }
        else
        {
            SP_ASSERT_MSG(false, "Invalid percentage. Supported percentages are 0.25, 0.5, 0.75 and 1.0.");
        }

        amd::gi::context_create();
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
