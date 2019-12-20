/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN 
//================================

//= INCLUDES =============
#include "Vulkan_Common.h"
//========================

namespace Spartan::vulkan_common
{
    VkDebugUtilsMessengerEXT            debug::m_messenger              = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT debug::m_fn_destroy_messenger   = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT     debug::m_fn_set_object_tag      = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT    debug::m_fn_set_object_name     = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT    debug::m_fn_marker_begin        = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT      debug::m_fn_marker_end          = nullptr;

    static VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity, VkDebugUtilsMessageTypeFlagsEXT msg_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data)
    {
        if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        {
            LOG_INFO(p_callback_data->pMessage);
        }
        else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            LOG_INFO(p_callback_data->pMessage);
        }
        else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            LOG_WARNING(p_callback_data->pMessage);
        }
        else if (msg_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            LOG_ERROR(p_callback_data->pMessage);
        }

        return VK_FALSE;
    }

    void debug::initialize(VkInstance instance)
    {
        if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")))
        {
            VkDebugUtilsMessengerCreateInfoEXT create_info  = {};
            create_info.sType                               = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            create_info.messageSeverity                     = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            create_info.messageType                         = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            create_info.pfnUserCallback                     = callback;

            func(instance, &create_info, nullptr, &m_messenger);
        }
        else
        {
            LOG_ERROR("Failed to get function pointer for vkCreateDebugUtilsMessengerEXT");
        }

        m_fn_destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (!m_fn_destroy_messenger)
        {
            LOG_ERROR("Failed to get function pointer for vkDestroyDebugUtilsMessengerEXT");
        }

        m_fn_set_object_tag = reinterpret_cast<PFN_vkSetDebugUtilsObjectTagEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectTagEXT"));
        if (!m_fn_set_object_tag)
        {
            LOG_ERROR("Failed to get function pointer for vkSetDebugUtilsObjectTagEXT");
        }

        m_fn_set_object_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
        if (!m_fn_set_object_name)
        {
            LOG_ERROR("Failed to get function pointer for vkSetDebugUtilsObjectNameEXT");
        }

        m_fn_marker_begin = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        if (!m_fn_marker_begin)
        {
            LOG_ERROR("Failed to get function pointer for vkCmdBeginDebugUtilsLabelEXT");
        }

        m_fn_marker_end = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
        if (!m_fn_marker_end)
        {
            LOG_ERROR("Failed to get function pointer for vkCmdEndDebugUtilsLabelEXT");
        }
    }
}
#endif
