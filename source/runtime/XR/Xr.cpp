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

//= INCLUDES ===========================
#include "pch.h"
#include "Xr.h"
#include "../RHI/RHI_Implementation.h"
#include "../RHI/RHI_Device.h"
#if defined(API_GRAPHICS_VULKAN)
    #define XR_USE_GRAPHICS_API_VULKAN
    #include <openxr/openxr.h>
    #include <openxr/openxr_platform.h>
#endif
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // static member definitions
    bool Xr::m_initialized          = false;
    bool Xr::m_hmd_connected        = false;
    bool Xr::m_session_running      = false;
    bool Xr::m_session_focused      = false;
    bool Xr::m_frame_began          = false;
    string Xr::m_runtime_name       = "N/A";
    string Xr::m_device_name        = "N/A";
    uint32_t Xr::m_recommended_width  = 0;
    uint32_t Xr::m_recommended_height = 0;
    array<XrEyeView, Xr::eye_count> Xr::m_eye_views;
    math::Vector3 Xr::m_head_position         = math::Vector3::Zero;
    math::Quaternion Xr::m_head_orientation   = math::Quaternion::Identity;

#if defined(API_GRAPHICS_VULKAN)
    // openxr state (vulkan implementation)
    namespace
    {
        XrInstance xr_instance                 = XR_NULL_HANDLE;
        XrSystemId xr_system_id                = XR_NULL_SYSTEM_ID;
        XrSession xr_session                   = XR_NULL_HANDLE;
        XrSpace xr_reference_space             = XR_NULL_HANDLE;
        XrSwapchain xr_swapchain               = XR_NULL_HANDLE;
        XrSessionState xr_session_state        = XR_SESSION_STATE_UNKNOWN;
        XrFrameState xr_frame_state            = {};
        XrTime xr_predicted_display_time       = 0;
        bool xr_session_state_changed          = false;

        // swapchain state
        uint32_t swapchain_width               = 0;
        uint32_t swapchain_height              = 0;
        uint32_t swapchain_length              = 0;
        uint32_t swapchain_image_index         = 0;
        vector<XrSwapchainImageVulkanKHR> swapchain_images;
        vector<VkImageView> swapchain_image_views;

        // views for stereo rendering
        vector<XrView> xr_views;
        vector<XrViewConfigurationView> xr_view_configs;
        XrViewState xr_view_state = {};

        // function pointers (loaded at runtime)
        PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
        PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
        PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
        PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;

        // helper to check xr results
        bool xr_check(XrResult result, const char* operation)
        {
            if (XR_FAILED(result))
            {
                char result_string[XR_MAX_RESULT_STRING_SIZE];
                xrResultToString(xr_instance, result, result_string);
                SP_LOG_ERROR("openxr %s failed: %s", operation, result_string);
                return false;
            }
            return true;
        }

        // convert openxr pose to engine matrices
        void xr_pose_to_matrix(const XrPosef& pose, math::Matrix& out_view, math::Vector3& out_position, math::Quaternion& out_orientation)
        {
            // openxr coordinate system: +x right, +y up, -z forward
            out_position = math::Vector3(pose.position.x, pose.position.y, pose.position.z);
            out_orientation = math::Quaternion(pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);

            // create view matrix (inverse of pose transform)
            math::Matrix rotation = math::Matrix::CreateRotation(out_orientation);
            math::Matrix translation = math::Matrix::CreateTranslation(out_position);
            math::Matrix transform = rotation * translation;
            out_view = transform.Inverted();
        }

        // create asymmetric projection matrix from fov
        math::Matrix create_projection_matrix(float fov_left, float fov_right, float fov_up, float fov_down, float near_z, float far_z)
        {
            // convert fov angles to tangents
            float tan_left  = tanf(fov_left);
            float tan_right = tanf(fov_right);
            float tan_up    = tanf(fov_up);
            float tan_down  = tanf(fov_down);

            float tan_width  = tan_right - tan_left;
            float tan_height = tan_up - tan_down;

            // create asymmetric projection (vulkan style with inverted y and 0-1 depth)
            math::Matrix projection;
            projection.m00 = 2.0f / tan_width;
            projection.m11 = 2.0f / tan_height; // vulkan: flip y if needed
            projection.m02 = (tan_right + tan_left) / tan_width;
            projection.m12 = (tan_up + tan_down) / tan_height;
            projection.m22 = far_z / (near_z - far_z);
            projection.m32 = -1.0f;
            projection.m23 = (near_z * far_z) / (near_z - far_z);
            projection.m33 = 0.0f;

            // zero out unused elements
            projection.m01 = projection.m03 = 0.0f;
            projection.m10 = projection.m13 = 0.0f;
            projection.m20 = projection.m21 = 0.0f;
            projection.m30 = projection.m31 = 0.0f;

            return projection;
        }
    }
