/*
Copyright(c) 2016 Panos Karabelas

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

#pragma once

//= INCLUDES =================
#include "D3D11Device.h"
#include <vector>
#include "../../Core/Vertex.h"
//============================

class D3D11Buffer
{
public:
	D3D11Buffer();
	~D3D11Buffer();

	bool Initialize(unsigned int size, D3D11Device* d3d11device);
	bool Initialize(std::vector<VertexPositionTextureNormalTangent> vertices, D3D11Device* d3d11device);
	bool Initialize(std::vector<unsigned int> indices, D3D11Device* d3d11device);

	void SetIA();
	void SetVS(unsigned int startSlot);
	void SetPS(unsigned int startSlot);

	void* Map();
	void Unmap();

private:
	D3D11Device* m_D3D11Device;
	ID3D11Buffer* m_buffer;
	unsigned int m_stride;
	unsigned int m_size;
	D3D11_USAGE m_usage;
	D3D11_BIND_FLAG m_bindFlag;
	D3D11_CPU_ACCESS_FLAG m_cpuAccessFlag;

	/*------------------------------------------------------------------------------
									[PRIVATE]
	------------------------------------------------------------------------------*/
	bool CreateBuffer(unsigned int size, void* data, D3D11_USAGE usage, D3D11_BIND_FLAG bindFlag, D3D11_CPU_ACCESS_FLAG cpuAccessFlag);
};
