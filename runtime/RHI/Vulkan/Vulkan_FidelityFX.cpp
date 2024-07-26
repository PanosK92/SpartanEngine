/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../RHI_FidelityFX.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Texture.h"
#include "../RHI_Texture2D.h"
#include "../RHI_Texture3D.h"
#include "../RHI_TextureCube.h"
#include "../RHI_Device.h"
#include "../RHI_Buffer.h"
#include "../Rendering/Renderer_Buffers.h"
#include "../World/Components/Renderable.h"
#include "../../World/Components/Camera.h"
#include "../World/Entity.h"
SP_WARNINGS_OFF
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_sssr.h>
#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>
#include "../RHI_GeometryBuffer.h"
SP_WARNINGS_ON
//=============================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    namespace
    {
        void ffx_message_callback(FfxMsgType type, const wchar_t* message)
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

        FfxSurfaceFormat to_ffx_surface_format(const RHI_Format format)
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
            case RHI_Format::D32_Float:
                return FFX_SURFACE_FORMAT_R32_FLOAT;
            case RHI_Format::Max:
                return FFX_SURFACE_FORMAT_UNKNOWN;
            default:
                SP_ASSERT_MSG(false, "Unsupported format");
                return FFX_SURFACE_FORMAT_UNKNOWN;
            }
        }

        FfxResourceStates to_ffx_resource_state(RHI_Image_Layout layout)
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

        FfxResource to_ffx_resource(RHI_Texture* texture, RHI_Buffer* buffer, RHI_GeometryBuffer* buffer_geometry, const wchar_t* name)
        {
            void* resource                     = nullptr;
            FfxResourceDescription description = {};
            FfxResourceStates state            = FFX_RESOURCE_STATE_COMMON;

            if (texture)
            {
                resource = texture->GetRhiResource();
                state = to_ffx_resource_state(texture->GetLayout(0));

                // usage
                uint32_t usage = FFX_RESOURCE_USAGE_READ_ONLY;
                if (texture->IsDepthFormat())
                    usage |= FFX_RESOURCE_USAGE_DEPTHTARGET;
                if (texture->IsUav())
                    usage |= FFX_RESOURCE_USAGE_UAV;
                if (texture->GetResourceType() == ResourceType::Texture2dArray || texture->GetResourceType() == ResourceType::TextureCube)
                    usage |= FFX_RESOURCE_USAGE_ARRAYVIEW;
                if (texture->IsRtv())
                    usage |= FFX_RESOURCE_USAGE_RENDERTARGET;

                // description
                switch (texture->GetResourceType())
                {
                case ResourceType::Texture2d:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
                    break;
                case ResourceType::Texture3d:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE3D;
                    break;
                case ResourceType::TextureCube:
                    description.type = FFX_RESOURCE_TYPE_TEXTURE_CUBE;
                    break;
                default:
                    SP_ASSERT_MSG(false, "Unsupported texture type");
                    break;
                }

                description.width    = texture->GetWidth();
                description.height   = texture->GetHeight();
                description.depth    = texture->GetDepth();
                description.mipCount = texture->GetMipCount();
                description.format   = to_ffx_surface_format(texture->GetFormat());
                description.flags    = FfxResourceFlags::FFX_RESOURCE_FLAGS_NONE;
                description.usage    = static_cast<FfxResourceUsage>(usage);
            }
            else if (buffer)
            {
                description.type   = FFX_RESOURCE_TYPE_BUFFER;
                description.usage  = FFX_RESOURCE_USAGE_UAV;
                description.size   = static_cast<uint32_t>(buffer->GetObjectSize());
                description.stride = buffer->GetStride();
                resource           = buffer->GetRhiResource();
                state              = FFX_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            else
            {
                description.type   = FFX_RESOURCE_TYPE_BUFFER;
                description.usage  = FFX_RESOURCE_USAGE_UAV;
                description.size   = static_cast<uint32_t>(buffer_geometry->GetObjectSize());
                description.stride = buffer_geometry->GetStride();
                resource           = buffer_geometry->GetRhiResource();
                state              = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
            }

            return ffxGetResourceVK(
                resource,
                description,
                const_cast<wchar_t*>(name),
                state
            );
        }

        void set_ffx_float3(FfxFloat32x3& dest, const Vector3& source)
        {
            dest[0] = source.x;
            dest[1] = source.y;
            dest[2] = source.z;
        }

        void set_ffx_float16(float* ffx_matrix, const Matrix& matrix)
        {
            const float* data = matrix.Data();
            memcpy(ffx_matrix, data, sizeof(Matrix));
        }

        void set_ffx_float16(float* ffx_matrix_destination, float* ffx_matrix_source)
        {
            memcpy(ffx_matrix_destination, ffx_matrix_source, sizeof(Matrix));
        }

        FfxInterface ffx_interface = {};

        namespace fsr3
        {
            uint32_t jitter_index = 0;

            bool                                       context_created           = false;
            FfxFsr3UpscalerContext                     context                   = {};
            FfxFsr3UpscalerContextDescription          description_context       = {};
            FfxFsr3UpscalerDispatchDescription         description_dispatch      = {};
            FfxFsr3UpscalerGenerateReactiveDescription description_reactive_mask = {};
        }

        namespace sssr
        {
            bool                       context_created      = false;
            FfxSssrContext             context              = {};
            FfxSssrContextDescription  description_context  = {};
            FfxSssrDispatchDescription description_dispatch = {};
            shared_ptr<RHI_Texture>    cubemap              = nullptr;
        }

        namespace brixelizer_gi
        {
            // parameters
            const float    mesh_unit_size           = 0.2f;
            const float    cascade_size_ratio       = 2.0f;
            const uint32_t cascade_max              = 24;
            const uint32_t cascade_count            = cascade_max / 3;
            const uint32_t bricks_max               = 1 << 18;
            const uint32_t brick_aabbs_stride       = sizeof(uint32_t);
            const uint32_t brick_aabbs_size         = bricks_max * brick_aabbs_stride;
            const uint32_t cascade_resolution       = 64;
            const uint32_t cascade_aabb_tree_size   = (16 * 16 * 16) * sizeof(uint32_t) + (4 * 4 * 4 + 1) * sizeof(Vector3) * 2;
            const uint32_t cascade_aabb_tree_stride = sizeof(uint32_t);
            const uint32_t cascade_brick_map_size   = cascade_resolution * cascade_resolution * cascade_resolution * sizeof(uint32_t);
            const uint32_t cascade_brick_map_stride = sizeof(uint32_t);
            const uint32_t sdf_atlas_size           = 512;
            Vector3        sdf_center               = Vector3::Zero;
            const bool     sdf_follow_camera        = true;

            // structs & resources
            bool                                context_created                       = false;
            FfxBrixelizerContext                context                               = {};
            FfxBrixelizerContextDescription     description_context                   = {};
            FfxBrixelizerUpdateDescription      description_update                    = {};
            FfxBrixelizerBakedUpdateDescription description_update_baked              = {};
            FfxBrixelizerGIContext              context_gi                            = {};
            FfxBrixelizerGIContextDescription   description_context_gi                = {};
            FfxBrixelizerGIDispatchDescription  description_dispatch_gi               = {};
            shared_ptr<RHI_Texture>             texture_sdf_atlas                     = nullptr;
            shared_ptr<RHI_Texture>             texture_depth_previous                = nullptr;
            shared_ptr<RHI_Texture>             texture_normal_previous               = nullptr;
            shared_ptr<RHI_Buffer>              buffer_brick_aabbs                    = nullptr;
            array<shared_ptr<RHI_Buffer>,       cascade_max> buffer_cascade_aabb_tree = {};
            array<shared_ptr<RHI_Buffer>,       cascade_max> buffer_cascade_brick_map = {};
            shared_ptr<RHI_Buffer>              buffer_scratch                        = nullptr;
            vector<FfxBrixelizerInstanceDescription> instances;
            vector<pair<const RHI_GeometryBuffer*, uint32_t>> buffer_indices;

            // debug visualization (which overwrites the diffuse gi output texture)
            enum class DebugMode
            {
                Off,       // brixelizer
                Distance,  // brixelizer
                UVW,       // brixelizer
                Iterations,// brixelizer
                Gradient,  // brixelizer
                BrickID,   // brixelizer
                CascadeID, // brixelizer
                Radiance,  // gi
                Irradiance // gi
            };
            DebugMode debug_mode                                            = DebugMode::Radiance;
            FfxBrixelizerDebugVisualizationDescription debug_description    = {};
            FfxBrixelizerGIDebugDescription            debug_description_gi = {};

            FfxBrixelizerTraceDebugModes to_ffx_debug_mode(const DebugMode debug_mode)
            {
                if (debug_mode == brixelizer_gi::DebugMode::Distance)   return FFX_BRIXELIZER_TRACE_DEBUG_MODE_DISTANCE;
                if (debug_mode == brixelizer_gi::DebugMode::UVW)        return FFX_BRIXELIZER_TRACE_DEBUG_MODE_UVW;
                if (debug_mode == brixelizer_gi::DebugMode::Iterations) return FFX_BRIXELIZER_TRACE_DEBUG_MODE_ITERATIONS;
                if (debug_mode == brixelizer_gi::DebugMode::Gradient)   return FFX_BRIXELIZER_TRACE_DEBUG_MODE_GRAD;
                if (debug_mode == brixelizer_gi::DebugMode::BrickID)    return FFX_BRIXELIZER_TRACE_DEBUG_MODE_BRICK_ID;
                if (debug_mode == brixelizer_gi::DebugMode::CascadeID)  return FFX_BRIXELIZER_TRACE_DEBUG_MODE_CASCADE_ID;

                return FFX_BRIXELIZER_TRACE_DEBUG_MODE_DISTANCE;
            }
        }
    }

    void RHI_FidelityFX::Initialize()
    {
        // ffx interface
        {
            // all used contexts need to be accounted for here
            const size_t countext_count =
                FFX_FSR3_CONTEXT_COUNT       +
                FFX_SSSR_CONTEXT_COUNT       +
                FFX_BRIXELIZER_CONTEXT_COUNT +
                FFX_BRIXELIZER_GI_CONTEXT_COUNT;
            
            VkDeviceContext device_context  = {};
            device_context.vkDevice         = RHI_Context::device;
            device_context.vkPhysicalDevice = RHI_Context::device_physical;
            device_context.vkDeviceProcAddr = vkGetDeviceProcAddr;
            
            const size_t scratch_buffer_size = ffxGetScratchMemorySizeVK(RHI_Context::device_physical, countext_count);
            void* scratch_buffer             = calloc(1, scratch_buffer_size);
            
            FfxErrorCode error_code = ffxGetInterfaceVK(&ffx_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, countext_count);
            SP_ASSERT(error_code == FFX_OK);
        }

        // assets
        {
            // sssr
            {
                sssr::cubemap = make_unique<RHI_TextureCube>(1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Srv, "ffx_environment");
            }

            // brixelizer gi
            {
                // scratch buffer
                brixelizer_gi::buffer_scratch = make_shared<RHI_Buffer>(
                    1 << 30, // 1024 MB (will grow if needed)
                    1,
                    RHI_Buffer_Transfer_Src | RHI_Buffer_Transfer_Dst,
                    "ffx_brixelizer_gi_scratch"
                );

                // sdf atlas texture
                brixelizer_gi::texture_sdf_atlas = make_unique<RHI_Texture3D>(
                    brixelizer_gi::sdf_atlas_size,
                    brixelizer_gi::sdf_atlas_size,
                    brixelizer_gi::sdf_atlas_size,
                    RHI_Format::R8_Unorm,
                    RHI_Texture_Srv | RHI_Texture_Uav,
                    "ffx_sdf_atlas"
                );

                // brick aabbs buffer
                brixelizer_gi::buffer_brick_aabbs = make_shared<RHI_Buffer>(
                    brixelizer_gi::brick_aabbs_stride,
                    brixelizer_gi::bricks_max,
                    0,
                    "ffx_brick_aabbs"
                );

                // cascade aabb trees
                for (uint32_t i = 0; i < brixelizer_gi::cascade_max; ++i)
                {
                    string name = "ffx_cascade_aabb_tree_" + to_string(i);
                    brixelizer_gi::buffer_cascade_aabb_tree[i] = make_shared<RHI_Buffer>(
                        brixelizer_gi::cascade_aabb_tree_stride,
                        brixelizer_gi::cascade_aabb_tree_size / brixelizer_gi::cascade_aabb_tree_stride,
                        0,
                        name.c_str()
                    );
                }

                // cascade brick maps
                for (uint32_t i = 0; i < brixelizer_gi::cascade_max; ++i)
                {
                    string name = "ffx_cascade_brick_map_" + to_string(i);
                    brixelizer_gi::buffer_cascade_brick_map[i] = make_shared<RHI_Buffer>(
                        brixelizer_gi::cascade_brick_map_stride,
                        brixelizer_gi::cascade_brick_map_size / brixelizer_gi::cascade_brick_map_stride,
                        0,
                        name.c_str()
                    );
                }
            }
        }
    }

    void RHI_FidelityFX::DestroyContexts()
    {
        // brixelizer gi
        if (brixelizer_gi::context_created)
        {
            SP_ASSERT(ffxBrixelizerContextDestroy(&brixelizer_gi::context) == FFX_OK);
            SP_ASSERT(ffxBrixelizerGIContextDestroy(&brixelizer_gi::context_gi) == FFX_OK);
            brixelizer_gi::context_created = false;
        }

        // sssr
        if (sssr::context_created)
        {
            SP_ASSERT(ffxSssrContextDestroy(&sssr::context) == FFX_OK);
            sssr::context_created = false;
        }

        // fsr 3
        if (fsr3::context_created)
        {
            SP_ASSERT(ffxFsr3UpscalerContextDestroy(&fsr3::context) == FFX_OK);
            fsr3::context_created = false;
        }
    }

    void RHI_FidelityFX::Shutdown()
    {
        sssr::cubemap                          = nullptr;
        brixelizer_gi::texture_sdf_atlas       = nullptr;
        brixelizer_gi::buffer_brick_aabbs      = nullptr;
        brixelizer_gi::buffer_scratch          = nullptr;
        brixelizer_gi::texture_depth_previous  = nullptr;
        brixelizer_gi::texture_normal_previous = nullptr;
        brixelizer_gi::buffer_cascade_aabb_tree.fill(nullptr);
        brixelizer_gi::buffer_cascade_brick_map.fill(nullptr);

        DestroyContexts();

        // ffx interface
        if (ffx_interface.scratchBuffer != nullptr)
        {
            free(ffx_interface.scratchBuffer);
        }
    }

    void RHI_FidelityFX::Resize(const Vector2& resolution_render, const Vector2& resolution_output)
    {
        // contexts are resolution dependent, so we destroy and (re)create them when resizing
        DestroyContexts();

        uint32_t width  = static_cast<uint32_t>(resolution_render.x);
        uint32_t height = static_cast<uint32_t>(resolution_render.y);

        // fs3
        if (!fsr3::context_created)
        {
            // description
            fsr3::description_context.maxRenderSize.width    = width;
            fsr3::description_context.maxRenderSize.height   = height;
            fsr3::description_context.maxUpscaleSize.width   = static_cast<uint32_t>(resolution_output.x);
            fsr3::description_context.maxUpscaleSize.height  = static_cast<uint32_t>(resolution_output.y);
            fsr3::description_context.flags                  = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
            fsr3::description_context.flags                 |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE; // hdr input
            #ifdef DEBUG
            fsr3::description_context.flags                 |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
            fsr3::description_context.fpMessage              = &ffx_message_callback;
            #endif
            fsr3::description_context.backendInterface       = ffx_interface;

            // context
            SP_ASSERT(ffxFsr3UpscalerContextCreate(&fsr3::context, &fsr3::description_context) == FFX_OK);
            fsr3::context_created = true;

            // reset jitter index
            fsr3::jitter_index = 0;
        }

        // sssr
        if (!sssr::context_created)
        {
             sssr::description_context.renderSize.width           = width;
             sssr::description_context.renderSize.height          = height;
             sssr::description_context.normalsHistoryBufferFormat = to_ffx_surface_format(RHI_Format::R16G16B16A16_Float);
             sssr::description_context.flags                      = FFX_SSSR_ENABLE_DEPTH_INVERTED;
             sssr::description_context.backendInterface           = ffx_interface;

             SP_ASSERT(ffxSssrContextCreate(&sssr::context, &sssr::description_context) == FFX_OK);
             sssr::context_created = true;
        }

        // brixelizer gi
        if (!brixelizer_gi::context_created)
        {
            // context
            {
                // sdf
                brixelizer_gi::description_context.sdfCenter[0] = 0.0f;
                brixelizer_gi::description_context.sdfCenter[1] = 0.0f;
                brixelizer_gi::description_context.sdfCenter[2] = 0.0f;

                // cascades
                brixelizer_gi::description_context.numCascades = brixelizer_gi::cascade_count;
                float voxel_size = brixelizer_gi::mesh_unit_size;
                for (uint32_t i = 0; i < brixelizer_gi::description_context.numCascades; ++i)
                {
                    FfxBrixelizerCascadeDescription* cascadeDesc  = &brixelizer_gi::description_context.cascadeDescs[i];
                    cascadeDesc->flags                            = (FfxBrixelizerCascadeFlag)(FFX_BRIXELIZER_CASCADE_STATIC | FFX_BRIXELIZER_CASCADE_DYNAMIC);
                    cascadeDesc->voxelSize                        = brixelizer_gi::mesh_unit_size;
                    voxel_size                                   *= brixelizer_gi::cascade_size_ratio;
                }

                // misc
                #ifdef DEBUG
                brixelizer_gi::description_context.flags = FFX_BRIXELIZER_CONTEXT_FLAG_ALL_DEBUG;
                #endif
                brixelizer_gi::description_context.backendInterface = ffx_interface;

                SP_ASSERT(ffxBrixelizerContextCreate(&brixelizer_gi::description_context, &brixelizer_gi::context) == FFX_OK);
            }

            // context gi
            {
                brixelizer_gi::description_context_gi.internalResolution = FfxBrixelizerGIInternalResolution::FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE;
                brixelizer_gi::description_context_gi.displaySize.width  = static_cast<uint32_t>(resolution_render.x);
                brixelizer_gi::description_context_gi.displaySize.height = static_cast<uint32_t>(resolution_render.y);
                brixelizer_gi::description_context_gi.flags              = FfxBrixelizerGIFlags::FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED;
                brixelizer_gi::description_context_gi.backendInterface   = ffx_interface;
                
                SP_ASSERT(ffxBrixelizerGIContextCreate(&brixelizer_gi::context_gi, &brixelizer_gi::description_context_gi) == FFX_OK);
            }
            
            // resources
            {
                uint32_t flags = RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit;
                brixelizer_gi::texture_depth_previous  = make_shared<RHI_Texture2D>(width, height, 1, RHI_Format::D32_Float,          flags, "ffx_depth_previous");
                brixelizer_gi::texture_normal_previous = make_shared<RHI_Texture2D>(width, height, 1, RHI_Format::R16G16B16A16_Float, flags, "ffx_normal_previous");
            }

            brixelizer_gi::context_created = true;
        }
    }

    void RHI_FidelityFX::FSR3_ResetHistory()
    {
        fsr3::description_dispatch.reset = true;
    }

    void RHI_FidelityFX::FSR3_GenerateJitterSample(float* x, float* y)
    {
        // get jitter phase count
        const uint32_t resolution_render_x = static_cast<uint32_t>(fsr3::description_context.maxRenderSize.width);
        const uint32_t resolution_render_y = static_cast<uint32_t>(fsr3::description_context.maxRenderSize.height);
        const int32_t jitter_phase_count   = ffxFsr3GetJitterPhaseCount(resolution_render_x, resolution_render_x);

        // ensure fsr3_jitter_index is properly wrapped around the jitter_phase_count
        fsr3::jitter_index = (fsr3::jitter_index + 1) % jitter_phase_count;

        // generate jitter sample
        FfxErrorCode result = ffxFsr3GetJitterOffset(&fsr3::description_dispatch.jitterOffset.x, &fsr3::description_dispatch.jitterOffset.y, fsr3::jitter_index, jitter_phase_count);
        SP_ASSERT(result == FFX_OK);

        // adjust the jitter offset for the projection matrix, based on the function comments
        *x =  2.0f * fsr3::description_dispatch.jitterOffset.x / resolution_render_x;
        *y = -2.0f * fsr3::description_dispatch.jitterOffset.y / resolution_render_y;
    }

    void RHI_FidelityFX::FSR3_Dispatch
    (
        RHI_CommandList* cmd_list,
        Camera* camera,
        const float delta_time_sec,
        const float sharpness,
        const float exposure,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_color_opaque,
        RHI_Texture* tex_reactive,
        RHI_Texture* tex_output
    )
    {
        // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/super-resolution-upscaler.md
        // requires:      VK_KHR_get_memory_requirements2

        // output is displayed in the viewport via imgui so we add a barrier to ensure FSR has done whatever it needs to do
        cmd_list->InsertBarrierTextureReadWrite(tex_output);
        cmd_list->InsertPendingBarrierGroup();

        // generate reactive mask
        {
            // set resources
            fsr3::description_reactive_mask.commandList       = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
            fsr3::description_reactive_mask.colorOpaqueOnly   = to_ffx_resource(tex_color_opaque, nullptr, nullptr, L"fsr3_color_opaque");
            fsr3::description_reactive_mask.colorPreUpscale   = to_ffx_resource(tex_color,        nullptr, nullptr, L"fsr3_color");
            fsr3::description_reactive_mask.outReactive       = to_ffx_resource(tex_reactive,     nullptr, nullptr, L"fsr3_reactive");

            // configure
            fsr3::description_reactive_mask.renderSize.width  = static_cast<uint32_t>(tex_velocity->GetWidth() * resolution_scale);
            fsr3::description_reactive_mask.renderSize.height = static_cast<uint32_t>(tex_velocity->GetHeight() * resolution_scale);
            fsr3::description_reactive_mask.scale             = 1.0f; // global multiplier for reactivity, higher values increase overall reactivity
            fsr3::description_reactive_mask.cutoffThreshold   = 0.8f; // difference threshold, lower values make more pixels reactive
            fsr3::description_reactive_mask.binaryValue       = 1.0f; // value assigned to reactive pixels in the mask (typically 1.0)
            fsr3::description_reactive_mask.flags             = 0;

            // dispatch
            SP_ASSERT(ffxFsr3UpscalerContextGenerateReactiveMask(&fsr3::context, &fsr3::description_reactive_mask) == FFX_OK);
        }

        // upscale
        {
            // set resources
            fsr3::description_dispatch.commandList   = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
            fsr3::description_dispatch.color         = to_ffx_resource(tex_color,    nullptr, nullptr, L"fsr3_color");
            fsr3::description_dispatch.depth         = to_ffx_resource(tex_depth,    nullptr, nullptr, L"fsr3_depth");
            fsr3::description_dispatch.motionVectors = to_ffx_resource(tex_velocity, nullptr, nullptr, L"fsr3_velocity");
            fsr3::description_dispatch.reactive      = to_ffx_resource(tex_reactive, nullptr, nullptr, L"fsr3_reactive");
            fsr3::description_dispatch.output        = to_ffx_resource(tex_output,   nullptr, nullptr, L"fsr3_output");

            // configure
            fsr3::description_dispatch.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth());
            fsr3::description_dispatch.motionVectorScale.y    = -static_cast<float>(tex_velocity->GetHeight());
            fsr3::description_dispatch.enableSharpening       = sharpness != 0.0f;
            fsr3::description_dispatch.sharpness              = sharpness;
            fsr3::description_dispatch.frameTimeDelta         = delta_time_sec * 1000.0f;                         // seconds to milliseconds
            fsr3::description_dispatch.preExposure            = exposure;                                         // the exposure value if not using FFX_FSR3_ENABLE_AUTO_EXPOSURE
            fsr3::description_dispatch.renderSize.width       = fsr3::description_reactive_mask.renderSize.width;
            fsr3::description_dispatch.renderSize.height      = fsr3::description_reactive_mask.renderSize.height;
            fsr3::description_dispatch.cameraNear             = camera->GetFarPlane();                            // far as near because we are using reverse-z
            fsr3::description_dispatch.cameraFar              = camera->GetNearPlane();                           // near as far because we are using reverse-z
            fsr3::description_dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();

            // dispatch
            SP_ASSERT(ffxFsr3UpscalerContextDispatch(&fsr3::context, &fsr3::description_dispatch) == FFX_OK);
            fsr3::description_dispatch.reset = false;
        }
    }

    void RHI_FidelityFX::SSSR_Dispatch(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        RHI_Texture* tex_brdf,
        RHI_Texture* tex_output
    )
    {
        // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/stochastic-screen-space-reflections.md

        // transition the depth to shader read, to avoid validation errors caused by ffx
        // when trying to create a depth view that is incompatible with the resource properties
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        cmd_list->InsertPendingBarrierGroup();

        // set resources
        sssr::description_dispatch.commandList        = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
        sssr::description_dispatch.color              = to_ffx_resource(tex_color,           nullptr, nullptr, L"sssr_color");
        sssr::description_dispatch.depth              = to_ffx_resource(tex_depth,           nullptr, nullptr, L"sssr_depth");
        sssr::description_dispatch.motionVectors      = to_ffx_resource(tex_velocity,        nullptr, nullptr, L"sssr_velocity");
        sssr::description_dispatch.normal             = to_ffx_resource(tex_normal,          nullptr, nullptr, L"sssr_normal");
        sssr::description_dispatch.materialParameters = to_ffx_resource(tex_material,        nullptr, nullptr, L"sssr_roughness");   // FfxSssrDispatchDescription specifies the channel
        sssr::description_dispatch.environmentMap     = to_ffx_resource(sssr::cubemap.get(), nullptr, nullptr, L"sssr_environment"); // dummy/empty as we don't want SSSR to also do IBL
        sssr::description_dispatch.brdfTexture        = to_ffx_resource(tex_brdf,            nullptr, nullptr, L"sssr_brdf");
        sssr::description_dispatch.output             = to_ffx_resource(tex_output,          nullptr, nullptr, L"sssr_output");
 
        // set render size
        sssr::description_dispatch.renderSize.width  = static_cast<uint32_t>(tex_color->GetWidth()  * resolution_scale);
        sssr::description_dispatch.renderSize.height = static_cast<uint32_t>(tex_color->GetHeight() * resolution_scale);

        // set camera matrices
        {
            // note: ffx expects column-major layout (as well ass column-major memory layout) and right-handed matrices

            auto adjust_matrix_view = [](const Matrix& matrix)
            {
                Matrix adjusted = matrix.Transposed();

                // negate the third row to switch handedness
                adjusted.m20 = -adjusted.m20;
                adjusted.m21 = -adjusted.m21;
                adjusted.m22 = -adjusted.m22;
                adjusted.m23 = -adjusted.m23;

                return adjusted;
            };

            auto adjust_matrix_projection = [](const Matrix& matrix)
            {
                Matrix adjusted = matrix.Transposed();

                // adjust for reverse-z
                adjusted.m22 = 0.0f;
                adjusted.m23 = matrix.m32; // near plane value
                adjusted.m32 = -1.0f;
                adjusted.m33 = 0.0f;

                return adjusted;
            };

            Matrix view                     = adjust_matrix_view(cb_frame->view);
            Matrix projection               = adjust_matrix_projection(cb_frame->projection);
            static Matrix view_projection   = Matrix::Identity;
            Matrix view_inv                 = Matrix::Invert(view);
            Matrix projection_inv           = Matrix::Invert(projection);
            Matrix view_projection_previous = view_projection;
            view_projection                 = projection * view;
            Matrix view_projection_inv      = Matrix::Invert(view_projection);

            set_ffx_float16(sssr::description_dispatch.view,               view);
            set_ffx_float16(sssr::description_dispatch.invView,            view_inv);
            set_ffx_float16(sssr::description_dispatch.projection,         projection);
            set_ffx_float16(sssr::description_dispatch.invProjection,      projection_inv);
            set_ffx_float16(sssr::description_dispatch.invViewProjection,  view_projection_inv);
            set_ffx_float16(sssr::description_dispatch.prevViewProjection, view_projection_previous);
        }

        // set sssr specific parameters
        sssr::description_dispatch.motionVectorScale.x                  = -0.5f; // expects [-0.5, 0.5] range
        sssr::description_dispatch.motionVectorScale.y                  = -0.5f; // expects [-0.5, 0.5] range, +Y as top-down
        sssr::description_dispatch.normalUnPackMul                      = 1.0f;
        sssr::description_dispatch.normalUnPackAdd                      = 0.0f;
        sssr::description_dispatch.depthBufferThickness                 = 0.08f; // hit acceptance bias, larger values can cause streaks, lower values can cause holes
        sssr::description_dispatch.varianceThreshold                    = 0.0f;  // luminance differences between history results will trigger an additional ray if they are greater than this threshold value
        sssr::description_dispatch.maxTraversalIntersections            = 32;    // caps the maximum number of lookups that are performed from the depth buffer hierarchy, most rays should end after about 20 lookups
        sssr::description_dispatch.minTraversalOccupancy                = 4;     // exit the core loop early if less than this number of threads are running
        sssr::description_dispatch.mostDetailedMip                      = 0;
        sssr::description_dispatch.temporalStabilityFactor              = 0.5f;  // the accumulation of history values, Higher values reduce noise, but are more likely to exhibit ghosting artifacts
        sssr::description_dispatch.temporalVarianceGuidedTracingEnabled = true;  // whether a ray should be spawned on pixels where a temporal variance is detected or not
        sssr::description_dispatch.samplesPerQuad                       = 1;     // the minimum number of rays per quad, variance guided tracing can increase this up to a maximum of 4
        sssr::description_dispatch.iblFactor                            = 0.0f;
        sssr::description_dispatch.roughnessChannel                     = 0;
        sssr::description_dispatch.isRoughnessPerceptual                = true;
        sssr::description_dispatch.roughnessThreshold                   = 1.0f;  // regions with a roughness value greater than this threshold won't spawn rays

        // dispatch
        FfxErrorCode error_code = ffxSssrContextDispatch(&sssr::context, &sssr::description_dispatch);
        SP_ASSERT(error_code == FFX_OK);
    }

    void RHI_FidelityFX::BrixelizerGI_Update(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        vector<shared_ptr<Entity>>& entities,
        int64_t index_start,
        int64_t index_end,
        RHI_Texture* tex_debug
    )
    {
        // aabbs
        {
            // Delete existing instances
            vector<FfxBrixelizerInstanceID> instance_ids;
            for (const FfxBrixelizerInstanceDescription& instance : brixelizer_gi::instances)
            {
                if (instance.outInstanceID)
                    instance_ids.push_back(*instance.outInstanceID);
            }
            if (!instance_ids.empty())
            {
                FfxErrorCode error = ffxBrixelizerDeleteInstances(&brixelizer_gi::context, instance_ids.data(), static_cast<uint32_t>(instance_ids.size()));
                SP_ASSERT(error == FFX_OK);
            }
            
            // Create new instances
            {
                brixelizer_gi::instances.clear();
            
                for (shared_ptr<Entity>& entity : entities)
                {
                    shared_ptr<Renderable> renderable     = entity->GetComponent<Renderable>();
                    FfxBrixelizerInstanceDescription desc = {};
                
                    // aabb
                    const BoundingBox& aabb = renderable->GetBoundingBox(BoundingBoxType::Transformed);
                    desc.aabb.min[0]        = aabb.GetMin().x;
                    desc.aabb.min[1]        = aabb.GetMin().y;
                    desc.aabb.min[2]        = aabb.GetMin().z;
                    desc.aabb.max[0]        = aabb.GetMax().x;
                    desc.aabb.max[1]        = aabb.GetMax().y;
                    desc.aabb.max[2]        = aabb.GetMax().z;
                
                    // vertex buffer
                    auto vb = renderable->GetVertexBuffer();
                    auto vb_it = find_if(brixelizer_gi::buffer_indices.begin(), brixelizer_gi::buffer_indices.end(),
                        [vb](const auto& pair) { return pair.first == vb; });
                    if (vb_it == brixelizer_gi::buffer_indices.end()) {
                        uint32_t new_index = static_cast<uint32_t>(brixelizer_gi::buffer_indices.size());
                        brixelizer_gi::buffer_indices.emplace_back(vb, new_index);
                        FfxResource ffx_resource = to_ffx_resource(nullptr, nullptr, vb, L"VertexBuffer");
                        FfxBrixelizerBufferDescription buffer_desc = {};
                        buffer_desc.buffer = ffx_resource;
                        buffer_desc.outIndex = &new_index;
                        FfxErrorCode error = ffxBrixelizerRegisterBuffers(&brixelizer_gi::context, &buffer_desc, 1);
                        SP_ASSERT(error == FFX_OK);
                        desc.vertexBuffer = new_index;
                    }
                    else {
                        desc.vertexBuffer = vb_it->second;
                    }
                    desc.vertexCount = renderable->GetVertexCount();
                    desc.vertexStride = sizeof(RHI_Vertex_PosTexNorTan);
                    desc.vertexFormat = FFX_SURFACE_FORMAT_R32G32B32_FLOAT;
                    desc.vertexBufferOffset = 0;

                    // index buffer
                    auto ib = renderable->GetIndexBuffer();
                    auto ib_it = find_if(brixelizer_gi::buffer_indices.begin(), brixelizer_gi::buffer_indices.end(),
                        [ib](const auto& pair) { return pair.first == ib; });
                    if (ib_it == brixelizer_gi::buffer_indices.end()) {
                        uint32_t new_index = static_cast<uint32_t>(brixelizer_gi::buffer_indices.size());
                        brixelizer_gi::buffer_indices.emplace_back(ib, new_index);
                        FfxResource ffx_resource = to_ffx_resource(nullptr, nullptr, ib, L"IndexBuffer");
                        FfxBrixelizerBufferDescription buffer_desc = {};
                        buffer_desc.buffer = ffx_resource;
                        buffer_desc.outIndex = &new_index;
                        FfxErrorCode error = ffxBrixelizerRegisterBuffers(&brixelizer_gi::context, &buffer_desc, 1);
                        SP_ASSERT(error == FFX_OK);
                        desc.indexBuffer = new_index;
                    }
                    else {
                        desc.indexBuffer = ib_it->second;
                    }
                
                    // misc
                    desc.triangleCount = renderable->GetIndexCount() / 3;
                    desc.flags         = FFX_BRIXELIZER_INSTANCE_FLAG_DYNAMIC;
                
                    brixelizer_gi::instances.push_back(desc);
                }
                
                FfxErrorCode error = ffxBrixelizerCreateInstances(&brixelizer_gi::context, brixelizer_gi::instances.data(), static_cast<uint32_t>(brixelizer_gi::instances.size()));
                SP_ASSERT(error == FFX_OK);
            }
        }
        
        FfxBrixelizerStats stats            = {};
        size_t required_scratch_buffer_size = 0;
        
        for (uint32_t i = 0; i < brixelizer_gi::cascade_max; i++)
        {
            brixelizer_gi::description_update.resources.cascadeResources[i].aabbTree = to_ffx_resource(nullptr, brixelizer_gi::buffer_cascade_aabb_tree[i].get(), nullptr, L"brixelizer_gi_abbb_tree");
            brixelizer_gi::description_update.resources.cascadeResources[i].brickMap = to_ffx_resource(nullptr, brixelizer_gi::buffer_cascade_brick_map[i].get(), nullptr, L"brixelizer_gi_brick_map");
        }
        brixelizer_gi::description_update.resources.sdfAtlas   = to_ffx_resource(brixelizer_gi::texture_sdf_atlas.get(), nullptr,  nullptr, L"brixelizer_gi_sdf_atlas");
        brixelizer_gi::description_update.resources.brickAABBs = to_ffx_resource(nullptr, brixelizer_gi::buffer_brick_aabbs.get(), nullptr, L"brixelizer_gi_brick_aabbs");
        brixelizer_gi::sdf_center                              = brixelizer_gi::sdf_follow_camera ? cb_frame->camera_position : brixelizer_gi::sdf_center;
        brixelizer_gi::description_update.sdfCenter[0]         = brixelizer_gi::sdf_center.x;
        brixelizer_gi::description_update.sdfCenter[1]         = brixelizer_gi::sdf_center.y;
        brixelizer_gi::description_update.sdfCenter[2]         = brixelizer_gi::sdf_center.z;
        brixelizer_gi::description_update.frameIndex           = cb_frame->frame;
        brixelizer_gi::description_update.maxReferences        = 32 * (1 << 20);                // maximum number of triangle voxel references to be stored in the update
        brixelizer_gi::description_update.triangleSwapSize     = 300 * (1 << 20);               // size of the swap space available to be used for storing triangles in the update
        brixelizer_gi::description_update.maxBricksPerBake     = 1 << 14;                       // maximum number of bricks to be updated
        brixelizer_gi::description_update.outScratchBufferSize = &required_scratch_buffer_size; // the size of the GPU scratch buffer needed for ffxBrixelizerUpdate()
        brixelizer_gi::description_update.outStats             = &stats;                        // statistics for the update, stats read back after ffxBrixelizerUpdate()
        
        // debug visualization for: distance, uvw, iterations, brick id, cascade id
        bool debug_on     = brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Off;
        bool debug_update = brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Radiance && brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Irradiance;
        if (debug_on && debug_update)
        {
            brixelizer_gi::description_update.populateDebugAABBsFlags = FFX_BRIXELIZER_POPULATE_AABBS_DYNAMIC_INSTANCES;
            brixelizer_gi::description_update.debugVisualizationDesc  = &brixelizer_gi::debug_description;
            brixelizer_gi::debug_description.output                   = to_ffx_resource(tex_debug, nullptr, nullptr, L"brixelizer_gi_tex_debug");
            brixelizer_gi::debug_description.renderWidth              = tex_debug->GetWidth();
            brixelizer_gi::debug_description.renderHeight             = tex_debug->GetHeight();
            brixelizer_gi::debug_description.debugState               = brixelizer_gi::to_ffx_debug_mode(brixelizer_gi::debug_mode);
            brixelizer_gi::debug_description.startCascadeIndex        = brixelizer_gi::description_dispatch_gi.startCascade;
            brixelizer_gi::debug_description.endCascadeIndex          = brixelizer_gi::description_dispatch_gi.endCascade;
            brixelizer_gi::debug_description.tMin                     = brixelizer_gi::description_dispatch_gi.tMin;
            brixelizer_gi::debug_description.tMax                     = brixelizer_gi::description_dispatch_gi.tMax;
            brixelizer_gi::debug_description.sdfSolveEps              = 0.5f;

            set_ffx_float16(brixelizer_gi::debug_description.inverseViewMatrix,       Matrix::Transpose(cb_frame->view_inv));
            set_ffx_float16(brixelizer_gi::debug_description.inverseProjectionMatrix, Matrix::Transpose(cb_frame->projection_inv));
        }
        
        // bake
        FfxCommandList ffx_command_list = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
        FfxErrorCode error_code         = ffxBrixelizerBakeUpdate(&brixelizer_gi::context, &brixelizer_gi::description_update, &brixelizer_gi::description_update_baked);
        SP_ASSERT(error_code == FFX_OK);

        // grow scratch buffer (if needed)
        if (required_scratch_buffer_size > brixelizer_gi::buffer_scratch->GetObjectSize())
        {
            // round up to the nearest power of 2 for efficiency
            size_t new_size = 1;
            while (new_size < required_scratch_buffer_size)
            { 
                new_size <<= 1;
            }

            brixelizer_gi::buffer_scratch = make_shared<RHI_Buffer>(new_size, 1, RHI_Buffer_Transfer_Src | RHI_Buffer_Transfer_Dst, "ffx_brixelizer_gi_scratch");
            SP_LOG_INFO("Resized scratch buffer to %.2f MB", static_cast<float>(new_size) / (1024.0f * 1024.0f));
        }

        // update
        FfxResource scratch_buffer = to_ffx_resource(nullptr, brixelizer_gi::buffer_scratch.get(), nullptr, L"ffx_brixelizer_gi_scratch");
        error_code                 = ffxBrixelizerUpdate(&brixelizer_gi::context, &brixelizer_gi::description_update_baked, scratch_buffer, ffx_command_list);
        SP_ASSERT(error_code == FFX_OK);
    }

    void RHI_FidelityFX::BrixelizerGI_Dispatch(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        RHI_Texture* tex_color,
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
        // note: ffx expects row-major, right-handed matrices
        static Matrix view         = Matrix::Identity;
        static Matrix projection   = Matrix::Identity;
        Matrix view_previous       = view;
        Matrix projection_previous = projection;
        view                       = Matrix::Transpose(cb_frame->view);
        projection                 = Matrix::Transpose(cb_frame->projection);
        Matrix view_inverted       = Matrix::Invert(view);
        Matrix projection_inverted = Matrix::Invert(projection);

        bool debug_draw = brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Radiance || brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Irradiance;

        if (!debug_draw)
        { 
            // set camera matrices
            set_ffx_float16(brixelizer_gi::description_dispatch_gi.view,           view);
            set_ffx_float16(brixelizer_gi::description_dispatch_gi.prevView,       view_previous);
            set_ffx_float16(brixelizer_gi::description_dispatch_gi.projection,     projection);
            set_ffx_float16(brixelizer_gi::description_dispatch_gi.prevProjection, projection_previous);
            
            // set resources
            brixelizer_gi::description_dispatch_gi.environmentMap   = to_ffx_resource(sssr::cubemap.get(),                          nullptr, nullptr, L"brixelizer_environment");
            brixelizer_gi::description_dispatch_gi.prevLitOutput    = to_ffx_resource(tex_color,                                    nullptr, nullptr, L"brixelizer_gi_lit_output_previous");
            brixelizer_gi::description_dispatch_gi.depth            = to_ffx_resource(tex_depth,                                    nullptr, nullptr, L"brixelizer_gi_depth");
            brixelizer_gi::description_dispatch_gi.historyDepth     = to_ffx_resource(brixelizer_gi::texture_depth_previous.get(),  nullptr, nullptr, L"brixelizer_gi_depth_previous");
            brixelizer_gi::description_dispatch_gi.normal           = to_ffx_resource(tex_normal,                                   nullptr, nullptr, L"brixelizer_gi_normal");
            brixelizer_gi::description_dispatch_gi.historyNormal    = to_ffx_resource(brixelizer_gi::texture_normal_previous.get(), nullptr, nullptr, L"brixelizer_gi_normal_previous");
            brixelizer_gi::description_dispatch_gi.roughness        = to_ffx_resource(tex_material,                                 nullptr, nullptr, L"brixelizer_gi_roughness");
            brixelizer_gi::description_dispatch_gi.motionVectors    = to_ffx_resource(tex_velocity,                                 nullptr, nullptr, L"brixelizer_gi_velocity");
            brixelizer_gi::description_dispatch_gi.noiseTexture     = to_ffx_resource(tex_noise[cb_frame->frame % 8],               nullptr, nullptr, L"brixelizer_gi_noise");
            brixelizer_gi::description_dispatch_gi.outputDiffuseGI  = to_ffx_resource(tex_diffuse_gi,                               nullptr, nullptr, L"brixelizer_gi_diffuse_gi");
            brixelizer_gi::description_dispatch_gi.outputSpecularGI = to_ffx_resource(tex_specular_gi,                              nullptr, nullptr, L"brixelizer_gi_specular_gi");
            brixelizer_gi::description_dispatch_gi.sdfAtlas         = to_ffx_resource(brixelizer_gi::texture_sdf_atlas.get(),       nullptr, nullptr, L"brixelizer_gi_sdf_atlas");
            brixelizer_gi::description_dispatch_gi.bricksAABBs      = to_ffx_resource(nullptr, brixelizer_gi::buffer_brick_aabbs.get(), nullptr, L"brixelizer_gi_brick_aabbs");
            for (uint32_t i = 0; i < brixelizer_gi::cascade_max; i++)
            {
                brixelizer_gi::description_update.resources.cascadeResources[i].aabbTree = to_ffx_resource(nullptr, brixelizer_gi::buffer_cascade_aabb_tree[i].get(), nullptr, L"brixelizer_gi_abbb_tree");
                brixelizer_gi::description_update.resources.cascadeResources[i].brickMap = to_ffx_resource(nullptr, brixelizer_gi::buffer_cascade_brick_map[i].get(), nullptr, L"brixelizer_gi_brick_map");
            }
            
            // set parameters
            brixelizer_gi::description_dispatch_gi.startCascade            = 0                                  + (2 * brixelizer_gi::cascade_count); // index of the start cascade for use with ray marching with Brixelizer
            brixelizer_gi::description_dispatch_gi.endCascade              = (brixelizer_gi::cascade_count - 1) + (2 * brixelizer_gi::cascade_count); // index of the end cascade for use with ray marching with Brixelizer
            brixelizer_gi::description_dispatch_gi.rayPushoff              = 0.25f;                                                                   // distance from a surface along the normal vector to offset the diffuse ray origin
            brixelizer_gi::description_dispatch_gi.sdfSolveEps             = 0.5f;                                                                    // epsilon value for ray marching to be used with Brixelizer for diffuse rays
            brixelizer_gi::description_dispatch_gi.specularRayPushoff      = 0.25f;                                                                   // distance from a surface along the normal vector to offset the specular ray origin
            brixelizer_gi::description_dispatch_gi.specularSDFSolveEps     = 0.5f;                                                                    // epsilon value for ray marching to be used with Brixelizer for specular rays
            brixelizer_gi::description_dispatch_gi.tMin                    = 0.0f;
            brixelizer_gi::description_dispatch_gi.tMax                    = 10000.0f;
            brixelizer_gi::description_dispatch_gi.normalsUnpackMul        = 1.0f;                                                                    // a multiply factor to transform the normal to the space expected by Brixelizer GI
            brixelizer_gi::description_dispatch_gi.normalsUnpackAdd        = 0.0f;                                                                    // an offset to transform the normal to the space expected by Brixelizer GI
            brixelizer_gi::description_dispatch_gi.isRoughnessPerceptual   = true;                                                                    // if false, we assume roughness squared was stored in the Gbuffer
            brixelizer_gi::description_dispatch_gi.roughnessChannel        = 0;                                                                       // the channel to read the roughness from the roughness texture
            brixelizer_gi::description_dispatch_gi.roughnessThreshold      = 1.0f;                                                                    // regions with a roughness value greater than this threshold won't spawn specular rays
            brixelizer_gi::description_dispatch_gi.environmentMapIntensity = 0.0f;                                                                    // value to scale the contribution from the environment map
            brixelizer_gi::description_dispatch_gi.motionVectorScale.x     = 1.0f;                                                                    // scale factor to apply to motion vectors
            brixelizer_gi::description_dispatch_gi.motionVectorScale.y     = 1.0f;                                                                    // scale factor to apply to motion vectors
            set_ffx_float3(brixelizer_gi::description_dispatch_gi.cameraPosition, cb_frame->camera_position);                                         // camera position

            // dispatch
            {
                FfxErrorCode error_code = ffxBrixelizerGetRawContext(&brixelizer_gi::context, &brixelizer_gi::description_dispatch_gi.brixelizerContext); // get the raw context for use with Brixelizer GI
                SP_ASSERT(error_code == FFX_OK);
                
                // dispatch
                FfxCommandList ffx_command_list = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
                error_code                      = ffxBrixelizerGIContextDispatch(&brixelizer_gi::context_gi, &brixelizer_gi::description_dispatch_gi, ffx_command_list);
                SP_ASSERT(error_code == FFX_OK);
            }

            // blit the dept and the normal so that we can use them in the next frame as "history"
            cmd_list->Blit(tex_depth,  brixelizer_gi::texture_depth_previous.get(),  false);
            cmd_list->Blit(tex_normal, brixelizer_gi::texture_normal_previous.get(), false);
        }
        else
        {
            // set camera matrices
            set_ffx_float16(brixelizer_gi::debug_description_gi.view,       view);
            set_ffx_float16(brixelizer_gi::debug_description_gi.projection, projection);

            // set resources
            brixelizer_gi::debug_description_gi.outputDebug   = to_ffx_resource(tex_debug, nullptr, nullptr, L"brixelizer_gi_debug");
            brixelizer_gi::debug_description_gi.outputSize[0] = tex_debug->GetWidth();
            brixelizer_gi::debug_description_gi.outputSize[1] = tex_debug->GetHeight();

            // set parameters
            brixelizer_gi::debug_description_gi.debugMode        = brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Radiance ? FFX_BRIXELIZER_GI_DEBUG_MODE_RADIANCE_CACHE : FFX_BRIXELIZER_GI_DEBUG_MODE_IRRADIANCE_CACHE;
            brixelizer_gi::debug_description_gi.normalsUnpackMul = brixelizer_gi::description_dispatch_gi.normalsUnpackMul;
            brixelizer_gi::debug_description_gi.normalsUnpackAdd = brixelizer_gi::description_dispatch_gi.normalsUnpackAdd;
            brixelizer_gi::debug_description_gi.startCascade     = brixelizer_gi::description_dispatch_gi.startCascade;
            brixelizer_gi::debug_description_gi.endCascade       = brixelizer_gi::description_dispatch_gi.endCascade;
            brixelizer_gi::debug_description_gi.depth            = brixelizer_gi::description_dispatch_gi.depth;
            brixelizer_gi::debug_description_gi.normal           = brixelizer_gi::description_dispatch_gi.normal;
            brixelizer_gi::debug_description_gi.sdfAtlas         = brixelizer_gi::description_dispatch_gi.sdfAtlas;
            brixelizer_gi::debug_description_gi.bricksAABBs      = brixelizer_gi::description_dispatch_gi.bricksAABBs;
            for (uint32_t i = 0; i < brixelizer_gi::cascade_max; ++i)
            {
                brixelizer_gi::debug_description_gi.cascadeAABBTrees[i] = brixelizer_gi::description_dispatch_gi.cascadeAABBTrees[i];
                brixelizer_gi::debug_description_gi.cascadeBrickMaps[i] = brixelizer_gi::description_dispatch_gi.cascadeBrickMaps[i];
            }

            // dispatch
            {
                FfxErrorCode error = ffxBrixelizerGetRawContext(&brixelizer_gi::context, &brixelizer_gi::debug_description_gi.brixelizerContext);
                SP_ASSERT(error == FFX_OK);

                FfxCommandList ffx_command_list = ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
                error = ffxBrixelizerGIContextDebugVisualization(&brixelizer_gi::context_gi, &brixelizer_gi::debug_description_gi, ffx_command_list);
                SP_ASSERT(error == FFX_OK);
            }
        }
    }
}
