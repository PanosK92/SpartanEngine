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

//= INCLUDES ======================
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../../Core/Settings.h"
#include "../../Core/Context.h"
#include "../../Logging/Log.h"
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	RHI_Device::RHI_Device(Context* context)
	{
       
	}

	RHI_Device::~RHI_Device()
	{
		
	}

    bool RHI_Device::Queue_Submit(const RHI_Queue_Type type, void* cmd_buffer, void* wait_semaphore /*= nullptr*/, void* signal_semaphore /*= nullptr*/, void* wait_fence /*= nullptr*/, uint32_t wait_flags /*= 0*/) const
    {
        return true;
    }

    bool RHI_Device::Queue_Wait(const RHI_Queue_Type type) const
    {
        return true;
    }
}
