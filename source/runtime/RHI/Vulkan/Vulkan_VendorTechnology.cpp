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
SP_WARNINGS_OFF
#ifdef _WIN32
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <xess/xess_vk.h>
#include <nrd/NRD.h>
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
        const float exposure_scale              = 1.0f; // neutral, let internal auto-exposure handle it
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
            if (!RHI_Device::IsSupportedXess())
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
            intel::params_init.initFlags          = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
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
            description_context.flags                 |= FFX_FSR3_ENABLE_AUTO_EXPOSURE;      // let fsr compute exposure internally for temporal stability
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

    }

    namespace nvidia
    {
        // nrd state
        nrd::Instance* nrd_instance           = nullptr;
        bool nrd_initialized                  = false;
        uint32_t nrd_width                    = 0;
        uint32_t nrd_height                   = 0;
        nrd::Identifier nrd_denoiser_identifier = 0;

        // texture pools
        vector<shared_ptr<RHI_Texture>> nrd_transient_textures;
        vector<shared_ptr<RHI_Texture>> nrd_permanent_textures;

        // vulkan resources for custom nrd pipeline
        VkDescriptorSetLayout nrd_descriptor_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout nrd_pipeline_layout            = VK_NULL_HANDLE;
        VkDescriptorPool nrd_descriptor_pool            = VK_NULL_HANDLE;
        VkSampler nrd_sampler_nearest                   = VK_NULL_HANDLE;
        VkSampler nrd_sampler_linear                    = VK_NULL_HANDLE;
        vector<VkPipeline> nrd_compute_pipelines;
        vector<VkShaderModule> nrd_shader_modules;
        vector<VkDescriptorSet> nrd_descriptor_sets;

        // constant buffer for nrd
        void* nrd_constant_buffer           = nullptr;
        void* nrd_constant_mapped           = nullptr;
        uint32_t nrd_constant_buffer_size   = 0;

        // binding offsets from nrd library
        nrd::SPIRVBindingOffsets nrd_binding_offsets = {};

        // maximum resources per dispatch (from instance desc)
        uint32_t nrd_max_textures         = 0;
        uint32_t nrd_max_storage_textures = 0;

        RHI_Format convert_nrd_format(nrd::Format format)
        {
            switch (format)
            {
                case nrd::Format::R8_UNORM:             return RHI_Format::R8_Unorm;
                case nrd::Format::R8_SNORM:             return RHI_Format::R8_Unorm;
                case nrd::Format::R8_UINT:              return RHI_Format::R8_Uint;
                case nrd::Format::R16_SFLOAT:           return RHI_Format::R16_Float;
                case nrd::Format::R16_UNORM:            return RHI_Format::R16_Unorm;
                case nrd::Format::RG8_UNORM:            return RHI_Format::R8G8_Unorm;
                case nrd::Format::RG16_SFLOAT:          return RHI_Format::R16G16_Float;
                case nrd::Format::RGBA8_UNORM:          return RHI_Format::R8G8B8A8_Unorm;
                case nrd::Format::RGBA16_SFLOAT:        return RHI_Format::R16G16B16A16_Float;
                case nrd::Format::R32_SFLOAT:           return RHI_Format::R32_Float;
                case nrd::Format::RG32_SFLOAT:          return RHI_Format::R32G32_Float;
                case nrd::Format::RGBA32_SFLOAT:        return RHI_Format::R32G32B32A32_Float;
                case nrd::Format::R10_G10_B10_A2_UNORM: return RHI_Format::R10G10B10A2_Unorm;
                case nrd::Format::R11_G11_B10_UFLOAT:   return RHI_Format::R11G11B10_Float;
                default:                                return RHI_Format::R16G16B16A16_Float;
            }
        }

        VkFormat convert_nrd_format_vk(nrd::Format format)
        {
            switch (format)
            {
                case nrd::Format::R8_UNORM:             return VK_FORMAT_R8_UNORM;
                case nrd::Format::R8_SNORM:             return VK_FORMAT_R8_SNORM;
                case nrd::Format::R8_UINT:              return VK_FORMAT_R8_UINT;
                case nrd::Format::R16_SFLOAT:           return VK_FORMAT_R16_SFLOAT;
                case nrd::Format::R16_UNORM:            return VK_FORMAT_R16_UNORM;
                case nrd::Format::RG8_UNORM:            return VK_FORMAT_R8G8_UNORM;
                case nrd::Format::RG16_SFLOAT:          return VK_FORMAT_R16G16_SFLOAT;
                case nrd::Format::RGBA8_UNORM:          return VK_FORMAT_R8G8B8A8_UNORM;
                case nrd::Format::RGBA16_SFLOAT:        return VK_FORMAT_R16G16B16A16_SFLOAT;
                case nrd::Format::R32_SFLOAT:           return VK_FORMAT_R32_SFLOAT;
                case nrd::Format::RG32_SFLOAT:          return VK_FORMAT_R32G32_SFLOAT;
                case nrd::Format::RGBA32_SFLOAT:        return VK_FORMAT_R32G32B32A32_SFLOAT;
                case nrd::Format::R10_G10_B10_A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                case nrd::Format::R11_G11_B10_UFLOAT:   return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                default:                                return VK_FORMAT_R16G16B16A16_SFLOAT;
            }
        }

        void create_samplers()
        {
            // nearest clamp sampler
            VkSamplerCreateInfo sampler_info = {};
            sampler_info.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.magFilter           = VK_FILTER_NEAREST;
            sampler_info.minFilter           = VK_FILTER_NEAREST;
            sampler_info.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_info.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.maxLod              = VK_LOD_CLAMP_NONE;

            SP_ASSERT_VK(vkCreateSampler(RHI_Context::device, &sampler_info, nullptr, &nrd_sampler_nearest));

            // linear clamp sampler
            sampler_info.magFilter  = VK_FILTER_LINEAR;
            sampler_info.minFilter  = VK_FILTER_LINEAR;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

            SP_ASSERT_VK(vkCreateSampler(RHI_Context::device, &sampler_info, nullptr, &nrd_sampler_linear));
        }

        void create_descriptor_set_layout()
        {
            const nrd::InstanceDesc* instance_desc = nrd::GetInstanceDesc(*nrd_instance);
            if (!instance_desc)
                return;

            // store max resource counts
            nrd_max_textures         = instance_desc->descriptorPoolDesc.perSetTexturesMaxNum;
            nrd_max_storage_textures = instance_desc->descriptorPoolDesc.perSetStorageTexturesMaxNum;

            vector<VkDescriptorSetLayoutBinding> bindings;

            // constant buffer binding (at constantBufferOffset)
            VkDescriptorSetLayoutBinding cb_binding = {};
            cb_binding.binding            = nrd_binding_offsets.constantBufferOffset;
            cb_binding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cb_binding.descriptorCount    = 1;
            cb_binding.stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings.push_back(cb_binding);

            // sampler bindings (at samplerOffset)
            VkSampler immutable_samplers[2] = { nrd_sampler_nearest, nrd_sampler_linear };
            for (uint32_t i = 0; i < 2; i++)
            {
                VkDescriptorSetLayoutBinding sampler_binding = {};
                sampler_binding.binding            = nrd_binding_offsets.samplerOffset + i;
                sampler_binding.descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
                sampler_binding.descriptorCount    = 1;
                sampler_binding.stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
                sampler_binding.pImmutableSamplers = &immutable_samplers[i];
                bindings.push_back(sampler_binding);
            }

            // texture bindings (SRVs at textureOffset)
            for (uint32_t i = 0; i < nrd_max_textures; i++)
            {
                VkDescriptorSetLayoutBinding tex_binding = {};
                tex_binding.binding         = nrd_binding_offsets.textureOffset + i;
                tex_binding.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                tex_binding.descriptorCount = 1;
                tex_binding.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
                bindings.push_back(tex_binding);
            }

            // storage texture bindings (UAVs at storageTextureAndBufferOffset)
            for (uint32_t i = 0; i < nrd_max_storage_textures; i++)
            {
                VkDescriptorSetLayoutBinding storage_binding = {};
                storage_binding.binding         = nrd_binding_offsets.storageTextureAndBufferOffset + i;
                storage_binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                storage_binding.descriptorCount = 1;
                storage_binding.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
                bindings.push_back(storage_binding);
            }

            VkDescriptorSetLayoutCreateInfo layout_info = {};
            layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
            layout_info.pBindings    = bindings.data();

            SP_ASSERT_VK(vkCreateDescriptorSetLayout(RHI_Context::device, &layout_info, nullptr, &nrd_descriptor_set_layout));
        }

        void create_pipeline_layout()
        {
            VkPipelineLayoutCreateInfo layout_info = {};
            layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts    = &nrd_descriptor_set_layout;

            SP_ASSERT_VK(vkCreatePipelineLayout(RHI_Context::device, &layout_info, nullptr, &nrd_pipeline_layout));
        }

        void create_descriptor_pool()
        {
            const nrd::InstanceDesc* instance_desc = nrd::GetInstanceDesc(*nrd_instance);
            if (!instance_desc)
                return;

            uint32_t max_sets = instance_desc->descriptorPoolDesc.setsMaxNum;

            vector<VkDescriptorPoolSize> pool_sizes;
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets });
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, max_sets * 2 });
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_sets * nrd_max_textures });
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_sets * nrd_max_storage_textures });

            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets       = max_sets;
            pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
            pool_info.pPoolSizes    = pool_sizes.data();

            SP_ASSERT_VK(vkCreateDescriptorPool(RHI_Context::device, &pool_info, nullptr, &nrd_descriptor_pool));

            // pre-allocate descriptor sets
            nrd_descriptor_sets.resize(max_sets);
            vector<VkDescriptorSetLayout> layouts(max_sets, nrd_descriptor_set_layout);

            VkDescriptorSetAllocateInfo alloc_info = {};
            alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool     = nrd_descriptor_pool;
            alloc_info.descriptorSetCount = max_sets;
            alloc_info.pSetLayouts        = layouts.data();

            SP_ASSERT_VK(vkAllocateDescriptorSets(RHI_Context::device, &alloc_info, nrd_descriptor_sets.data()));
        }

        void create_constant_buffer()
        {
            const nrd::InstanceDesc* instance_desc = nrd::GetInstanceDesc(*nrd_instance);
            if (!instance_desc)
                return;

            nrd_constant_buffer_size = instance_desc->constantBufferMaxDataSize;

            // create buffer using engine's memory management (VMA)
            RHI_Device::MemoryBufferCreate(
                nrd_constant_buffer,
                nrd_constant_buffer_size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                nullptr,
                "nrd_constant_buffer"
            );

            // map the buffer
            RHI_Device::MemoryMap(nrd_constant_buffer, nrd_constant_mapped);
        }

        void create_compute_pipelines()
        {
            const nrd::InstanceDesc* instance_desc = nrd::GetInstanceDesc(*nrd_instance);
            if (!instance_desc)
                return;

            nrd_shader_modules.resize(instance_desc->pipelinesNum, VK_NULL_HANDLE);
            nrd_compute_pipelines.resize(instance_desc->pipelinesNum, VK_NULL_HANDLE);

            for (uint32_t i = 0; i < instance_desc->pipelinesNum; i++)
            {
                const nrd::PipelineDesc& pipeline_desc = instance_desc->pipelines[i];

                if (!pipeline_desc.computeShaderSPIRV.bytecode || pipeline_desc.computeShaderSPIRV.size == 0)
                    continue;

                // create shader module
                VkShaderModuleCreateInfo module_info = {};
                module_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                module_info.codeSize = static_cast<size_t>(pipeline_desc.computeShaderSPIRV.size);
                module_info.pCode    = reinterpret_cast<const uint32_t*>(pipeline_desc.computeShaderSPIRV.bytecode);

                VkResult result = vkCreateShaderModule(RHI_Context::device, &module_info, nullptr, &nrd_shader_modules[i]);
                if (result != VK_SUCCESS)
                {
                    SP_LOG_ERROR("Failed to create NRD shader module %u", i);
                    continue;
                }

                // create compute pipeline
                VkComputePipelineCreateInfo pipeline_info = {};
                pipeline_info.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                pipeline_info.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                pipeline_info.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
                pipeline_info.stage.module       = nrd_shader_modules[i];
                pipeline_info.stage.pName        = instance_desc->shaderEntryPoint;
                pipeline_info.layout             = nrd_pipeline_layout;

                result = vkCreateComputePipelines(RHI_Context::device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &nrd_compute_pipelines[i]);
                if (result != VK_SUCCESS)
                {
                    SP_LOG_ERROR("Failed to create NRD compute pipeline %u", i);
                }
            }
        }

        void create_resources(uint32_t width, uint32_t height)
        {
            if (!nrd_instance)
                return;

            const nrd::InstanceDesc* instance_desc = nrd::GetInstanceDesc(*nrd_instance);
            if (!instance_desc)
                return;

            uint32_t flags = RHI_Texture_Uav | RHI_Texture_Srv;

            // create permanent pool textures
            nrd_permanent_textures.resize(instance_desc->permanentPoolSize);
            for (uint32_t i = 0; i < instance_desc->permanentPoolSize; i++)
            {
                const nrd::TextureDesc& tex_desc = instance_desc->permanentPool[i];
                uint32_t tex_width  = width  / tex_desc.downsampleFactor;
                uint32_t tex_height = height / tex_desc.downsampleFactor;
                RHI_Format format   = convert_nrd_format(tex_desc.format);
                string name         = "nrd_permanent_" + to_string(i);

                nrd_permanent_textures[i] = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    tex_width, tex_height, 1, 1,
                    format, flags,
                    name.c_str()
                );
            }

            // create transient pool textures
            nrd_transient_textures.resize(instance_desc->transientPoolSize);
            for (uint32_t i = 0; i < instance_desc->transientPoolSize; i++)
            {
                const nrd::TextureDesc& tex_desc = instance_desc->transientPool[i];
                uint32_t tex_width  = width  / tex_desc.downsampleFactor;
                uint32_t tex_height = height / tex_desc.downsampleFactor;
                RHI_Format format   = convert_nrd_format(tex_desc.format);
                string name         = "nrd_transient_" + to_string(i);

                nrd_transient_textures[i] = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    tex_width, tex_height, 1, 1,
                    format, flags,
                    name.c_str()
                );
            }
        }

        void destroy_vulkan_resources()
        {
            vkDeviceWaitIdle(RHI_Context::device);

            // destroy pipelines
            for (auto& pipeline : nrd_compute_pipelines)
            {
                if (pipeline != VK_NULL_HANDLE)
                    vkDestroyPipeline(RHI_Context::device, pipeline, nullptr);
            }
            nrd_compute_pipelines.clear();

            // destroy shader modules
            for (auto& module : nrd_shader_modules)
            {
                if (module != VK_NULL_HANDLE)
                    vkDestroyShaderModule(RHI_Context::device, module, nullptr);
            }
            nrd_shader_modules.clear();

            // destroy constant buffer
            if (nrd_constant_mapped && nrd_constant_buffer)
            {
                RHI_Device::MemoryUnmap(nrd_constant_buffer);
                nrd_constant_mapped = nullptr;
            }
            if (nrd_constant_buffer)
            {
                RHI_Device::MemoryBufferDestroy(nrd_constant_buffer);
                nrd_constant_buffer = nullptr;
            }

            // destroy descriptor pool (this frees all descriptor sets)
            if (nrd_descriptor_pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(RHI_Context::device, nrd_descriptor_pool, nullptr);
                nrd_descriptor_pool = VK_NULL_HANDLE;
            }
            nrd_descriptor_sets.clear();

            // destroy pipeline layout
            if (nrd_pipeline_layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(RHI_Context::device, nrd_pipeline_layout, nullptr);
                nrd_pipeline_layout = VK_NULL_HANDLE;
            }

            // destroy descriptor set layout
            if (nrd_descriptor_set_layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(RHI_Context::device, nrd_descriptor_set_layout, nullptr);
                nrd_descriptor_set_layout = VK_NULL_HANDLE;
            }

            // destroy samplers
            if (nrd_sampler_nearest != VK_NULL_HANDLE)
            {
                vkDestroySampler(RHI_Context::device, nrd_sampler_nearest, nullptr);
                nrd_sampler_nearest = VK_NULL_HANDLE;
            }
            if (nrd_sampler_linear != VK_NULL_HANDLE)
            {
                vkDestroySampler(RHI_Context::device, nrd_sampler_linear, nullptr);
                nrd_sampler_linear = VK_NULL_HANDLE;
            }
        }

        void destroy_resources()
        {
            nrd_transient_textures.clear();
            nrd_permanent_textures.clear();
            destroy_vulkan_resources();
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
            const size_t max_contexts = FFX_FSR3_CONTEXT_COUNT;
            
            VkDeviceContext device_context  = {};
            device_context.vkDevice         = RHI_Context::device;
            device_context.vkPhysicalDevice = RHI_Context::device_physical;
            device_context.vkDeviceProcAddr = vkGetDeviceProcAddr;
            
            const size_t scratch_buffer_size = ffxGetScratchMemorySizeVK(RHI_Context::device_physical, max_contexts);
            void* scratch_buffer             = calloc(1, scratch_buffer_size);
            
            SP_ASSERT(ffxGetInterfaceVK(&amd::ffx_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, max_contexts)== FFX_OK);
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

        // ffx interface
        if (amd::ffx_interface.scratchBuffer != nullptr)
        {
            free(amd::ffx_interface.scratchBuffer);
        }

        // shared
        amd::texture_skybox = nullptr;

        // nrd
        NRD_Shutdown();
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
                // todo: make these mutually exlusive
                if ((resolution_render_changed || resolution_output_changed))
                {
                    amd::upscaler::context_create();
                    intel::context_create();
                }
            }
        }

    #endif
    }

    void RHI_VendorTechnology::ResetHistory()
    {
    #ifdef _WIN32
        common::reset_history = true;
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
    #endif
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
        cmd_list->FlushBarriers();

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

    void RHI_VendorTechnology::NRD_Initialize(uint32_t width, uint32_t height)
    {
    #ifdef _WIN32
        if (nvidia::nrd_initialized)
            return;

        nvidia::nrd_width  = width;
        nvidia::nrd_height = height;

        // get library desc for spirv binding offsets
        const nrd::LibraryDesc* lib_desc = nrd::GetLibraryDesc();
        if (!lib_desc)
        {
            SP_LOG_ERROR("Failed to get NRD library descriptor");
            return;
        }
        nvidia::nrd_binding_offsets = lib_desc->spirvBindingOffsets;

        SP_LOG_INFO("NRD SPIRV binding offsets - sampler: %u, texture: %u, cb: %u, storage: %u",
            nvidia::nrd_binding_offsets.samplerOffset,
            nvidia::nrd_binding_offsets.textureOffset,
            nvidia::nrd_binding_offsets.constantBufferOffset,
            nvidia::nrd_binding_offsets.storageTextureAndBufferOffset);

        // create nrd instance with relax diffuse+specular denoiser (best for restir)
        nrd::DenoiserDesc denoiser_desc = {};
        denoiser_desc.identifier        = 0;
        denoiser_desc.denoiser          = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
        nvidia::nrd_denoiser_identifier = denoiser_desc.identifier;

        nrd::InstanceCreationDesc instance_desc = {};
        instance_desc.denoisers                 = &denoiser_desc;
        instance_desc.denoisersNum              = 1;

        nrd::Result result = nrd::CreateInstance(instance_desc, nvidia::nrd_instance);
        if (result != nrd::Result::SUCCESS)
        {
            SP_LOG_ERROR("Failed to create NRD instance");
            return;
        }

        // create vulkan resources in order
        nvidia::create_samplers();
        nvidia::create_descriptor_set_layout();
        nvidia::create_pipeline_layout();
        nvidia::create_descriptor_pool();
        nvidia::create_constant_buffer();
        nvidia::create_compute_pipelines();
        nvidia::create_resources(width, height);

        nvidia::nrd_initialized = true;

        // register nrd as third party lib
        string nrd_version = to_string(NRD_VERSION_MAJOR) + "." + to_string(NRD_VERSION_MINOR) + "." + to_string(NRD_VERSION_BUILD);
        Settings::RegisterThirdPartyLib("NVIDIA NRD", nrd_version, "https://github.com/NVIDIAGameWorks/RayTracingDenoiser");

        SP_LOG_INFO("NRD initialized with RELAX denoiser for ReSTIR");
    #endif
    }

    void RHI_VendorTechnology::NRD_Shutdown()
    {
    #ifdef _WIN32
        if (!nvidia::nrd_initialized)
            return;

        nvidia::destroy_resources();

        if (nvidia::nrd_instance)
        {
            nrd::DestroyInstance(*nvidia::nrd_instance);
            nvidia::nrd_instance = nullptr;
        }

        nvidia::nrd_initialized = false;
    #endif
    }

    void RHI_VendorTechnology::NRD_Resize(uint32_t width, uint32_t height)
    {
    #ifdef _WIN32
        if (!nvidia::nrd_initialized || (nvidia::nrd_width == width && nvidia::nrd_height == height))
            return;

        nvidia::nrd_width  = width;
        nvidia::nrd_height = height;

        nvidia::destroy_resources();
        nvidia::create_resources(width, height);
    #endif
    }

    void RHI_VendorTechnology::NRD_Denoise(
        RHI_CommandList* cmd_list,
        RHI_Texture* tex_noisy,
        RHI_Texture* tex_output,
        const Matrix& view_matrix,
        const Matrix& projection_matrix,
        const Matrix& view_matrix_prev,
        const Matrix& projection_matrix_prev,
        float jitter_x,
        float jitter_y,
        float jitter_prev_x,
        float jitter_prev_y,
        float time_delta_ms,
        uint32_t frame_index
    )
    {
    #ifdef _WIN32
        if (!nvidia::nrd_initialized || !nvidia::nrd_instance || !cmd_list)
            return;

        if (nvidia::nrd_compute_pipelines.empty() || nvidia::nrd_descriptor_sets.empty())
            return;

        VkCommandBuffer vk_cmd = static_cast<VkCommandBuffer>(cmd_list->GetRhiResource());

        // update nrd common settings
        nrd::CommonSettings settings = {};

        // nrd expects row-major matrices, engine stores column-major, so transpose
        // also nrd is left-handed like the engine, so no handedness conversion needed
        Matrix view_transposed      = view_matrix.Transposed();
        Matrix view_prev_transposed = view_matrix_prev.Transposed();
        Matrix proj_transposed      = projection_matrix.Transposed();
        Matrix proj_prev_transposed = projection_matrix_prev.Transposed();

        memcpy(settings.worldToViewMatrix,      view_transposed.Data(),      sizeof(float) * 16);
        memcpy(settings.worldToViewMatrixPrev,  view_prev_transposed.Data(), sizeof(float) * 16);
        memcpy(settings.viewToClipMatrix,       proj_transposed.Data(),      sizeof(float) * 16);
        memcpy(settings.viewToClipMatrixPrev,   proj_prev_transposed.Data(), sizeof(float) * 16);

        // motion vector scale (screen space uvs)
        settings.motionVectorScale[0] = 1.0f;
        settings.motionVectorScale[1] = 1.0f;
        settings.motionVectorScale[2] = 0.0f;

        // jitter
        settings.cameraJitter[0]     = jitter_x;
        settings.cameraJitter[1]     = jitter_y;
        settings.cameraJitterPrev[0] = jitter_prev_x;
        settings.cameraJitterPrev[1] = jitter_prev_y;

        // resolution
        settings.resourceSize[0]     = static_cast<uint16_t>(nvidia::nrd_width);
        settings.resourceSize[1]     = static_cast<uint16_t>(nvidia::nrd_height);
        settings.resourceSizePrev[0] = static_cast<uint16_t>(nvidia::nrd_width);
        settings.resourceSizePrev[1] = static_cast<uint16_t>(nvidia::nrd_height);
        settings.rectSize[0]         = static_cast<uint16_t>(nvidia::nrd_width);
        settings.rectSize[1]         = static_cast<uint16_t>(nvidia::nrd_height);
        settings.rectSizePrev[0]     = static_cast<uint16_t>(nvidia::nrd_width);
        settings.rectSizePrev[1]     = static_cast<uint16_t>(nvidia::nrd_height);

        // other settings
        settings.timeDeltaBetweenFrames = time_delta_ms;
        settings.denoisingRange         = 500000.0f;
        settings.disocclusionThreshold  = 0.01f;
        settings.frameIndex             = frame_index;
        settings.accumulationMode       = nrd::AccumulationMode::CONTINUE;
        settings.isMotionVectorInWorldSpace = false;

        nrd::SetCommonSettings(*nvidia::nrd_instance, settings);

        // set relax-specific settings optimized for restir
        nrd::RelaxSettings relax_settings = {};
        relax_settings.diffuseMaxAccumulatedFrameNum      = 30;
        relax_settings.specularMaxAccumulatedFrameNum     = 30;
        relax_settings.diffuseMaxFastAccumulatedFrameNum  = 6;
        relax_settings.specularMaxFastAccumulatedFrameNum = 6;
        relax_settings.historyFixFrameNum                 = 3;
        relax_settings.diffusePrepassBlurRadius           = 30.0f;
        relax_settings.specularPrepassBlurRadius          = 50.0f;
        relax_settings.atrousIterationNum                 = 5;
        relax_settings.enableAntiFirefly                  = true;
        relax_settings.hitDistanceReconstructionMode      = nrd::HitDistanceReconstructionMode::AREA_3X3;
        
        nrd::SetDenoiserSettings(*nvidia::nrd_instance, nvidia::nrd_denoiser_identifier, &relax_settings);

        // get dispatches from nrd
        const nrd::DispatchDesc* dispatch_descs = nullptr;
        uint32_t dispatch_count = 0;
        
        nrd::Result result = nrd::GetComputeDispatches(
            *nvidia::nrd_instance,
            &nvidia::nrd_denoiser_identifier, 1,
            dispatch_descs, dispatch_count
        );

        if (result != nrd::Result::SUCCESS || dispatch_count == 0 || nvidia::nrd_compute_pipelines.empty())
        {
            SP_LOG_WARNING("NRD dispatch failed - result: %d, dispatch_count: %u, pipelines: %zu",
                static_cast<int>(result), dispatch_count, nvidia::nrd_compute_pipelines.size());
            // fallback: copy noisy diffuse input directly to output
            RHI_Texture* diffuse_in = Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_diff_radiance_hitdist);
            if (diffuse_in && tex_output)
            {
                cmd_list->Blit(diffuse_in, tex_output, false);
            }
            return;
        }

        // helper to get texture for nrd resource type
        auto get_texture_for_resource = [&](nrd::ResourceType type, uint16_t index_in_pool) -> RHI_Texture*
        {
            switch (type)
            {
                case nrd::ResourceType::IN_MV:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::gbuffer_velocity);
                case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_normal_roughness);
                case nrd::ResourceType::IN_VIEWZ:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_viewz);
                case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_diff_radiance_hitdist);
                case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_spec_radiance_hitdist);
                case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_out_diff_radiance_hitdist);
                case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
                    return Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_out_spec_radiance_hitdist);
                case nrd::ResourceType::TRANSIENT_POOL:
                    if (index_in_pool < nvidia::nrd_transient_textures.size())
                        return nvidia::nrd_transient_textures[index_in_pool].get();
                    break;
                case nrd::ResourceType::PERMANENT_POOL:
                    if (index_in_pool < nvidia::nrd_permanent_textures.size())
                        return nvidia::nrd_permanent_textures[index_in_pool].get();
                    break;
                default:
                    break;
            }
            return nullptr;
        };

        // pre-pass: transition pool textures and motion vectors to GENERAL layout
        // nrd input textures (viewz, normal_roughness, radiance) are already in GENERAL from Renderer_Passes.cpp
        {
            set<void*> transitioned_images;
            vector<VkImageMemoryBarrier> pre_barriers;

            for (uint32_t dispatch_idx = 0; dispatch_idx < dispatch_count; dispatch_idx++)
            {
                const nrd::DispatchDesc& dispatch = dispatch_descs[dispatch_idx];
                for (uint32_t r = 0; r < dispatch.resourcesNum; r++)
                {
                    const nrd::ResourceDesc& res = dispatch.resources[r];
                    RHI_Texture* texture = get_texture_for_resource(res.type, res.indexInPool);
                    if (!texture)
                        continue;

                    // skip textures we know are already in GENERAL (nrd inputs from nrd_prepare)
                    bool is_nrd_input = (res.type == nrd::ResourceType::IN_VIEWZ ||
                                        res.type == nrd::ResourceType::IN_NORMAL_ROUGHNESS ||
                                        res.type == nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST ||
                                        res.type == nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST);
                    if (is_nrd_input)
                        continue;

                    VkImage image = static_cast<VkImage>(texture->GetRhiResource());
                    if (transitioned_images.find(image) != transitioned_images.end())
                        continue; // already handled

                    transitioned_images.insert(image);

                    // for pool textures and outputs, use UNDEFINED as old layout (first use or don't care about contents)
                    // for motion vectors, use SHADER_READ_ONLY_OPTIMAL (likely layout from G-buffer pass)
                    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    if (res.type == nrd::ResourceType::IN_MV)
                    {
                        old_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }

                    VkImageMemoryBarrier barrier = {};
                    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.oldLayout                       = old_layout;
                    barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image                           = image;
                    barrier.subresourceRange.aspectMask     = texture->IsDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel   = 0;
                    barrier.subresourceRange.levelCount     = texture->GetMipCount();
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount     = 1;

                    pre_barriers.push_back(barrier);
                }
            }

            if (!pre_barriers.empty())
            {
                vkCmdPipelineBarrier(vk_cmd,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr,
                    static_cast<uint32_t>(pre_barriers.size()), pre_barriers.data());
            }
        }

        // execute each dispatch using custom vulkan pipeline
        for (uint32_t dispatch_idx = 0; dispatch_idx < dispatch_count; dispatch_idx++)
        {
            const nrd::DispatchDesc& dispatch = dispatch_descs[dispatch_idx];
            
            if (dispatch.pipelineIndex >= nvidia::nrd_compute_pipelines.size())
                continue;

            VkPipeline pipeline = nvidia::nrd_compute_pipelines[dispatch.pipelineIndex];
            if (pipeline == VK_NULL_HANDLE)
                continue;

            // use a descriptor set for this dispatch (cycling through available sets)
            uint32_t set_idx = dispatch_idx % static_cast<uint32_t>(nvidia::nrd_descriptor_sets.size());
            VkDescriptorSet descriptor_set = nvidia::nrd_descriptor_sets[set_idx];

            // update constant buffer if needed
            if (dispatch.constantBufferData && dispatch.constantBufferDataSize > 0 && !dispatch.constantBufferDataMatchesPreviousDispatch)
            {
                memcpy(nvidia::nrd_constant_mapped, dispatch.constantBufferData, dispatch.constantBufferDataSize);
            }

            // transition all resources to GENERAL layout (compatible with both sampled and storage access)
            // use memory barriers for coherency between dispatches
            vector<VkImageMemoryBarrier> image_barriers;
            for (uint32_t r = 0; r < dispatch.resourcesNum; r++)
            {
                const nrd::ResourceDesc& res = dispatch.resources[r];
                RHI_Texture* texture = get_texture_for_resource(res.type, res.indexInPool);
                
                if (!texture)
                    continue;

                VkImageMemoryBarrier barrier = {};
                barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = static_cast<VkImage>(texture->GetRhiResource());
                barrier.subresourceRange.aspectMask     = texture->IsDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;

                image_barriers.push_back(barrier);
            }

            if (!image_barriers.empty())
            {
                vkCmdPipelineBarrier(vk_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(image_barriers.size()), image_barriers.data());
            }

            // build descriptor writes
            vector<VkDescriptorImageInfo> image_infos;
            vector<VkWriteDescriptorSet> writes;
            image_infos.reserve(dispatch.resourcesNum);

            // constant buffer write
            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = static_cast<VkBuffer>(nvidia::nrd_constant_buffer);
            buffer_info.offset = 0;
            buffer_info.range  = nvidia::nrd_constant_buffer_size;

            VkWriteDescriptorSet cb_write = {};
            cb_write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cb_write.dstSet          = descriptor_set;
            cb_write.dstBinding      = nvidia::nrd_binding_offsets.constantBufferOffset;
            cb_write.descriptorCount = 1;
            cb_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cb_write.pBufferInfo     = &buffer_info;
            writes.push_back(cb_write);

            // resource writes
            uint32_t texture_index  = 0;
            uint32_t storage_index  = 0;
            
            for (uint32_t r = 0; r < dispatch.resourcesNum; r++)
            {
                const nrd::ResourceDesc& res = dispatch.resources[r];
                RHI_Texture* texture = get_texture_for_resource(res.type, res.indexInPool);
                
                if (!texture)
                    continue;

                VkDescriptorImageInfo img_info = {};
                img_info.sampler     = VK_NULL_HANDLE;
                img_info.imageView   = static_cast<VkImageView>(texture->GetRhiSrv());
                img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                image_infos.push_back(img_info);

                VkWriteDescriptorSet write = {};
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = descriptor_set;
                write.descriptorCount = 1;
                write.pImageInfo      = &image_infos.back();

                if (res.descriptorType == nrd::DescriptorType::TEXTURE)
                {
                    write.dstBinding     = nvidia::nrd_binding_offsets.textureOffset + texture_index;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    texture_index++;
                }
                else
                {
                    write.dstBinding     = nvidia::nrd_binding_offsets.storageTextureAndBufferOffset + storage_index;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    storage_index++;
                }

                writes.push_back(write);
            }

            // update descriptors
            if (!writes.empty())
            {
                vkUpdateDescriptorSets(RHI_Context::device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }

            // bind pipeline and descriptor set
            vkCmdBindPipeline(vk_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(vk_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, nvidia::nrd_pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

            // dispatch
            vkCmdDispatch(vk_cmd, dispatch.gridWidth, dispatch.gridHeight, 1);

            // memory barrier between dispatches
            VkMemoryBarrier barrier = {};
            barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(vk_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // copy denoised diffuse result to output
        RHI_Texture* denoised = Renderer::GetRenderTarget(Renderer_RenderTarget::nrd_out_diff_radiance_hitdist);
        if (denoised && tex_output)
        {
            // transition output texture for reading
            cmd_list->InsertBarrier(denoised, RHI_Image_Layout::General);
            cmd_list->Blit(denoised, tex_output, false);
        }
    #endif
    }

    bool RHI_VendorTechnology::NRD_IsAvailable()
    {
    #ifdef _WIN32
        return nvidia::nrd_initialized && nvidia::nrd_instance != nullptr;
    #else
        return false;
    #endif
    }
}
