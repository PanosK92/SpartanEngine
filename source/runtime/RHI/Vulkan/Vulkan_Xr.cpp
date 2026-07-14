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
#include "../../XR/Xr.h"
#include "../../Rendering/Renderer.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../../World/World.h"
#include "../../World/Components/Camera.h"
#include "../../World/Entity.h"
#include <thread>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // openxr state (vulkan implementation)
    namespace
    {
        XrInstance xr_instance           = XR_NULL_HANDLE;
        XrSystemId xr_system_id          = XR_NULL_SYSTEM_ID;
        XrSession xr_session             = XR_NULL_HANDLE;
        XrSpace xr_reference_space       = XR_NULL_HANDLE;
        XrSwapchain xr_swapchain         = XR_NULL_HANDLE;
        XrSessionState xr_session_state  = XR_SESSION_STATE_UNKNOWN;
        XrFrameState xr_frame_state      = {};
        XrTime xr_predicted_display_time = 0;
        bool xr_session_state_changed    = false;

        // swapchain state
        uint32_t swapchain_width       = 0;
        uint32_t swapchain_height      = 0;
        uint32_t swapchain_length      = 0;
        uint32_t swapchain_image_index = 0;
        VkFormat swapchain_format      = VK_FORMAT_R8G8B8A8_SRGB;
        vector<XrSwapchainImageVulkanKHR> swapchain_images;
        vector<VkImageView> swapchain_image_views;

        // tracks whether a swapchain image was acquired/released between the current
        // xrBeginFrame and xrEndFrame. when it hasn't been (e.g. the renderer was still
        // loading resources and skipped the blit), xrEndFrame must submit zero layers
        // or the runtime responds with XR_ERROR_LAYER_INVALID.
        bool swapchain_image_released_this_frame = false;

        // views for stereo rendering
        vector<XrView> xr_views;
        vector<XrViewConfigurationView> xr_view_configs;
        XrViewState xr_view_state = {};

        // function pointers (loaded at runtime)
        PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
        PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
        PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
        PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;

        // background init state
        // xrCreateInstance with steamvr spawns vrserver.exe and blocks for several seconds
        // waiting for its pipe, so the whole init runs on a worker to keep engine boot snappy
        thread xr_init_thread;
        atomic<bool> xr_init_in_progress   = false;
        atomic<bool> xr_post_init_pending  = false;

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

        // build an asymmetric left-handed reverse-z projection matrix from openxr fov angles.
        // the engine uses row-vector multiplication (clip = view * proj), left-handed view space
        // (+z forward), and reverse-z (near maps to 1, far maps to 0) to match Camera::ComputeProjection.
        // openxr fov angles are signed relative to the forward axis (angleLeft/angleDown typically
        // negative, angleRight/angleUp typically positive); they apply directly in lh since only
        // the z axis was mirrored during the pose conversion.
        math::Matrix create_projection_matrix(float fov_left, float fov_right, float fov_up, float fov_down, float near_z, float far_z)
        {
            const float tan_left  = tanf(fov_left);
            const float tan_right = tanf(fov_right);
            const float tan_up    = tanf(fov_up);
            const float tan_down  = tanf(fov_down);

            const float tan_width  = tan_right - tan_left;
            const float tan_height = tan_up - tan_down;

            const float x_scale = 2.0f / tan_width;
            const float y_scale = 2.0f / tan_height;
            const float x_shift = -(tan_right + tan_left) / tan_width;
            const float y_shift = -(tan_up + tan_down) / tan_height;

            // reverse-z: at z=near clip.z/w = 1, at z=far clip.z/w = 0
            const float z_scale = near_z / (near_z - far_z);
            const float z_bias  = (near_z * far_z) / (far_z - near_z);

            // row-major constructor: rows below correspond to (row, col) = (m00..m03, m10..m13, ...)
            return math::Matrix(
                x_scale, 0.0f,    0.0f,    0.0f,
                0.0f,    y_scale, 0.0f,    0.0f,
                x_shift, y_shift, z_scale, 1.0f,
                0.0f,    0.0f,    z_bias,  0.0f
            );
        }
    }

    void Xr::InitializeWorker()
    {
        // runs on xr_init_thread so engine boot isn't stalled by steamvr spin-up or by
        // the ~7s xrCreateInstance pipe wait when no hmd is present

        for (auto& eye : m_eye_views)
        {
            eye.view        = math::Matrix::Identity;
            eye.projection  = math::Matrix::Identity;
            eye.position    = math::Vector3::Zero;
            eye.orientation = math::Quaternion::Identity;
        }

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
            SP_LOG_INFO("openxr: no runtime with vulkan support detected");
            return;
        }

        const char* enabled_extensions[] = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };

        XrApplicationInfo app_info  = {};
        app_info.apiVersion         = XR_API_VERSION_1_0;
        strcpy_s(app_info.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Spartan Engine");
        strcpy_s(app_info.engineName, XR_MAX_ENGINE_NAME_SIZE, "Spartan");
        app_info.applicationVersion = 1;
        app_info.engineVersion      = 1;

        XrInstanceCreateInfo create_info  = { XR_TYPE_INSTANCE_CREATE_INFO };
        create_info.applicationInfo       = app_info;
        create_info.enabledExtensionCount = 1;
        create_info.enabledExtensionNames = enabled_extensions;

        if (XR_FAILED(xrCreateInstance(&create_info, &xr_instance)))
        {
            SP_LOG_INFO("openxr: no runtime available");
            xr_instance = XR_NULL_HANDLE;
            return;
        }

        XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(xrGetInstanceProperties(xr_instance, &instance_props)))
        {
            m_runtime_name = instance_props.runtimeName;
            SP_LOG_INFO("openxr runtime: %s", instance_props.runtimeName);
        }

        xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsRequirementsKHR));
        xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsDeviceKHR",       reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanGraphicsDeviceKHR));
        xrGetInstanceProcAddr(xr_instance, "xrGetVulkanInstanceExtensionsKHR",   reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanInstanceExtensionsKHR));
        xrGetInstanceProcAddr(xr_instance, "xrGetVulkanDeviceExtensionsKHR",     reinterpret_cast<PFN_xrVoidFunction*>(&xrGetVulkanDeviceExtensionsKHR));

        // this is the call that actually tells steamvr to look for a headset and can
        // return HmdNotFound if none is plugged in
        XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
        system_info.formFactor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        const bool hmd_found = XR_SUCCEEDED(xrGetSystem(xr_instance, &system_info, &xr_system_id));

        if (!hmd_found)
        {
            // release the runtime so we don't hold onto vrserver while idling
            SP_LOG_INFO("openxr: no hmd detected, releasing runtime");
            xrDestroyInstance(xr_instance);
            xr_instance  = XR_NULL_HANDLE;
            xr_system_id = XR_NULL_SYSTEM_ID;
            return;
        }

        m_hmd_connected = true;

        XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };
        if (XR_SUCCEEDED(xrGetSystemProperties(xr_instance, xr_system_id, &system_props)))
        {
            m_device_name = system_props.systemName;
        }

        uint32_t view_count = 0;
        xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr);
        if (view_count > 0)
        {
            xr_view_configs.resize(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
            xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count, xr_view_configs.data());

            m_recommended_width  = xr_view_configs[0].recommendedImageRectWidth;
            m_recommended_height = xr_view_configs[0].recommendedImageRectHeight;

            xr_views.resize(view_count, { XR_TYPE_VIEW });
        }

        if (xrGetVulkanGraphicsRequirementsKHR)
        {
            XrGraphicsRequirementsVulkanKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
            xrGetVulkanGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirements);
        }

        SP_LOG_INFO("openxr hmd: %s (%ux%u per eye)", m_device_name.c_str(), m_recommended_width, m_recommended_height);

        if (!CreateSession())
        {
            SP_LOG_ERROR("openxr: failed to create session");
            return;
        }

        // stereo mode flip is deferred to the main thread because it rebuilds render
        // targets via Renderer::RecreateRenderTargets which is not thread-safe
        if (IsMultiviewSupported())
        {
            xr_post_init_pending = true;
        }
    }

    void Xr::Initialize()
    {
        // already up or a previous init is still running
        if (IsAvailable() || xr_init_in_progress.load())
        {
            return;
        }

        // reap any previous attempt so we can spawn a fresh one (e.g. ctrl+0 retry)
        if (xr_init_thread.joinable())
        {
            xr_init_thread.join();
        }

        xr_init_in_progress = true;
        SP_LOG_INFO("openxr: initializing on background thread");

        xr_init_thread = thread([]()
        {
            InitializeWorker();
            // release-store: everything written by the worker becomes visible to any
            // thread that observes m_initialized == true via acquire semantics
            m_initialized.store(xr_instance != XR_NULL_HANDLE);
            xr_init_in_progress = false;
        });
    }

    void Xr::Shutdown()
    {
        // wait for any in-flight init so we don't race on the xr/vulkan handles
        if (xr_init_thread.joinable())
        {
            xr_init_thread.join();
        }

        xr_post_init_pending = false;

        if (!m_initialized.load())
        {
            return;
        }

        SetStereoMode(false);

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
        m_initialized.store(false);
    }

    void Xr::Tick()
    {
        if (!m_initialized.load())
        {
            return;
        }

        // apply deferred post-init actions that must run on the main thread
        if (xr_post_init_pending.exchange(false))
        {
            SetStereoMode(true);
            SP_LOG_INFO("openxr: multiview stereo rendering enabled");
        }

        if (!m_hmd_connected)
        {
            return;
        }

        ProcessEvents();
    }

    bool Xr::CreateSession()
    {
        if (xr_session != XR_NULL_HANDLE)
        {
            return true;
        }

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
        {
            return false;
        }

        // create reference space (local = seated, stage = standing)
        if (!CreateReferenceSpace())
        {
            return false;
        }

        // create swapchain
        if (!CreateSwapchain())
        {
            return false;
        }

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
        {
            return true;
        }

        if (xr_session == XR_NULL_HANDLE || xr_view_configs.empty())
        {
            return false;
        }

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
        swapchain_format = static_cast<VkFormat>(selected_format);

        // create swapchain as array texture for multiview (2 layers = 2 eyes)
        XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapchain_info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        swapchain_info.format      = selected_format;
        swapchain_info.sampleCount = 1;
        swapchain_info.width       = swapchain_width;
        swapchain_info.height      = swapchain_height;
        swapchain_info.faceCount   = 1;
        swapchain_info.arraySize   = eye_count; // multiview: 2 layers for stereo
        swapchain_info.mipCount    = 1;

        if (!xr_check(xrCreateSwapchain(xr_session, &swapchain_info, &xr_swapchain), "create swapchain"))
        {
            return false;
        }

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

        swapchain_width  = 0;
        swapchain_height = 0;
        swapchain_length = 0;
        swapchain_format = VK_FORMAT_R8G8B8A8_SRGB;
    }

    bool Xr::CreateReferenceSpace()
    {
        // we always request LOCAL reference space.  its origin sits near the user's
        // initial head pose (runtime-chosen, gravity aligned), so when the user is
        // still the eye poses we get back are close to the origin.  that makes the
        // hmd pose a small offset that can be composed on top of the game camera's
        // world transform, keeping the in-world head at the camera's position (e.g.
        // on top of the player capsule) rather than at the vr room floor like STAGE
        // space would.
        const XrReferenceSpaceType space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
        SP_LOG_INFO("openxr: using local reference space (origin near initial head pose)");

        XrPosef identity_pose = {};
        identity_pose.orientation.w = 1.0f;

        XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        space_info.referenceSpaceType = space_type;
        space_info.poseInReferenceSpace = identity_pose;

        return xr_check(xrCreateReferenceSpace(xr_session, &space_info, &xr_reference_space), "create reference space");
    }

    void Xr::ProcessEvents()
    {
        // track whether stereo rendering was active on the previous pass through this
        // function. any change in the effective stereo state (session running + stereo
        // enabled) triggers a render target rebuild so the g-buffer switches between
        // Type2D and Type2DArray as the user puts on / takes off the headset.
        const bool stereo_active_before = m_session_running && m_stereo_3d;

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
                            // the runtime drives this transition when the hmd becomes ready
                            // to present frames (typically when the user puts it on).
                            XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
                            begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                            if (xr_check(xrBeginSession(xr_session, &begin_info), "begin session"))
                            {
                                m_session_running = true;
                                SP_LOG_INFO("openxr: session started (headset active)");
                            }
                            break;
                        }
                        case XR_SESSION_STATE_STOPPING:
                        {
                            // driven by the runtime when the user removes the headset or
                            // otherwise stops the session.  the session stays alive and
                            // can transition back to READY the next time the headset is worn.
                            m_session_running = false;
                            m_session_focused = false;
                            xrEndSession(xr_session);
                            SP_LOG_INFO("openxr: session stopped (headset inactive)");
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

        // if the effective stereo state flipped during this tick, rebuild the render
        // targets so they match the new layout (Type2DArray vs Type2D) before the next
        // frame kicks off.  Xr::Tick runs before Renderer::Tick each frame, so the queue
        // is idle here and RecreateRenderTargets can safely wait for gpu completion.
        const bool stereo_active_after = m_session_running && m_stereo_3d;
        if (stereo_active_before != stereo_active_after)
        {
            Renderer::RecreateRenderTargets();
        }
    }

    void Xr::UpdateViews()
    {
        if (xr_session == XR_NULL_HANDLE || xr_reference_space == XR_NULL_HANDLE)
        {
            return;
        }

        XrViewLocateInfo view_locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
        view_locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        view_locate_info.displayTime = xr_predicted_display_time;
        view_locate_info.space = xr_reference_space;

        xr_view_state = { XR_TYPE_VIEW_STATE };
        uint32_t view_count = 0;

        XrResult result = xrLocateViews(xr_session, &view_locate_info, &xr_view_state,
            static_cast<uint32_t>(xr_views.size()), &view_count, xr_views.data());

        if (XR_FAILED(result) || view_count < eye_count)
        {
            return;
        }

        // check if pose is valid
        bool pose_valid = (xr_view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                          (xr_view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

        if (!pose_valid)
        {
            return;
        }

        // calculate head position (average of both eyes), still in rig-local / openxr space
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

        // resolve the engine camera's world pose so hmd poses can be anchored to it.
        // the camera entity defines the "rig root" in the game world (e.g. the head
        // position on top of the player capsule).  we read position and rotation
        // directly from the entity and compose per-eye transforms with primitives so
        // the final view matrix is built via CreateLookAtLH, exactly like the mono
        // camera path in Camera::UpdateViewMatrix.  that avoids any ambiguity around
        // matrix composition order between this code and the rest of the engine.
        math::Vector3    camera_pos = math::Vector3::Zero;
        math::Quaternion camera_rot = math::Quaternion::Identity;
        float            near_z     = 0.1f;
        float            far_z      = 10000.0f;
        if (auto camera = World::GetCamera())
        {
            camera_pos = camera->GetEntity()->GetPosition();
            camera_rot = camera->GetEntity()->GetRotation();
            near_z     = camera->GetNearPlane();
            far_z      = camera->GetFarPlane();
        }

        // update per-eye data
        for (uint32_t i = 0; i < eye_count; i++)
        {
            const XrView& view = xr_views[i];

            // convert openxr (right-handed, -z forward) pose into engine space
            // (left-handed, +z forward) by mirroring the z axis.  the required mapping
            // for a quaternion (qx, qy, qz, qw) is to negate qx and qy (equivalent to
            // negating qz and qw, since q and -q represent the same rotation).  we use
            // the (qz, qw) form below.
            const math::Vector3 rig_local_pos(
                view.pose.position.x,
                view.pose.position.y,
                -view.pose.position.z
            );
            const math::Quaternion rig_local_rot(
                view.pose.orientation.x,
                view.pose.orientation.y,
                -view.pose.orientation.z,
                -view.pose.orientation.w
            );

            // compose rig-local eye pose with camera world pose to get eye world pose.
            // the rig-local offset is rotated into world space by the camera's rotation
            // and added to the camera's world position; orientation composes as usual.
            const math::Vector3    world_eye_pos = camera_pos + camera_rot * rig_local_pos;
            const math::Quaternion world_eye_rot = camera_rot * rig_local_rot;

            // derive view basis from the composed orientation and build the view matrix
            // using the exact same call the mono camera uses, so there is no convention
            // mismatch between xr and non-xr view matrices.
            const math::Vector3 forward = world_eye_rot * math::Vector3::Forward;
            const math::Vector3 up      = world_eye_rot * math::Vector3::Up;

            m_eye_views[i].view        = math::Matrix::CreateLookAtLH(world_eye_pos, world_eye_pos + forward, up);
            m_eye_views[i].position    = world_eye_pos;
            m_eye_views[i].orientation = world_eye_rot;

            // store fov angles
            m_eye_views[i].fov_left  = view.fov.angleLeft;
            m_eye_views[i].fov_right = view.fov.angleRight;
            m_eye_views[i].fov_up    = view.fov.angleUp;
            m_eye_views[i].fov_down  = view.fov.angleDown;

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
        {
            return false;
        }

        // wait for frame
        xr_frame_state = { XR_TYPE_FRAME_STATE };
        if (!xr_check(xrWaitFrame(xr_session, nullptr, &xr_frame_state), "wait frame"))
        {
            return false;
        }

        // begin frame
        if (!xr_check(xrBeginFrame(xr_session, nullptr), "begin frame"))
        {
            return false;
        }

        m_frame_began                         = true;
        swapchain_image_released_this_frame   = false;
        xr_predicted_display_time             = xr_frame_state.predictedDisplayTime;

        // update view poses
        UpdateViews();

        // should we render this frame?
        return xr_frame_state.shouldRender == XR_TRUE;
    }

    void Xr::EndFrame()
    {
        if (!m_frame_began || xr_session == XR_NULL_HANDLE)
        {
            return;
        }

        // prepare projection views for submission
        array<XrCompositionLayerProjectionView, eye_count> projection_views;

        for (uint32_t i = 0; i < eye_count; i++)
        {
            projection_views[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projection_views[i].subImage.swapchain         = xr_swapchain;
            projection_views[i].subImage.imageRect.offset  = { 0, 0 };
            projection_views[i].subImage.imageRect.extent  = { static_cast<int32_t>(swapchain_width), static_cast<int32_t>(swapchain_height) };
            projection_views[i].subImage.imageArrayIndex   = i;

            // submit the pose/fov matching the eye that was actually rendered into array layer i.
            // this is the correct pairing now that per-eye matrices are used end-to-end; no
            // runtime-specific swap is required.
            projection_views[i].pose = xr_views[i].pose;
            projection_views[i].fov  = xr_views[i].fov;
        }

        XrCompositionLayerProjection projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        projection_layer.space     = xr_reference_space;
        projection_layer.viewCount = eye_count;
        projection_layer.views     = projection_views.data();

        const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection_layer) };

        // only submit the projection layer if a swapchain image was actually produced
        // this frame.  if the renderer skipped the blit (e.g. resources were still being
        // loaded on the background thread right after the session came up), we must
        // submit zero layers or the runtime returns XR_ERROR_LAYER_INVALID.
        const bool submit_layer = xr_frame_state.shouldRender && swapchain_image_released_this_frame;

        XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
        end_info.displayTime          = xr_predicted_display_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end_info.layerCount           = submit_layer ? 1 : 0;
        end_info.layers               = submit_layer ? layers : nullptr;

        xr_check(xrEndFrame(xr_session, &end_info), "end frame");
        m_frame_began = false;
    }

    bool Xr::AcquireSwapchainImage()
    {
        if (xr_swapchain == XR_NULL_HANDLE)
        {
            return false;
        }

        XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (!xr_check(xrAcquireSwapchainImage(xr_swapchain, &acquire_info, &swapchain_image_index), "acquire swapchain image"))
        {
            return false;
        }

        XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait_info.timeout = XR_INFINITE_DURATION;
        if (!xr_check(xrWaitSwapchainImage(xr_swapchain, &wait_info), "wait swapchain image"))
        {
            return false;
        }

        return true;
    }

    void Xr::ReleaseSwapchainImage()
    {
        if (xr_swapchain == XR_NULL_HANDLE)
        {
            return;
        }

        XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        if (xr_check(xrReleaseSwapchainImage(xr_swapchain, &release_info), "release swapchain image"))
        {
            // mark the frame as having produced a swapchain image so EndFrame knows it
            // is valid to submit a projection layer referencing it.
            swapchain_image_released_this_frame = true;
        }
    }

    bool Xr::IsAvailable()
    {
        // acquire-load pairs with the release-store in Initialize's worker thread
        return m_initialized.load() && xr_instance != XR_NULL_HANDLE;
    }

    void* Xr::GetSwapchainImage()
    {
        if (swapchain_images.empty() || swapchain_image_index >= swapchain_images.size())
        {
            return nullptr;
        }
        return swapchain_images[swapchain_image_index].image;
    }

    void* Xr::GetSwapchainImageView()
    {
        if (swapchain_image_views.empty() || swapchain_image_index >= swapchain_image_views.size())
        {
            return nullptr;
        }
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
}
