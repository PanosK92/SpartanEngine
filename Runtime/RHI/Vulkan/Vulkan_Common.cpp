/*
Copyright(c) 2016-2020 Panos Karabelas

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
#ifdef API_GRAPHICS_VULKAN 
//================================

//= INCLUDES =============
#include "Vulkan_Common.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::vulkan_common
{
    PFN_vkCreateDebugUtilsMessengerEXT                          functions::create_messenger                         = nullptr;
    VkDebugUtilsMessengerEXT                                    functions::messenger                                = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                         functions::destroy_messenger                        = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT                             functions::set_object_tag                           = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT                            functions::set_object_name                          = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT                            functions::marker_begin                             = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT                              functions::marker_end                               = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR                 functions::get_physical_device_memory_properties_2  = nullptr;
    mutex                                                       command_buffer_immediate::m_mutex_begin;
    mutex                                                       command_buffer_immediate::m_mutex_end;
    map<RHI_Queue_Type, command_buffer_immediate::cmdbi_object> command_buffer_immediate::m_objects;
}
#endif
