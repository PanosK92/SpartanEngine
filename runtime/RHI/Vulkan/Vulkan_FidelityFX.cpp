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
#include "../RHI_Buffer.h"
#include "../RHI_Device.h"
#include "../RHI_Swapchain.h"
#include "../RHI_Queue.h"
#include "../RHI_Shader.h"
#include "../RHI_Pipeline.h"
#include "../Input/Input.h"
#include "../Rendering/Renderer_Buffers.h"
#include "../Rendering/Renderer.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Camera.h"
#include "../World/Entity.h"
#include "../Core/Debugging.h"
SP_WARNINGS_OFF
#ifdef _MSC_VER
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_sssr.h>
#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>
#include <FidelityFX/host/ffx_breadcrumbs.h>
#endif
SP_WARNINGS_ON
//=============================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    #ifdef _MSC_VER
    namespace
    {
        // shared among all contexts
        FfxInterface ffx_interface      = {};
        Matrix view                     = Matrix::Identity;
        Matrix view_previous            = Matrix::Identity;
        Matrix projection               = Matrix::Identity;
        Matrix projection_previous      = Matrix::Identity;
        Matrix view_projection          = Matrix::Identity;
        Matrix view_inverted            = Matrix::Identity;
        Matrix projection_inverted      = Matrix::Identity;
        Matrix view_projection_previous = Matrix::Identity;
        Matrix view_projection_inverted = Matrix::Identity;

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

        FfxSurfaceFormat to_ffx_format(const RHI_Format format)
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
            case FFX_SURFACE_FORMAT_UNKNOWN:
                return RHI_Format::Max;
            default:
                SP_ASSERT_MSG(false, "Unsupported FFX format");
                return RHI_Format::Max;
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

        template<typename T>
        FfxResource to_ffx_resource(T* resource, const wchar_t* name)
        {
            FfxResourceDescription description = {};
            FfxResourceStates state            = FFX_RESOURCE_STATE_COMMON;
            void* rhi_resource                 = const_cast<T*>(resource)->GetRhiResource();

            if constexpr (is_same_v<remove_const_t<T>, RHI_Texture>)
            {
                state = to_ffx_resource_state(resource->GetLayout(0));

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
                description.format   = to_ffx_format(resource->GetFormat());
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

        // overload for nullptr
        FfxResource to_ffx_resource(nullptr_t, const wchar_t* name)
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

        FfxCommandList to_ffx_cmd_list(RHI_CommandList* cmd_list)
        {
            return ffxGetCommandListVK(static_cast<VkCommandBuffer>(cmd_list->GetRhiResource()));
        }

        FfxPipeline to_ffx_pipeline(RHI_Pipeline* pipeline)
        {
            return ffxGetPipelineVK(static_cast<VkPipeline>(pipeline->GetRhiResource()));
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

        Matrix to_ffx_matrix_view(const Matrix& matrix)
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

        Matrix to_ffx_matrix_projection(const Matrix& matrix)
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

        const char* convert_wchar_to_char(const wchar_t* wchar_str)
        {
            // get the required size for the multibyte string
            size_t size_needed = wcstombs(nullptr, wchar_str, 0) + 1;
        
            // allocate memory for the multibyte string
            char* char_str = new char[size_needed];
        
            // convert the wchar_t string to a multibyte string
            wcstombs(char_str, wchar_str, size_needed);
        
            return char_str;
        }

        namespace fsr3
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
        }

        namespace sssr
        {
            bool                       context_created      = false;
            FfxSssrContext             context              = {};
            FfxSssrContextDescription  description_context  = {};
            FfxSssrDispatchDescription description_dispatch = {};
        }

        namespace brixelizer_gi
        {
            // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/brixelizer.md
            // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/brixelizer-gi.md

            // sdk issue #1: the sdk should keep track of static/dynamic instances and decide what needs to be deleted or created, not the user.
            // sdk issue #2: all the buffers which are needed, should be created and bound internally by the sdk, not the user.
            // sdk issue #3: instance ids are really indices, using actual ids (a big number) will cause an out of bounds crash.
            // sdk issue #4: the previous depth and normal textures, should be created internally using a blit operation, not by the user.

            // parameters
            const float    voxel_size               = 0.05f;
            const float    cascade_size_ratio       = 2.0f;
            const uint32_t cascade_count            = 8;         // max is 24
            const uint32_t cascade_offset           = 16;        // 0 - 8 is for static, 8 - 16 is for dynamic, 16 - 24 is for static + dynamic (merged)
            const uint32_t cascade_index_start      = cascade_offset + 0;
            const uint32_t cascade_index_end        = cascade_offset + cascade_count - 1;
            const uint32_t cascade_resolution       = 64;
            const uint32_t sdf_atlas_size           = 512;
            const bool     sdf_center_around_camera = false;
            const float    sdf_ray_normal_offset    = 0.5f;      // distance from a surface along the normal vector to offset the ray origin - below 0.5 I see artifacts
            const float    sdf_ray_epsilon          = 0.5f;      // epsilon value for ray marching to be used with brixelizer for rays
            const uint32_t bricks_max               = 262144;
            const uint32_t bricks_per_update_max    = 16384;     // maximum number of bricks to be updated
            const uint32_t triangle_references_max  = 33554432;  // maximum number of triangle voxel references to be stored in the update
            const uint32_t triangle_swap_size       = 314572800; // size of the swap space available to be used for storing triangles in the update
            const float t_min                       = 0.0f;
            const float t_max                       = 10000.0f;

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

                // else register a new one
                {
                    uint32_t index = 0;

                    FfxBrixelizerBufferDescription buffer_desc = {};
                    buffer_desc.buffer                         = to_ffx_resource(buffer, L"brixelizer_gi_buffer");
                    buffer_desc.outIndex                       = &index;
                    SP_ASSERT(ffxBrixelizerRegisterBuffers(&context, &buffer_desc, 1) == FFX_OK);

                    instance_buffers.emplace_back(buffer, index);

                    return index;
                }
            }

           FfxBrixelizerInstanceDescription create_instance_description(const shared_ptr<Entity>& entity, uint32_t instance_index = 0)
           {
               FfxBrixelizerInstanceDescription desc = {};
               shared_ptr<Renderable> renderable     = entity->GetComponent<Renderable>();
           
               // aabb: world space, pre-transformed
               const BoundingBox& aabb = renderable->HasInstancing() ? renderable->GetBoundingBox(BoundingBoxType::TransformedInstance, instance_index) : renderable->GetBoundingBox(BoundingBoxType::Transformed);
               desc.aabb.min[0]        = aabb.GetMin().x;
               desc.aabb.min[1]        = aabb.GetMin().y;
               desc.aabb.min[2]        = aabb.GetMin().z;
               desc.aabb.max[0]        = aabb.GetMax().x;
               desc.aabb.max[1]        = aabb.GetMax().y;
               desc.aabb.max[2]        = aabb.GetMax().z;
           
               // transform: world space, row-major
               Matrix transform = renderable->HasInstancing() ? renderable->GetInstanceTransform(instance_index) : entity->GetMatrix();
               set_ffx_float16(desc.transform, transform);
           
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
               desc.flags           = entity->IsMoving() ? FFX_BRIXELIZER_INSTANCE_FLAG_DYNAMIC : FFX_BRIXELIZER_INSTANCE_FLAG_NONE;
               uint64_t instance_id = renderable->HasInstancing() ? (entity->GetObjectId() | (static_cast<uint64_t>(instance_index) << 32)) : entity->GetObjectId();
               desc.outInstanceID   = &get_or_create_id(instance_id);
           
               return desc;
           }

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
            DebugMode debug_mode            = DebugMode::Max;
            bool debug_mode_arrow_switch    = false;
            bool debug_mode_aabbs_and_stats = false;
            bool debug_mode_log_instances   = false;
            FfxBrixelizerStats debug_stats  = {};

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

            string debug_mode_to_string(const DebugMode debug_mode)
            {
                if (debug_mode == brixelizer_gi::DebugMode::Distance)   return "Distance";
                if (debug_mode == brixelizer_gi::DebugMode::UVW)        return "UVW";
                if (debug_mode == brixelizer_gi::DebugMode::Iterations) return "Iterations";
                if (debug_mode == brixelizer_gi::DebugMode::Gradient)   return "Gradient";
                if (debug_mode == brixelizer_gi::DebugMode::BrickID)    return "Brick ID";
                if (debug_mode == brixelizer_gi::DebugMode::CascadeID)  return "Cascade ID";
                if (debug_mode == brixelizer_gi::DebugMode::Radiance)   return "Radiance";
                if (debug_mode == brixelizer_gi::DebugMode::Irradiance) return "Irradiance";

                return "Disabled";
            }
        }

        namespace breadcrumbs
        {
            // requires: VK_KHR_synchronization2 because of vkCmdWriteBufferMarkerAMD and vkCmdWriteBufferMarker2AMD 
            bool                  context_created = false;
            FfxBreadcrumbsContext context         = {};
        }
    }
    #endif 

    void RHI_FidelityFX::Initialize()
    {
    #ifdef _MSC_VER
        Settings::RegisterThirdPartyLib("AMD FidelityFX", "1.1.1", "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK");

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
            
            SP_ASSERT(ffxGetInterfaceVK(&ffx_interface, ffxGetDeviceVK(&device_context), scratch_buffer, scratch_buffer_size, max_contexts)== FFX_OK);
        }

        // assets
        {
            // brixelizer gi
            {
                // sdf atlas texture
                brixelizer_gi::texture_sdf_atlas = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type3D,
                    brixelizer_gi::sdf_atlas_size,
                    brixelizer_gi::sdf_atlas_size,
                    brixelizer_gi::sdf_atlas_size,
                    1,
                    RHI_Format::R8_Unorm,
                    RHI_Texture_Srv | RHI_Texture_Uav,
                    "ffx_sdf_atlas"
                );

                // scratch buffer
                brixelizer_gi::buffer_scratch = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Storage,
                    1 << 30, // stride - 1024 MB (will assert if not enough)
                    1,       // element count
                    nullptr,
                    false,   // mappable
                    "ffx_brixelizer_gi_scratch"
                );

                // brick aabbs buffer
                brixelizer_gi::buffer_brick_aabbs = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Storage,
                    static_cast<uint32_t>(sizeof(uint32_t)), // stride
                    brixelizer_gi::bricks_max,               // element count
                    nullptr,
                    false,                                   // mappable
                    "ffx_brick_aabbs"
                );

                // cascade aabb trees
                const uint32_t cascade_aabb_tree_size = (16 * 16 * 16) * sizeof(uint32_t) + (4 * 4 * 4 + 1) * sizeof(Vector3) * 2;
                for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
                {
                    string name = "ffx_cascade_aabb_tree_" + to_string(i);
                    brixelizer_gi::buffer_cascade_aabb_tree[i] = make_shared<RHI_Buffer>(
                        RHI_Buffer_Type::Storage,
                        static_cast<uint32_t>(sizeof(uint32_t)),                          // stride
                        static_cast<uint32_t>(cascade_aabb_tree_size / sizeof(uint32_t)), // element count
                        nullptr,
                        false,                                                            // mappable
                        name.c_str()
                    );
                }

                // cascade brick maps
                const uint32_t cascade_brick_map_size = brixelizer_gi::cascade_resolution * brixelizer_gi::cascade_resolution * brixelizer_gi::cascade_resolution * sizeof(uint32_t);
                for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
                {
                    string name = "ffx_cascade_brick_map_" + to_string(i);
                    brixelizer_gi::buffer_cascade_brick_map[i] = make_shared<RHI_Buffer>(
                        RHI_Buffer_Type::Storage,
                        static_cast<uint32_t>(sizeof(uint32_t)),                          // stride
                        static_cast<uint32_t>(cascade_brick_map_size / sizeof(uint32_t)), // element count
                        nullptr,
                        false,                                                            // mappable
                        name.c_str()
                    );
                }
            }
        }
    #endif
    }

    void RHI_FidelityFX::DestroyContexts()
    {
    #ifdef _MSC_VER
        if (breadcrumbs::context_created)
        {
            SP_ASSERT(ffxBreadcrumbsContextDestroy(&breadcrumbs::context) == FFX_OK);
            breadcrumbs::context_created = false;
        }

        // brixelizer gi
        if (brixelizer_gi::context_created)
        {
            SP_ASSERT(ffxBrixelizerContextDestroy(&brixelizer_gi::context) == FFX_OK);
            SP_ASSERT(ffxBrixelizerGIContextDestroy(&brixelizer_gi::context_gi) == FFX_OK);
            brixelizer_gi::static_instances.clear();
            brixelizer_gi::instance_buffers.clear();
            brixelizer_gi::entity_map.clear();
            brixelizer_gi::instances_to_create.clear();
            brixelizer_gi::instances_to_delete.clear();
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

            fsr3::texture_depth_previous_nearest_reconstructed = nullptr;
            fsr3::texture_depth_dilated                        = nullptr;
            fsr3::texture_motion_vectors_dilated               = nullptr;
        }
    #endif
    }

    void RHI_FidelityFX::Shutdown()
    {
    #ifdef _MSC_VER
        DestroyContexts();

        // ffx interface
        if (ffx_interface.scratchBuffer != nullptr)
        {
            free(ffx_interface.scratchBuffer);
        }

        brixelizer_gi::texture_sdf_atlas       = nullptr;
        brixelizer_gi::buffer_brick_aabbs      = nullptr;
        brixelizer_gi::buffer_scratch          = nullptr;
        brixelizer_gi::texture_depth_previous  = nullptr;
        brixelizer_gi::texture_normal_previous = nullptr;
        brixelizer_gi::buffer_cascade_aabb_tree.fill(nullptr);
        brixelizer_gi::buffer_cascade_brick_map.fill(nullptr);

    #endif
    }

    void RHI_FidelityFX::Resize(const Vector2& resolution_render, const Vector2& resolution_output)
    {
    #ifdef _MSC_VER
        // some contexts are resolution dependent, so we destroy and (re)create them here
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

            // create shared resources (between upscaler and interpolator)
            {
                ffxFsr3UpscalerGetSharedResourceDescriptions(&fsr3::context, &fsr3::description_shared_resources);

                FfxCreateResourceDescription resource = fsr3::description_shared_resources.reconstructedPrevNearestDepth;
                fsr3::texture_depth_previous_nearest_reconstructed = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    resource.resourceDescription.width,
                    resource.resourceDescription.height,
                    resource.resourceDescription.depth,
                    resource.resourceDescription.mipCount,
                    to_rhi_format(resource.resourceDescription.format),
                    RHI_Texture_Srv | RHI_Texture_Uav | RHI_Texture_ClearBlit,
                    convert_wchar_to_char(resource.name)
                );

                resource = fsr3::description_shared_resources.dilatedDepth;
                fsr3::texture_depth_dilated = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    resource.resourceDescription.width,
                    resource.resourceDescription.height,
                    resource.resourceDescription.depth,
                    resource.resourceDescription.mipCount,
                    to_rhi_format(resource.resourceDescription.format),
                    RHI_Texture_Srv | RHI_Texture_Uav,
                    convert_wchar_to_char(resource.name)
                );

                resource = fsr3::description_shared_resources.dilatedMotionVectors;
                fsr3::texture_motion_vectors_dilated = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D,
                    resource.resourceDescription.width,
                    resource.resourceDescription.height,
                    resource.resourceDescription.depth,
                    resource.resourceDescription.mipCount, 
                    to_rhi_format(resource.resourceDescription.format),
                    RHI_Texture_Srv | RHI_Texture_Uav,
                    convert_wchar_to_char(resource.name)
                );
            }

            // set velocity factor [0, 1], this controls the temporal stability of bright pixels
            ffxFsr3UpscalerSetConstant(&fsr3::context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, static_cast<void*>(&fsr3::velocity_factor));

            // reset jitter index
            fsr3::jitter_index = 0;
        }

        // sssr
        if (!sssr::context_created)
        {
             sssr::description_context.renderSize.width           = width;
             sssr::description_context.renderSize.height          = height;
             sssr::description_context.normalsHistoryBufferFormat = to_ffx_format(RHI_Format::R16G16B16A16_Float);
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
                set_ffx_float3(brixelizer_gi::description_context.sdfCenter, Vector3::Zero);

                // cascades
                brixelizer_gi::description_context.numCascades = brixelizer_gi::cascade_count;
                float voxel_size = brixelizer_gi::voxel_size;
                for (uint32_t i = 0; i < brixelizer_gi::cascade_count; ++i)
                {
                    FfxBrixelizerCascadeDescription* cascade_description  = &brixelizer_gi::description_context.cascadeDescs[i];
                    cascade_description->flags                            = static_cast<FfxBrixelizerCascadeFlag>(FFX_BRIXELIZER_CASCADE_STATIC | FFX_BRIXELIZER_CASCADE_DYNAMIC);
                    cascade_description->voxelSize                        = voxel_size;
                    voxel_size                                           *= brixelizer_gi::cascade_size_ratio;
                }

                // interface
                brixelizer_gi::description_context.flags            = brixelizer_gi::debug_mode_aabbs_and_stats ? FFX_BRIXELIZER_CONTEXT_FLAG_ALL_DEBUG : static_cast<FfxBrixelizerContextFlags>(0);
                brixelizer_gi::description_context.backendInterface = ffx_interface;

                SP_ASSERT(ffxBrixelizerContextCreate(&brixelizer_gi::description_context, &brixelizer_gi::context) == FFX_OK);
            }

            // context gi
            {
                brixelizer_gi::description_context_gi.internalResolution = FfxBrixelizerGIInternalResolution::FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT; // render resolution
                brixelizer_gi::description_context_gi.displaySize.width  = static_cast<uint32_t>(resolution_render.x);                                          // output resolution
                brixelizer_gi::description_context_gi.displaySize.height = static_cast<uint32_t>(resolution_render.y);                                          // output resolution
                brixelizer_gi::description_context_gi.flags              = FfxBrixelizerGIFlags::FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED;
                brixelizer_gi::description_context_gi.backendInterface   = ffx_interface;
                
                SP_ASSERT(ffxBrixelizerGIContextCreate(&brixelizer_gi::context_gi, &brixelizer_gi::description_context_gi) == FFX_OK);
            }
            
            // resources
            {
                uint32_t flags = RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit;
                brixelizer_gi::texture_depth_previous  = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::D32_Float,          flags, "ffx_depth_previous");
                brixelizer_gi::texture_normal_previous = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, RHI_Format::R16G16B16A16_Float, flags, "ffx_normal_previous");
            }

            brixelizer_gi::context_created = true;
        }

        // breacrumbs
        if (!breadcrumbs::context_created && Debugging::IsBreadcrumbsEnabled())
        {
            uint32_t gpu_queue_indices[2] =
            {
                RHI_Device::GetQueueIndex(RHI_Queue_Type::Graphics),
                RHI_Device::GetQueueIndex(RHI_Queue_Type::Compute)
            };

             FfxBreadcrumbsContextDescription context_description = {};
             context_description.backendInterface                 = ffx_interface;
             context_description.maxMarkersPerMemoryBlock         = 3;
             context_description.usedGpuQueuesCount               = 2;
             context_description.pUsedGpuQueues                   = &gpu_queue_indices[0];
             context_description.allocCallbacks.fpAlloc           = malloc;
             context_description.allocCallbacks.fpRealloc         = realloc;
             context_description.allocCallbacks.fpFree            = free;
             context_description.frameHistoryLength               = Renderer::GetSwapChain()->GetBufferCount() * 2; // double the swapchain's backbuffer count
             context_description.flags                            = FFX_BREADCRUMBS_PRINT_FINISHED_LISTS    |
                                                                    FFX_BREADCRUMBS_PRINT_NOT_STARTED_LISTS |
                                                                    FFX_BREADCRUMBS_PRINT_FINISHED_NODES    |
                                                                    FFX_BREADCRUMBS_PRINT_NOT_STARTED_NODES;

             SP_ASSERT(ffxBreadcrumbsContextCreate(&breadcrumbs::context, &context_description) == FFX_OK);
             breadcrumbs::context_created = true;
        }
    #endif
    }
 
    void RHI_FidelityFX::Tick(Cb_Frame* cb_frame)
    {
    #ifdef _MSC_VER
        // matrices - ffx is right-handed
        {
            view_previous            = view;
            projection_previous      = projection;
            view_projection_previous = view_projection;

            view                     = to_ffx_matrix_view(cb_frame->view);
            projection               = to_ffx_matrix_projection(cb_frame->projection);
            view_projection          = projection * view;

            view_inverted            = Matrix::Invert(view);
            projection_inverted      = Matrix::Invert(projection);
            view_projection_inverted = Matrix::Invert(view_projection);
        }

        // brixelizer gi
        if (brixelizer_gi::debug_mode_arrow_switch)
        {
            if (Input::GetKeyDown(KeyCode::Arrow_Left))
            {
                brixelizer_gi::debug_mode = static_cast<brixelizer_gi::DebugMode>(
                    (static_cast<uint32_t>(brixelizer_gi::debug_mode) - 1 + static_cast<uint32_t>(brixelizer_gi::DebugMode::Max) + 1) %
                    (static_cast<uint32_t>(brixelizer_gi::DebugMode::Max) + 1));
                SP_LOG_INFO("Debug mode: %s", brixelizer_gi::debug_mode_to_string(brixelizer_gi::debug_mode));
            }
            else if (Input::GetKeyDown(KeyCode::Arrow_Right))
            {
                brixelizer_gi::debug_mode = static_cast<brixelizer_gi::DebugMode>(
                    (static_cast<uint32_t>(brixelizer_gi::debug_mode) + 1) %
                    (static_cast<uint32_t>(brixelizer_gi::DebugMode::Max) + 1));
                SP_LOG_INFO("Debug mode: %s", brixelizer_gi::debug_mode_to_string(brixelizer_gi::debug_mode));
            }

        }

        // breadcrumbs
        if (breadcrumbs::context_created)
        {
             SP_ASSERT(ffxBreadcrumbsStartFrame(&breadcrumbs::context) == FFX_OK);
        }
    #endif
    }

    void RHI_FidelityFX::FSR3_ResetHistory()
    {
    #ifdef _MSC_VER
        fsr3::description_dispatch.reset = true;
    #endif
    }

    void RHI_FidelityFX::FSR3_GenerateJitterSample(float* x, float* y)
    {
    #ifdef _MSC_VER
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
    #endif
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
        RHI_Texture* tex_output
    )
    {
    #ifdef _MSC_VER
        // output is displayed in the viewport, so add a barrier to ensure any work is done before writting to it
        cmd_list->InsertBarrierTextureReadWrite(tex_output);
        cmd_list->InsertPendingBarrierGroup();

        // upscale
        {
            // set resources (no need for the transparency or reactive masks as we do them later, full res)
            fsr3::description_dispatch.commandList                   = to_ffx_cmd_list(cmd_list);
            fsr3::description_dispatch.color                         = to_ffx_resource(tex_color,                                                L"fsr3_color");
            fsr3::description_dispatch.depth                         = to_ffx_resource(tex_depth,                                                L"fsr3_depth");
            fsr3::description_dispatch.motionVectors                 = to_ffx_resource(tex_velocity,                                             L"fsr3_velocity");
            fsr3::description_dispatch.exposure                      = to_ffx_resource(nullptr,                                                  L"fsr3_exposure");
            fsr3::description_dispatch.reactive                      = to_ffx_resource(nullptr,                                                  L"fsr3_reactive");
            fsr3::description_dispatch.transparencyAndComposition    = to_ffx_resource(nullptr,                                                  L"fsr3_transaprency_and_composition");
            fsr3::description_dispatch.dilatedDepth                  = to_ffx_resource(fsr3::texture_depth_dilated.get(),                        L"fsr3_depth_dilated");
            fsr3::description_dispatch.dilatedMotionVectors          = to_ffx_resource(fsr3::texture_motion_vectors_dilated.get(),               L"fsr3_motion_vectors_dilated");
            fsr3::description_dispatch.reconstructedPrevNearestDepth = to_ffx_resource(fsr3::texture_depth_previous_nearest_reconstructed.get(), L"fsr3_depth_nearest_previous_reconstructed");
            fsr3::description_dispatch.output                        = to_ffx_resource(tex_output,                                               L"fsr3_output");

            // configure
            fsr3::description_dispatch.motionVectorScale.x    = -static_cast<float>(tex_velocity->GetWidth());
            fsr3::description_dispatch.motionVectorScale.y    = -static_cast<float>(tex_velocity->GetHeight());
            fsr3::description_dispatch.enableSharpening       = sharpness != 0.0f;         // sdk issue: redundant paramter
            fsr3::description_dispatch.sharpness              = sharpness;                 
            fsr3::description_dispatch.frameTimeDelta         = delta_time_sec * 1000.0f;  // seconds to milliseconds
            fsr3::description_dispatch.preExposure            = exposure;                  // the exposure value if not using FFX_FSR3_ENABLE_AUTO_EXPOSURE
            fsr3::description_dispatch.renderSize.width       = tex_velocity->GetWidth();  
            fsr3::description_dispatch.renderSize.height      = tex_velocity->GetHeight(); 
            fsr3::description_dispatch.cameraNear             = camera->GetFarPlane();     // far as near because we are using reverse-z
            fsr3::description_dispatch.cameraFar              = camera->GetNearPlane();    // near as far because we are using reverse-z
            fsr3::description_dispatch.cameraFovAngleVertical = camera->GetFovVerticalRad();


            // dispatch
            SP_ASSERT(ffxFsr3UpscalerContextDispatch(&fsr3::context, &fsr3::description_dispatch) == FFX_OK);
            fsr3::description_dispatch.reset = false;
        }
    #endif
    }

    void RHI_FidelityFX::SSSR_Dispatch(
        RHI_CommandList* cmd_list,
        const float resolution_scale,
        RHI_Texture* tex_color,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        RHI_Texture* tex_brdf,
        RHI_Texture* tex_skybox,
        RHI_Texture* tex_output
    )
    {
    #ifdef _MSC_VER
        // documentation: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/stochastic-screen-space-reflections.md

        // transition the depth to shader read, to avoid validation errors caused by ffx
        // when trying to create a depth view that is incompatible with the resource properties
        tex_depth->SetLayout(RHI_Image_Layout::Shader_Read, cmd_list);
        cmd_list->InsertPendingBarrierGroup();

        // set resources
        sssr::description_dispatch.commandList        = to_ffx_cmd_list(cmd_list);
        sssr::description_dispatch.color              = to_ffx_resource(tex_color,    L"sssr_color");
        sssr::description_dispatch.depth              = to_ffx_resource(tex_depth,    L"sssr_depth");
        sssr::description_dispatch.motionVectors      = to_ffx_resource(tex_velocity, L"sssr_velocity");
        sssr::description_dispatch.normal             = to_ffx_resource(tex_normal,   L"sssr_normal");
        sssr::description_dispatch.materialParameters = to_ffx_resource(tex_material, L"sssr_roughness");   // FfxSssrDispatchDescription specifies the channel
        sssr::description_dispatch.environmentMap     = to_ffx_resource(tex_skybox,   L"sssr_environment");
        sssr::description_dispatch.brdfTexture        = to_ffx_resource(tex_brdf,     L"sssr_brdf");
        sssr::description_dispatch.output             = to_ffx_resource(tex_output,   L"sssr_output");
 
        // set render size
        sssr::description_dispatch.renderSize.width  = static_cast<uint32_t>(tex_color->GetWidth()  * resolution_scale);
        sssr::description_dispatch.renderSize.height = static_cast<uint32_t>(tex_color->GetHeight() * resolution_scale);

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
        sssr::description_dispatch.temporalStabilityFactor              = 0.8f;  // the accumulation of history values, higher values reduce noise, but are more likely to exhibit ghosting artifacts
        sssr::description_dispatch.temporalVarianceGuidedTracingEnabled = true;  // whether a ray should be spawned on pixels where a temporal variance is detected or not
        sssr::description_dispatch.samplesPerQuad                       = 1;     // the minimum number of rays per quad, variance guided tracing can increase this up to a maximum of 4
        sssr::description_dispatch.iblFactor                            = 1.0f;
        sssr::description_dispatch.roughnessChannel                     = 0;
        sssr::description_dispatch.isRoughnessPerceptual                = true;
        sssr::description_dispatch.roughnessThreshold                   = 1.0f;  // regions with a roughness value greater than this threshold won't spawn rays

        // set camera matrices
        set_ffx_float16(sssr::description_dispatch.view,               view);
        set_ffx_float16(sssr::description_dispatch.invView,            view_inverted);
        set_ffx_float16(sssr::description_dispatch.projection,         projection);
        set_ffx_float16(sssr::description_dispatch.invProjection,      projection_inverted);
        set_ffx_float16(sssr::description_dispatch.invViewProjection,  view_projection_inverted);
        set_ffx_float16(sssr::description_dispatch.prevViewProjection, view_projection_previous);

        // dispatch
        FfxErrorCode error_code = ffxSssrContextDispatch(&sssr::context, &sssr::description_dispatch);
        SP_ASSERT(error_code == FFX_OK);
    #endif
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
    #ifdef _MSC_VER
        // instances
        {
            brixelizer_gi::instances_to_create.clear();
            brixelizer_gi::instances_to_delete.clear();
            brixelizer_gi::entity_map.clear();
        
            // process entities
            for (int64_t i = index_start; i < index_end; i++)
            {
                auto& entity                         = entities[i];
                uint64_t entity_id                   = entity->GetObjectId();
                brixelizer_gi::entity_map[entity_id] = entity;
                bool is_dynamic                      = entity->IsMoving();
                auto static_it                       = brixelizer_gi::static_instances.find(entity_id);
                bool was_static                      = static_it != brixelizer_gi::static_instances.end();
                shared_ptr<Renderable> renderable    = entity->GetComponent<Renderable>();

                if (is_dynamic)
                {
                    if (renderable->HasInstancing())
                    {
                        for (uint32_t instance_index = 0; instance_index < renderable->GetInstanceCount(); instance_index++)
                        {
                            uint64_t instance_id = entity_id | (static_cast<uint64_t>(instance_index) << 32);
                            brixelizer_gi::instances_to_create.push_back(brixelizer_gi::create_instance_description(entity, instance_index));
                            
                            auto static_instance_it = brixelizer_gi::static_instances.find(instance_id);
                            if (static_instance_it != brixelizer_gi::static_instances.end())
                            {
                                brixelizer_gi::instances_to_delete.push_back(brixelizer_gi::get_or_create_id(instance_id));
                                brixelizer_gi::static_instances.erase(static_instance_it);
                                if (brixelizer_gi::debug_mode_log_instances)
                                {
                                    SP_LOG_INFO("Static instance became dynamic: %llu (instance %u)", entity_id, instance_index);
                                }
                            }
                        }
                    }
                    else
                    {
                        brixelizer_gi::instances_to_create.push_back(brixelizer_gi::create_instance_description(entity));
                        
                        if (was_static)
                        {
                            brixelizer_gi::instances_to_delete.push_back(brixelizer_gi::get_or_create_id(entity_id));
                            brixelizer_gi::static_instances.erase(static_it);
                            if (brixelizer_gi::debug_mode_log_instances)
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
                            brixelizer_gi::instances_to_create.push_back(brixelizer_gi::create_instance_description(entity, instance_index));
                            brixelizer_gi::static_instances.insert(instance_id);
                            if (brixelizer_gi::debug_mode_log_instances)
                            {
                                SP_LOG_INFO("Added new static instance: %llu (instance %u)", entity_id, instance_index);
                            }
                        }
                    }
                    else
                    {
                        brixelizer_gi::instances_to_create.push_back(brixelizer_gi::create_instance_description(entity));
                        brixelizer_gi::static_instances.insert(entity_id);
                        if (brixelizer_gi::debug_mode_log_instances)
                        {
                            SP_LOG_INFO("Added new static instance: %llu", entity_id);
                        }
                    }
                }
            }
        
            // delete static instances that no longer exist
            for (auto it = brixelizer_gi::static_instances.begin(); it != brixelizer_gi::static_instances.end();)
            {
                uint64_t entity_id = *it;
                if (brixelizer_gi::entity_map.find(entity_id) == brixelizer_gi::entity_map.end())
                {
                    brixelizer_gi::instances_to_delete.push_back(brixelizer_gi::get_or_create_id(entity_id));
                    it = brixelizer_gi::static_instances.erase(it);
                    if (brixelizer_gi::debug_mode_log_instances)
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
            if (!brixelizer_gi::instances_to_create.empty())
            {
                SP_ASSERT(ffxBrixelizerCreateInstances(&brixelizer_gi::context, brixelizer_gi::instances_to_create.data(), static_cast<uint32_t>(brixelizer_gi::instances_to_create.size())) == FFX_OK);
            }
        
            // delete instances
            if (!brixelizer_gi::instances_to_delete.empty())
            {
                SP_ASSERT(ffxBrixelizerDeleteInstances(&brixelizer_gi::context, brixelizer_gi::instances_to_delete.data(), static_cast<uint32_t>(brixelizer_gi::instances_to_delete.size())) == FFX_OK);
            }
        }

        // fill in the update description
        for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
        {
            brixelizer_gi::description_update.resources.cascadeResources[i].aabbTree = to_ffx_resource(brixelizer_gi::buffer_cascade_aabb_tree[i].get(), L"brixelizer_gi_abbb_tree");
            brixelizer_gi::description_update.resources.cascadeResources[i].brickMap = to_ffx_resource(brixelizer_gi::buffer_cascade_brick_map[i].get(), L"brixelizer_gi_brick_map");
        }
        brixelizer_gi::description_update.resources.sdfAtlas   = to_ffx_resource(brixelizer_gi::texture_sdf_atlas.get(),  L"brixelizer_gi_sdf_atlas");
        brixelizer_gi::description_update.resources.brickAABBs = to_ffx_resource(brixelizer_gi::buffer_brick_aabbs.get(), L"brixelizer_gi_brick_aabbs");
        brixelizer_gi::description_update.frameIndex           = cb_frame->frame;
        brixelizer_gi::description_update.maxReferences        = brixelizer_gi::triangle_references_max;
        brixelizer_gi::description_update.triangleSwapSize     = brixelizer_gi::triangle_swap_size;
        brixelizer_gi::description_update.maxBricksPerBake     = brixelizer_gi::bricks_per_update_max;
        size_t required_scratch_buffer_size                    = 0;
        brixelizer_gi::description_update.outScratchBufferSize = &required_scratch_buffer_size; // the size of the gpu scratch buffer needed for ffxBrixelizerUpdate()
        brixelizer_gi::description_update.outStats             = &brixelizer_gi::debug_stats;   // statistics for the update, stats read back after ffxBrixelizerUpdate()
        set_ffx_float3(brixelizer_gi::description_update.sdfCenter, brixelizer_gi::sdf_center_around_camera ? cb_frame->camera_position : Vector3::Zero); // sdf center in world space

        // debug visualization for: distance, uvw, iterations, brick id, cascade id
        bool debug_enabled = brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Max;
        bool debug_update  = brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Radiance && brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Irradiance;
        if (debug_enabled && debug_update)
        {
            FfxBrixelizerPopulateDebugAABBsFlags flags = static_cast<FfxBrixelizerPopulateDebugAABBsFlags>(FFX_BRIXELIZER_POPULATE_AABBS_INSTANCES | FFX_BRIXELIZER_POPULATE_AABBS_CASCADE_AABBS);

            for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
            {
                brixelizer_gi::debug_description.cascadeDebugAABB[0] = FFX_BRIXELIZER_CASCADE_DEBUG_AABB_NONE;
            }

            brixelizer_gi::description_update.populateDebugAABBsFlags = brixelizer_gi::debug_mode_aabbs_and_stats ? flags : FFX_BRIXELIZER_POPULATE_AABBS_NONE;
            brixelizer_gi::description_update.debugVisualizationDesc  = &brixelizer_gi::debug_description;
            brixelizer_gi::debug_description.commandList              = to_ffx_cmd_list(cmd_list);
            brixelizer_gi::debug_description.output                   = to_ffx_resource(tex_debug, L"brixelizer_gi_tex_debug");
            brixelizer_gi::debug_description.renderWidth              = tex_debug->GetWidth();
            brixelizer_gi::debug_description.renderHeight             = tex_debug->GetHeight();
            brixelizer_gi::debug_description.debugState               = brixelizer_gi::to_ffx_debug_mode(brixelizer_gi::debug_mode);
            brixelizer_gi::debug_description.startCascadeIndex        = brixelizer_gi::cascade_index_start;
            brixelizer_gi::debug_description.endCascadeIndex          = brixelizer_gi::cascade_index_end;
            brixelizer_gi::debug_description.tMin                     = brixelizer_gi::t_min;
            brixelizer_gi::debug_description.tMax                     = brixelizer_gi::t_max;
            brixelizer_gi::debug_description.sdfSolveEps              = brixelizer_gi::sdf_ray_epsilon;
           
            set_ffx_float16(brixelizer_gi::debug_description.inverseViewMatrix,       view_inverted);
            set_ffx_float16(brixelizer_gi::debug_description.inverseProjectionMatrix, projection_inverted);
        }
        
        // update
        SP_ASSERT(ffxBrixelizerBakeUpdate(&brixelizer_gi::context, &brixelizer_gi::description_update, &brixelizer_gi::description_update_baked) == FFX_OK);
        SP_ASSERT_MSG(required_scratch_buffer_size <= brixelizer_gi::buffer_scratch->GetObjectSize(), "Create a larger scratch buffer");
        SP_ASSERT(ffxBrixelizerUpdate(&brixelizer_gi::context, &brixelizer_gi::description_update_baked, to_ffx_resource(brixelizer_gi::buffer_scratch.get(), L"ffx_brixelizer_gi_scratch"), to_ffx_cmd_list(cmd_list)) == FFX_OK);
    #endif
    }

    void RHI_FidelityFX::BrixelizerGI_Dispatch(
        RHI_CommandList* cmd_list,
        Cb_Frame* cb_frame,
        RHI_Texture* tex_frame,
        RHI_Texture* tex_depth,
        RHI_Texture* tex_velocity,
        RHI_Texture* tex_normal,
        RHI_Texture* tex_material,
        RHI_Texture* tex_skybox,
        array<RHI_Texture*, 8>& tex_noise,
        RHI_Texture* tex_diffuse_gi,
        RHI_Texture* tex_specular_gi,
        RHI_Texture* tex_debug
    )
    {
    #ifdef _MSC_VER
        bool debug_enabled  = brixelizer_gi::debug_mode != brixelizer_gi::DebugMode::Max;
        bool debug_dispatch = brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Radiance || brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Irradiance;
        bool debug_update   = debug_enabled && !debug_dispatch;
        if (debug_update)
            return;

        // set camera matrices
        set_ffx_float16(brixelizer_gi::description_dispatch_gi.view,           view);
        set_ffx_float16(brixelizer_gi::description_dispatch_gi.prevView,       view_previous);
        set_ffx_float16(brixelizer_gi::description_dispatch_gi.projection,     projection);
        set_ffx_float16(brixelizer_gi::description_dispatch_gi.prevProjection, projection_previous);

        // set resources
        brixelizer_gi::description_dispatch_gi.environmentMap   = to_ffx_resource(tex_skybox,                                    L"brixelizer_environment");
        brixelizer_gi::description_dispatch_gi.prevLitOutput    = to_ffx_resource(tex_frame,                                     L"brixelizer_gi_lit_output_previous");
        brixelizer_gi::description_dispatch_gi.depth            = to_ffx_resource(tex_depth,                                     L"brixelizer_gi_depth");
        brixelizer_gi::description_dispatch_gi.historyDepth     = to_ffx_resource(brixelizer_gi::texture_depth_previous.get(),   L"brixelizer_gi_depth_previous");
        brixelizer_gi::description_dispatch_gi.normal           = to_ffx_resource(tex_normal,                                    L"brixelizer_gi_normal");
        brixelizer_gi::description_dispatch_gi.historyNormal    = to_ffx_resource(brixelizer_gi::texture_normal_previous.get(),  L"brixelizer_gi_normal_previous");
        brixelizer_gi::description_dispatch_gi.roughness        = to_ffx_resource(tex_material,                                  L"brixelizer_gi_roughness");
        brixelizer_gi::description_dispatch_gi.motionVectors    = to_ffx_resource(tex_velocity,                                  L"brixelizer_gi_velocity");
        brixelizer_gi::description_dispatch_gi.noiseTexture     = to_ffx_resource(tex_noise[cb_frame->frame % tex_noise.size()], L"brixelizer_gi_noise");
        brixelizer_gi::description_dispatch_gi.outputDiffuseGI  = to_ffx_resource(tex_diffuse_gi,                                L"brixelizer_gi_diffuse_gi");
        brixelizer_gi::description_dispatch_gi.outputSpecularGI = to_ffx_resource(tex_specular_gi,                               L"brixelizer_gi_specular_gi");
        brixelizer_gi::description_dispatch_gi.sdfAtlas         = to_ffx_resource(brixelizer_gi::texture_sdf_atlas.get(),        L"brixelizer_gi_sdf_atlas");
        brixelizer_gi::description_dispatch_gi.bricksAABBs      = to_ffx_resource(brixelizer_gi::buffer_brick_aabbs.get(),       L"brixelizer_gi_brick_aabbs");
        for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
        {
            brixelizer_gi::description_dispatch_gi.cascadeAABBTrees[i] = brixelizer_gi::description_update.resources.cascadeResources[i].aabbTree;
            brixelizer_gi::description_dispatch_gi.cascadeBrickMaps[i] = brixelizer_gi::description_update.resources.cascadeResources[i].brickMap;
        }
        
        // set parameters
        brixelizer_gi::description_dispatch_gi.startCascade            = brixelizer_gi::cascade_index_start;
        brixelizer_gi::description_dispatch_gi.endCascade              = brixelizer_gi::cascade_index_end;
        brixelizer_gi::description_dispatch_gi.rayPushoff              = brixelizer_gi::sdf_ray_normal_offset;
        brixelizer_gi::description_dispatch_gi.sdfSolveEps             = brixelizer_gi::sdf_ray_epsilon;
        brixelizer_gi::description_dispatch_gi.specularRayPushoff      = brixelizer_gi::sdf_ray_normal_offset;
        brixelizer_gi::description_dispatch_gi.specularSDFSolveEps     = brixelizer_gi::sdf_ray_epsilon;
        brixelizer_gi::description_dispatch_gi.tMin                    = brixelizer_gi::t_min;
        brixelizer_gi::description_dispatch_gi.tMax                    = brixelizer_gi::t_max;
        brixelizer_gi::description_dispatch_gi.normalsUnpackMul        = 1.0f;                            // a multiply factor to transform the normal to the space expected by brixelizer gi
        brixelizer_gi::description_dispatch_gi.normalsUnpackAdd        = 0.0f;                            // an offset to transform the normal to the space expected by brixelizer gi
        brixelizer_gi::description_dispatch_gi.isRoughnessPerceptual   = true;                            // if false, we assume roughness squared was stored in the Gbuffer
        brixelizer_gi::description_dispatch_gi.roughnessChannel        = 0;                               // the channel to read the roughness from the roughness texture
        brixelizer_gi::description_dispatch_gi.roughnessThreshold      = 1.0f;                            // regions with a roughness value greater than this threshold won't spawn specular rays
        brixelizer_gi::description_dispatch_gi.environmentMapIntensity = 0.0f;                            // value to scale the contribution from the environment map
        brixelizer_gi::description_dispatch_gi.motionVectorScale.x     = -1.0f;
        brixelizer_gi::description_dispatch_gi.motionVectorScale.y     = -1.0f;
        set_ffx_float3(brixelizer_gi::description_dispatch_gi.cameraPosition, cb_frame->camera_position); // camera position

        // dispatch
        SP_ASSERT(ffxBrixelizerGetRawContext(&brixelizer_gi::context, &brixelizer_gi::description_dispatch_gi.brixelizerContext) == FFX_OK);
        SP_ASSERT(ffxBrixelizerGIContextDispatch(&brixelizer_gi::context_gi, &brixelizer_gi::description_dispatch_gi, to_ffx_cmd_list(cmd_list)) == FFX_OK);

        // blit the depth and the normal so that we can use them in the next frame as "history"
        cmd_list->Blit(tex_depth,  brixelizer_gi::texture_depth_previous.get(),  false);
        cmd_list->Blit(tex_normal, brixelizer_gi::texture_normal_previous.get(), false);

        // debug visualisation
        if (brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Radiance || brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Irradiance)
        {
            // set camera matrices
            set_ffx_float16(brixelizer_gi::debug_description_gi.view,       view);
            set_ffx_float16(brixelizer_gi::debug_description_gi.projection, projection);

            // set resources
            brixelizer_gi::debug_description_gi.outputDebug   = to_ffx_resource(tex_debug, L"brixelizer_gi_debug");
            brixelizer_gi::debug_description_gi.outputSize[0] = tex_debug->GetWidth();
            brixelizer_gi::debug_description_gi.outputSize[1] = tex_debug->GetHeight();
            brixelizer_gi::debug_description_gi.depth         = brixelizer_gi::description_dispatch_gi.depth;
            brixelizer_gi::debug_description_gi.normal        = brixelizer_gi::description_dispatch_gi.normal;
            brixelizer_gi::debug_description_gi.sdfAtlas      = brixelizer_gi::description_dispatch_gi.sdfAtlas;
            brixelizer_gi::debug_description_gi.bricksAABBs   = brixelizer_gi::description_dispatch_gi.bricksAABBs;
            for (uint32_t i = 0; i < FFX_BRIXELIZER_MAX_CASCADES; i++)
            {
                brixelizer_gi::debug_description_gi.cascadeAABBTrees[i] = brixelizer_gi::description_dispatch_gi.cascadeAABBTrees[i];
                brixelizer_gi::debug_description_gi.cascadeBrickMaps[i] = brixelizer_gi::description_dispatch_gi.cascadeBrickMaps[i];
            }

            // set parameters
            brixelizer_gi::debug_description_gi.startCascade     = brixelizer_gi::description_dispatch_gi.startCascade;
            brixelizer_gi::debug_description_gi.endCascade       = brixelizer_gi::description_dispatch_gi.endCascade;
            brixelizer_gi::debug_description_gi.debugMode        = brixelizer_gi::debug_mode == brixelizer_gi::DebugMode::Radiance ? FFX_BRIXELIZER_GI_DEBUG_MODE_RADIANCE_CACHE : FFX_BRIXELIZER_GI_DEBUG_MODE_IRRADIANCE_CACHE;
            brixelizer_gi::debug_description_gi.normalsUnpackMul = brixelizer_gi::description_dispatch_gi.normalsUnpackMul;
            brixelizer_gi::debug_description_gi.normalsUnpackAdd = brixelizer_gi::description_dispatch_gi.normalsUnpackAdd;

            // dispatch
            brixelizer_gi::debug_description_gi.brixelizerContext = brixelizer_gi::description_dispatch_gi.brixelizerContext;
            SP_ASSERT(ffxBrixelizerGIContextDebugVisualization(&brixelizer_gi::context_gi, &brixelizer_gi::debug_description_gi, to_ffx_cmd_list(cmd_list)) == FFX_OK);
        }
    #endif
    }

    void RHI_FidelityFX::Breadcrumbs_RegisterCommandList(RHI_CommandList* cmd_list, const RHI_Queue* queue, const char* name)
    {
        SP_ASSERT(Debugging::IsBreadcrumbsEnabled());

        // during engine startup this can happen, this is from immediate command lists
        // that are used to initialize certain resources, we don't track them
        if (!breadcrumbs::context_created)
            return;

        FfxBreadcrumbsCommandListDescription description = {};
        description.commandList                          = to_ffx_cmd_list(cmd_list);
        description.queueType                            = RHI_Device::GetQueueIndex(queue->GetType());
        description.name                                 = { name, true};
        description.pipeline                             = nullptr;
        description.submissionIndex                      = 0;

        SP_ASSERT(ffxBreadcrumbsRegisterCommandList(&breadcrumbs::context, &description) == FFX_OK);
    }

    void RHI_FidelityFX::Breadcrumbs_RegisterPipeline(RHI_Pipeline* pipeline)
    {
        SP_ASSERT(Debugging::IsBreadcrumbsEnabled());

        FfxBreadcrumbsPipelineStateDescription description = {};
        description.pipeline                               = to_ffx_pipeline(pipeline);

        RHI_PipelineState* pso = pipeline->GetState();
        description.name       = { pso->name.c_str(), true};

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

        SP_ASSERT(ffxBreadcrumbsRegisterPipeline(&breadcrumbs::context, &description) == FFX_OK);
    }

    void RHI_FidelityFX::Breadcrumbs_SetPipelineState(RHI_CommandList* cmd_list, RHI_Pipeline* pipeline)
    {
        SP_ASSERT(Debugging::IsBreadcrumbsEnabled());
        SP_ASSERT(ffxBreadcrumbsSetPipeline(&breadcrumbs::context, to_ffx_cmd_list(cmd_list), to_ffx_pipeline(pipeline)) == FFX_OK);
    }

    void RHI_FidelityFX::Breadcrumbs_MarkerBegin(RHI_CommandList* cmd_list, const char* name)
    {
        SP_ASSERT(Debugging::IsBreadcrumbsEnabled());

        // requires: VK_KHR_synchronization2 because of vkCmdWriteBufferMarkerAMD and vkCmdWriteBufferMarker2AMD 
 
        const FfxBreadcrumbsNameTag name_tag = { name, true };
        SP_ASSERT(ffxBreadcrumbsBeginMarker(&breadcrumbs::context, to_ffx_cmd_list(cmd_list), FFX_BREADCRUMBS_MARKER_BEGIN_RENDER_PASS, &name_tag) == FFX_OK);
    }

    void RHI_FidelityFX::Breadcrumbs_MarkerEnd(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(Debugging::IsBreadcrumbsEnabled());
        SP_ASSERT(ffxBreadcrumbsEndMarker(&breadcrumbs::context, to_ffx_cmd_list(cmd_list)) == FFX_OK);
    }
}
