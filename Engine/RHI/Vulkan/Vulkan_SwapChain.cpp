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

//= INCLUDES ==================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& device,
		unsigned int width,
		unsigned int height,
		RHI_Format format			/*= Format_R8G8B8A8_UNORM*/,
		RHI_Swap_Effect swap_effect	/*= Swap_Discard*/,
		unsigned long flags			/*= 0 */,
		unsigned int buffer_count	/*= 1 */
	)
	{

	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		return true;
	}

	bool RHI_SwapChain::SetAsRenderTarget() const
	{
		return true;
	}

	bool RHI_SwapChain::Clear(const Vector4& color) const
	{
		return true;
	}

	bool RHI_SwapChain::Present(const RHI_Present_Mode mode) const
	{
		return true;
	}
}
#endif