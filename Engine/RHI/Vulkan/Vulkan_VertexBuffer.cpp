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

//= INCLUDES =====================
#include "../RHI_Device.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_Vertex.h"
#include "../../Logging/Log.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_VertexBuffer::RHI_VertexBuffer(const std::shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device = rhi_device;
	}

	RHI_VertexBuffer::~RHI_VertexBuffer()
	{

	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosCol>& vertices)
	{
		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUV>& vertices)
	{
		return true;
	}

	bool RHI_VertexBuffer::Create(const vector<RHI_Vertex_PosUvNorTan>& vertices)
	{
		return true;
	}

	bool RHI_VertexBuffer::CreateDynamic(const unsigned int stride, const unsigned int vertex_count)
	{
		return true;
	}

	void* RHI_VertexBuffer::Map() const
	{
		return nullptr;
	}

	bool RHI_VertexBuffer::Unmap() const
	{
		return true;
	}
}
#endif