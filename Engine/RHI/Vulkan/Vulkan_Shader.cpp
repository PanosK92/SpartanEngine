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
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{

	RHI_Shader::RHI_Shader(const shared_ptr<RHI_Device> rhi_device)
	{
		m_rhi_device = rhi_device;
	}

	RHI_Shader::~RHI_Shader()
	{
		
	}

	bool RHI_Shader::Compile_Vertex(const string& shader, const unsigned long input_layout)
	{
		return false;
	}

	bool RHI_Shader::Compile_Pixel(const string& shader)
	{
		return false;
	}
}
#endif