#endif // API_GRAPHICS_VULKAN

#if defined(API_GRAPHICS_VULKAN)

    void Xr::Initialize()
    {
        if (m_initialized)
            return;

        // initialize eye views
        for (auto& eye : m_eye_views)
        {
            eye.view       = math::Matrix::Identity;
            eye.projection = math::Matrix::Identity;
            eye.position   = math::Vector3::Zero;
            eye.orientation = math::Quaternion::Identity;
        }

        // create openxr instance with vulkan extension
        {
            // check for extension support
            uint32_t extension_count = 0;
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
            vector<XrExtensionProperties> extensions(extension_count, { XR_TYPE_EXTENSION_PROPERTIES });
            xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, extensions.data());

            bool vulkan_supported = false;
            for (const auto& ext : extensions)
            {
                if (strcmp(ext.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0)
                {
                    vulkan_supported = true;
                    break;
                }
            }

            if (!vulkan_supported)
            {
                SP_LOG_WARNING("openxr: vulkan extension not supported by runtime");
                m_initialized = true;
                return;
            }

            // create instance
            const char* enabled_extensions[] = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };

            XrApplicationInfo app_info  = {};
            app_info.apiVersion         = XR_API_VERSION_1_0;
            strcpy_s(app_info.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Spartan Engine");
            strcpy_s(app_info.engineName, XR_MAX_ENGINE_NAME_SIZE, "Spartan");
            app_info.applicationVersion = 1;
            app_info.engineVersion      = 1;

            XrInstanceCreateInfo create_info    = { XR_TYPE_INSTANCE_CREATE_INFO };
            create_info.applicationInfo         = app_info;
            create_info.enabledExtensionCount   = 1;
            create_info.enabledExtensionNames   = enabled_extensions;

            XrResult result = xrCreateInstance(&create_info, &xr_instance);
            if (XR_FAILED(result))
            {
                SP_LOG_WARNING("openxr: no runtime found or failed to create instance");
                m_initialized = true;
                return;
            }
        }

        // get runtime info
        {
            XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
            if (XR_SUCCEEDED(xrGetInstanceProperties(xr_instance, &instance_props)))
            {
                m_runtime_name = instance_props.runtimeName;
                SP_LOG_INFO("openxr runtime: %s", m_runtime_name.c_str());
            }
        }

        // get function pointers
        {
            xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsRequirementsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsRequirementsKHR));
            xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsDeviceKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsDeviceKHR));
            xrGetInstanceProcAddr(xr_instance, "xrGetVulkanInstanceExtensionsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanInstanceExtensionsKHR));
            xrGetInstanceProcAddr(xr_instance, "xrGetVulkanDeviceExtensionsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanDeviceExtensionsKHR));
        }

        // get hmd system
        {
            XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
            system_info.formFactor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

            XrResult result = xrGetSystem(xr_instance, &system_info, &xr_system_id);
            m_hmd_connected = XR_SUCCEEDED(result);
        }

        // get hmd properties and view configuration
        if (m_hmd_connected)
        {
            // system properties
            XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };
            if (XR_SUCCEEDED(xrGetSystemProperties(xr_instance, xr_system_id, &system_props)))
            {
                m_device_name = system_props.systemName;
            }

            // view configuration (resolution per eye)
            uint32_t view_count = 0;
            xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr);
            if (view_count > 0)
            {
                xr_view_configs.resize(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
                xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count, xr_view_configs.data());

                m_recommended_width  = xr_view_configs[0].recommendedImageRectWidth;
                m_recommended_height = xr_view_configs[0].recommendedImageRectHeight;

                // prepare views array
                xr_views.resize(view_count, { XR_TYPE_VIEW });
            }

            // check vulkan graphics requirements (required before session creation)
            if (xrGetVulkanGraphicsRequirementsKHR)
            {
                XrGraphicsRequirementsVulkanKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
                xrGetVulkanGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirements);
            }

            SP_LOG_INFO("openxr hmd: %s (%ux%u per eye)", m_device_name.c_str(), m_recommended_width, m_recommended_height);

            // create session now that rhi is initialized
            if (!CreateSession())
            {
                SP_LOG_ERROR("openxr: failed to create session");
            }
        }

        m_initialized = true;
    }

    void Xr::Shutdown()
    {
        if (!m_initialized)
            return;

        DestroySwapchain();
        DestroySession();

        if (xr_instance != XR_NULL_HANDLE)
        {
            xrDestroyInstance(xr_instance);
            xr_instance = XR_NULL_HANDLE;
        }

        xr_system_id         = XR_NULL_SYSTEM_ID;
        m_hmd_connected      = false;
        m_session_running    = false;
        m_session_focused    = false;
        m_runtime_name       = "N/A";
        m_device_name        = "N/A";
        m_recommended_width  = 0;
        m_recommended_height = 0;
        m_initialized        = false;
    }

    void Xr::Tick()
    {
        if (!m_initialized || !m_hmd_connected)
            return;

        ProcessEvents();
    }

    bool Xr::CreateSession()
    {
        if (xr_session != XR_NULL_HANDLE)
            return true;

        if (!RHI_Context::device || !RHI_Context::device_physical || !RHI_Context::instance)
        {
            SP_LOG_ERROR("openxr: vulkan context not ready");
            return false;
        }

        // log required vulkan instance extensions
        if (xrGetVulkanInstanceExtensionsKHR)
        {
            uint32_t buffer_size = 0;
            xrGetVulkanInstanceExtensionsKHR(xr_instance, xr_system_id, 0, &buffer_size, nullptr);
            if (buffer_size > 0)
            {
                string extensions(buffer_size, '\0');
                xrGetVulkanInstanceExtensionsKHR(xr_instance, xr_system_id, buffer_size, &buffer_size, extensions.data());
                SP_LOG_INFO("openxr: required instance extensions: %s", extensions.c_str());
            }
        }

        // log required vulkan device extensions
        if (xrGetVulkanDeviceExtensionsKHR)
        {
            uint32_t buffer_size = 0;
            xrGetVulkanDeviceExtensionsKHR(xr_instance, xr_system_id, 0, &buffer_size, nullptr);
            if (buffer_size > 0)
            {
                string extensions(buffer_size, '\0');
                xrGetVulkanDeviceExtensionsKHR(xr_instance, xr_system_id, buffer_size, &buffer_size, extensions.data());
                SP_LOG_INFO("openxr: required device extensions: %s", extensions.c_str());
            }
        }

        // get graphics requirements (required before session creation)
        if (xrGetVulkanGraphicsRequirementsKHR)
        {
            XrGraphicsRequirementsVulkanKHR requirements = {};
            requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
            XrResult req_result = xrGetVulkanGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirements);
            if (XR_FAILED(req_result))
            {
                SP_LOG_ERROR("openxr: failed to get vulkan graphics requirements");
                return false;
            }

            SP_LOG_INFO("openxr: vulkan requirements - min: %u.%u.%u, max: %u.%u.%u",
                XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                XR_VERSION_MINOR(requirements.minApiVersionSupported),
                XR_VERSION_PATCH(requirements.minApiVersionSupported),
                XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                XR_VERSION_MINOR(requirements.maxApiVersionSupported),
                XR_VERSION_PATCH(requirements.maxApiVersionSupported));
        }

        // get the physical device openxr wants to use
        VkPhysicalDevice xr_physical_device = VK_NULL_HANDLE;
        if (xrGetVulkanGraphicsDeviceKHR)
        {
            XrResult result = xrGetVulkanGraphicsDeviceKHR(xr_instance, xr_system_id, RHI_Context::instance, &xr_physical_device);
            if (XR_FAILED(result))
            {
                SP_LOG_WARNING("openxr: failed to get vulkan graphics device, using engine's device");
                xr_physical_device = RHI_Context::device_physical;
            }
            else if (xr_physical_device != RHI_Context::device_physical)
            {
                SP_LOG_WARNING("openxr: physical device mismatch - xr wants different gpu, this may cause issues");
            }
        }
        else
        {
            xr_physical_device = RHI_Context::device_physical;
        }

        // create session with vulkan binding
        XrGraphicsBindingVulkanKHR graphics_binding = {};
        graphics_binding.type             = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
        graphics_binding.next             = nullptr;
        graphics_binding.instance         = RHI_Context::instance;
        graphics_binding.physicalDevice   = xr_physical_device;
        graphics_binding.device           = RHI_Context::device;
        graphics_binding.queueFamilyIndex = RHI_Device::GetQueueIndex(RHI_Queue_Type::Graphics);
        graphics_binding.queueIndex       = 0;

        SP_LOG_INFO("openxr: creating session with queue family %u", graphics_binding.queueFamilyIndex);

        XrSessionCreateInfo session_info = {};
        session_info.type     = XR_TYPE_SESSION_CREATE_INFO;
        session_info.next     = &graphics_binding;
        session_info.systemId = xr_system_id;

        if (!xr_check(xrCreateSession(xr_instance, &session_info, &xr_session), "create session"))
            return false;

        // create reference space (local = seated, stage = standing)
        if (!CreateReferenceSpace())
            return false;

        // create swapchain
        if (!CreateSwapchain())
            return false;

        SP_LOG_INFO("openxr: session created successfully");
        return true;
    }

    void Xr::DestroySession()
    {
        if (xr_reference_space != XR_NULL_HANDLE)
        {
            xrDestroySpace(xr_reference_space);
            xr_reference_space = XR_NULL_HANDLE;
        }

        if (xr_session != XR_NULL_HANDLE)
        {
            xrDestroySession(xr_session);
            xr_session = XR_NULL_HANDLE;
        }

        m_session_running = false;
        m_session_focused = false;
        xr_session_state  = XR_SESSION_STATE_UNKNOWN;
    }

    bool Xr::CreateSwapchain()
    {
        if (xr_swapchain != XR_NULL_HANDLE)
            return true;

        if (xr_session == XR_NULL_HANDLE || xr_view_configs.empty())
            return false;

        // query supported formats
        uint32_t format_count = 0;
        xrEnumerateSwapchainFormats(xr_session, 0, &format_count, nullptr);
        vector<int64_t> formats(format_count);
        xrEnumerateSwapchainFormats(xr_session, format_count, &format_count, formats.data());

        // prefer srgb format for correct gamma
        int64_t selected_format = VK_FORMAT_R8G8B8A8_SRGB;
        bool format_found = false;
        for (int64_t format : formats)
        {
            if (format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB)
            {
                selected_format = format;
                format_found = true;
                break;
            }
        }
        if (!format_found && !formats.empty())
        {
            // fallback to first available
            selected_format = formats[0];
        }

        swapchain_width  = m_recommended_width;
        swapchain_height = m_recommended_height;

        // create swapchain as array texture for multiview (2 layers = 2 eyes)
        XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapchain_info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapchain_info.format      = selected_format;
        swapchain_info.sampleCount = 1;
        swapchain_info.width       = swapchain_width;
        swapchain_info.height      = swapchain_height;
        swapchain_info.faceCount   = 1;
        swapchain_info.arraySize   = eye_count; // multiview: 2 layers for stereo
        swapchain_info.mipCount    = 1;

        if (!xr_check(xrCreateSwapchain(xr_session, &swapchain_info, &xr_swapchain), "create swapchain"))
            return false;

        // get swapchain images
        xrEnumerateSwapchainImages(xr_swapchain, 0, &swapchain_length, nullptr);
        swapchain_images.resize(swapchain_length, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
        xrEnumerateSwapchainImages(xr_swapchain, swapchain_length, &swapchain_length,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain_images.data()));

        // create image views for each swapchain image (2d array view for multiview)
        swapchain_image_views.resize(swapchain_length);
        for (uint32_t i = 0; i < swapchain_length; i++)
        {
            VkImageViewCreateInfo view_info = {};
            view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image            = swapchain_images[i].image;
            view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            view_info.format           = static_cast<VkFormat>(selected_format);
            view_info.components       = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel   = 0;
            view_info.subresourceRange.levelCount     = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount     = eye_count;

            if (vkCreateImageView(RHI_Context::device, &view_info, nullptr, &swapchain_image_views[i]) != VK_SUCCESS)
            {
                SP_LOG_ERROR("openxr: failed to create swapchain image view");
                return false;
            }
        }

        SP_LOG_INFO("openxr: swapchain created (%ux%u, %u images, multiview array)", swapchain_width, swapchain_height, swapchain_length);
        return true;
    }

    void Xr::DestroySwapchain()
    {
        // destroy image views
        for (VkImageView& view : swapchain_image_views)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(RHI_Context::device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }
        swapchain_image_views.clear();
        swapchain_images.clear();

        if (xr_swapchain != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(xr_swapchain);
            xr_swapchain = XR_NULL_HANDLE;
        }

        swapchain_length = 0;
    }

    bool Xr::CreateReferenceSpace()
    {
        // try stage space first (standing), fall back to local (seated)
        XrReferenceSpaceType space_type = XR_REFERENCE_SPACE_TYPE_STAGE;

        uint32_t space_count = 0;
        xrEnumerateReferenceSpaces(xr_session, 0, &space_count, nullptr);
        vector<XrReferenceSpaceType> spaces(space_count);
        xrEnumerateReferenceSpaces(xr_session, space_count, &space_count, spaces.data());

        bool stage_supported = false;
        for (XrReferenceSpaceType space : spaces)
        {
            if (space == XR_REFERENCE_SPACE_TYPE_STAGE)
            {
                stage_supported = true;
                break;
            }
        }

        if (!stage_supported)
        {
            space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
            SP_LOG_INFO("openxr: using local (seated) reference space");
        }
        else
        {
            SP_LOG_INFO("openxr: using stage (standing) reference space");
        }

        XrPosef identity_pose = {};
        identity_pose.orientation.w = 1.0f;

        XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        space_info.referenceSpaceType = space_type;
        space_info.poseInReferenceSpace = identity_pose;

        return xr_check(xrCreateReferenceSpace(xr_session, &space_info, &xr_reference_space), "create reference space");
    }

    void Xr::ProcessEvents()
    {
        XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };

        while (xrPollEvent(xr_instance, &event_buffer) == XR_SUCCESS)
        {
            switch (event_buffer.type)
            {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                {
                    auto* state_event = reinterpret_cast<XrEventDataSessionStateChanged*>(&event_buffer);
                    xr_session_state = state_event->state;
                    xr_session_state_changed = true;

                    switch (xr_session_state)
                    {
                        case XR_SESSION_STATE_READY:
                        {
                            // begin session
                            XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
                            begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                            if (xr_check(xrBeginSession(xr_session, &begin_info), "begin session"))
                            {
                                m_session_running = true;
                                SP_LOG_INFO("openxr: session started");
                            }
                            break;
                        }
                        case XR_SESSION_STATE_STOPPING:
                        {
                            m_session_running = false;
                            m_session_focused = false;
                            xrEndSession(xr_session);
                            SP_LOG_INFO("openxr: session stopped");
                            break;
                        }
                        case XR_SESSION_STATE_FOCUSED:
                        {
                            m_session_focused = true;
                            break;
                        }
                        case XR_SESSION_STATE_VISIBLE:
                        {
                            m_session_focused = false;
                            break;
                        }
                        case XR_SESSION_STATE_EXITING:
                        case XR_SESSION_STATE_LOSS_PENDING:
                        {
                            m_session_running = false;
                            m_session_focused = false;
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                {
                    SP_LOG_WARNING("openxr: instance loss pending");
                    m_session_running = false;
                    break;
                }
                default:
                    break;
            }

            event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
        }
    }

    void Xr::UpdateViews()
    {
        if (xr_session == XR_NULL_HANDLE || xr_reference_space == XR_NULL_HANDLE)
            return;

        XrViewLocateInfo view_locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
        view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        view_locate_info.displayTime = xr_predicted_display_time;
        view_locate_info.space = xr_reference_space;

        xr_view_state = { XR_TYPE_VIEW_STATE };
        uint32_t view_count = 0;

        XrResult result = xrLocateViews(xr_session, &view_locate_info, &xr_view_state,
            static_cast<uint32_t>(xr_views.size()), &view_count, xr_views.data());

        if (XR_FAILED(result) || view_count < eye_count)
            return;

        // check if pose is valid
        bool pose_valid = (xr_view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                          (xr_view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

        if (!pose_valid)
            return;

        // calculate head position (average of both eyes)
        XrVector3f head_pos = {
            (xr_views[0].pose.position.x + xr_views[1].pose.position.x) * 0.5f,
            (xr_views[0].pose.position.y + xr_views[1].pose.position.y) * 0.5f,
            (xr_views[0].pose.position.z + xr_views[1].pose.position.z) * 0.5f
        };
        m_head_position = math::Vector3(head_pos.x, head_pos.y, head_pos.z);

        // use first eye orientation as head orientation (they're nearly identical)
        m_head_orientation = math::Quaternion(
            xr_views[0].pose.orientation.x,
            xr_views[0].pose.orientation.y,
            xr_views[0].pose.orientation.z,
            xr_views[0].pose.orientation.w
        );

        // update per-eye data
        for (uint32_t i = 0; i < eye_count; i++)
        {
            const XrView& view = xr_views[i];

            // convert pose to view matrix
            xr_pose_to_matrix(view.pose, m_eye_views[i].view, m_eye_views[i].position, m_eye_views[i].orientation);

            // store fov angles
            m_eye_views[i].fov_left  = view.fov.angleLeft;
            m_eye_views[i].fov_right = view.fov.angleRight;
            m_eye_views[i].fov_up    = view.fov.angleUp;
            m_eye_views[i].fov_down  = view.fov.angleDown;

            // create projection matrix
            float near_z = 0.1f;
            float far_z  = 1000.0f;
            m_eye_views[i].projection = create_projection_matrix(
                view.fov.angleLeft, view.fov.angleRight,
                view.fov.angleUp, view.fov.angleDown,
                near_z, far_z
            );
        }
    }

    bool Xr::BeginFrame()
    {
        if (!m_session_running || xr_session == XR_NULL_HANDLE)
            return false;

        // wait for frame
        xr_frame_state = { XR_TYPE_FRAME_STATE };
        if (!xr_check(xrWaitFrame(xr_session, nullptr, &xr_frame_state), "wait frame"))
            return false;

        // begin frame
        if (!xr_check(xrBeginFrame(xr_session, nullptr), "begin frame"))
            return false;

        m_frame_began = true;
        xr_predicted_display_time = xr_frame_state.predictedDisplayTime;

        // update view poses
        UpdateViews();

        // should we render this frame?
        return xr_frame_state.shouldRender == XR_TRUE;
    }

    void Xr::EndFrame()
    {
        if (!m_frame_began || xr_session == XR_NULL_HANDLE)
            return;

        // prepare projection views for submission
        array<XrCompositionLayerProjectionView, eye_count> projection_views;
        for (uint32_t i = 0; i < eye_count; i++)
        {
            projection_views[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projection_views[i].pose = xr_views[i].pose;
            projection_views[i].fov  = xr_views[i].fov;
            projection_views[i].subImage.swapchain = xr_swapchain;
            projection_views[i].subImage.imageRect.offset = { 0, 0 };
            projection_views[i].subImage.imageRect.extent = { static_cast<int32_t>(swapchain_width), static_cast<int32_t>(swapchain_height) };
            projection_views[i].subImage.imageArrayIndex = i; // layer 0 = left eye, layer 1 = right eye
        }

        XrCompositionLayerProjection projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        projection_layer.space     = xr_reference_space;
        projection_layer.viewCount = eye_count;
        projection_layer.views     = projection_views.data();

        const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer) };

        XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
        end_info.displayTime          = xr_predicted_display_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount           = xr_frame_state.shouldRender ? 1 : 0;
        end_info.layers               = xr_frame_state.shouldRender ? layers : nullptr;

        xr_check(xrEndFrame(xr_session, &end_info), "end frame");
        m_frame_began = false;
    }

    bool Xr::AcquireSwapchainImage()
    {
        if (xr_swapchain == XR_NULL_HANDLE)
            return false;

        XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (!xr_check(xrAcquireSwapchainImage(xr_swapchain, &acquire_info, &swapchain_image_index), "acquire swapchain image"))
            return false;

        XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait_info.timeout = XR_INFINITE_DURATION;
        if (!xr_check(xrWaitSwapchainImage(xr_swapchain, &wait_info), "wait swapchain image"))
            return false;

        return true;
    }

    void Xr::ReleaseSwapchainImage()
    {
        if (xr_swapchain == XR_NULL_HANDLE)
            return;

        XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xr_check(xrReleaseSwapchainImage(xr_swapchain, &release_info), "release swapchain image");
    }

    bool Xr::IsAvailable()
    {
        return m_initialized && xr_instance != XR_NULL_HANDLE;
    }

    void* Xr::GetSwapchainImage()
    {
        if (swapchain_images.empty() || swapchain_image_index >= swapchain_images.size())
            return nullptr;
        return swapchain_images[swapchain_image_index].image;
    }

    void* Xr::GetSwapchainImageView()
    {
        if (swapchain_image_views.empty() || swapchain_image_index >= swapchain_image_views.size())
            return nullptr;
        return swapchain_image_views[swapchain_image_index];
    }

    uint32_t Xr::GetSwapchainImageIndex()
    {
        return swapchain_image_index;
    }

    uint32_t Xr::GetSwapchainLength()
    {
        return swapchain_length;
    }

    bool Xr::IsMultiviewSupported()
    {
        // multiview is supported if we successfully created an array swapchain
        return xr_swapchain != XR_NULL_HANDLE && swapchain_length > 0;
    }

#else // d3d12 or other - xr not yet implemented

    void Xr::Initialize()
    {
        // xr not implemented for this graphics api
        m_initialized = true;
    }

    void Xr::Shutdown()
    {
        m_initialized = false;
    }

    void Xr::Tick()
    {
        // no-op
    }

    bool Xr::CreateSession()
    {
        return false;
    }

    void Xr::DestroySession()
    {
        // no-op
    }

    bool Xr::CreateSwapchain()
    {
        return false;
    }

    void Xr::DestroySwapchain()
    {
        // no-op
    }

    bool Xr::CreateReferenceSpace()
    {
        return false;
    }

    void Xr::ProcessEvents()
    {
        // no-op
    }

    void Xr::UpdateViews()
    {
        // no-op
    }

    bool Xr::BeginFrame()
    {
        return false;
    }

    void Xr::EndFrame()
    {
        // no-op
    }

    bool Xr::AcquireSwapchainImage()
    {
        return false;
    }

    void Xr::ReleaseSwapchainImage()
    {
        // no-op
    }

    bool Xr::IsAvailable()
    {
        return false;
    }

    void* Xr::GetSwapchainImage()
    {
        return nullptr;
    }

    void* Xr::GetSwapchainImageView()
    {
        return nullptr;
    }

    uint32_t Xr::GetSwapchainImageIndex()
    {
        return 0;
    }

    uint32_t Xr::GetSwapchainLength()
    {
        return 0;
    }

    bool Xr::IsMultiviewSupported()
    {
        return false;
    }

#endif // API_GRAPHICS_VULKAN

    // api-agnostic accessor implementations (these don't touch any vulkan types)
    bool Xr::IsHmdConnected()
    {
        return m_hmd_connected;
    }

    bool Xr::IsSessionRunning()
    {
        return m_session_running;
    }

    bool Xr::IsSessionFocused()
    {
        return m_session_focused;
    }

    const string& Xr::GetRuntimeName()
    {
        return m_runtime_name;
    }

    const string& Xr::GetDeviceName()
    {
        return m_device_name;
    }

    uint32_t Xr::GetRecommendedWidth()
    {
        return m_recommended_width;
    }

    uint32_t Xr::GetRecommendedHeight()
    {
        return m_recommended_height;
    }

    const XrEyeView& Xr::GetEyeView(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index];
    }

    const math::Matrix& Xr::GetViewMatrix(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index].view;
    }

    const math::Matrix& Xr::GetProjectionMatrix(uint32_t eye_index)
    {
        SP_ASSERT(eye_index < eye_count);
        return m_eye_views[eye_index].projection;
    }

    const math::Vector3& Xr::GetHeadPosition()
    {
        return m_head_position;
    }

    const math::Quaternion& Xr::GetHeadOrientation()
    {
        return m_head_orientation;
    }
}
