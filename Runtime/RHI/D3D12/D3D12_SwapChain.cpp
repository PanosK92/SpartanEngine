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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../../Rendering/Renderer.h"
#include "../../Profiling/Profiler.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    RHI_SwapChain::RHI_SwapChain(
        void* window_handle,
        const shared_ptr<RHI_Device>& rhi_device,
        const uint32_t width,
        const uint32_t height,
        const RHI_Format format	    /*= Format_R8G8B8A8_UNORM*/,
        const uint32_t buffer_count	/*= 2 */,
        const uint32_t flags	    /*= Present_Immediate */,
        const char* name            /*= nullptr */
    )
    {
    
    }
    
    RHI_SwapChain::~RHI_SwapChain()
    {
    
    }
    
    bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {
        return true;
    }
    
    bool RHI_SwapChain::AcquireNextImage()
    {
        return true;
    }
    
    bool RHI_SwapChain::Present()
    {
        return true;
    }
}
