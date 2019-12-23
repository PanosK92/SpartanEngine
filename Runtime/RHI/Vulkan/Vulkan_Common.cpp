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
    void*                               command_buffer::m_cmd_pool      = nullptr;
    void*                               command_buffer::m_cmd_buffer    = nullptr;
}
#endif
