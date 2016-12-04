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

//= INCLUDES =================
#include "D3D12Texture.h"
#include "D3D12Graphics.h"
#include "../../Logging/Log.h"
//============================

D3D12Texture::D3D12Texture(Graphics* graphics)
{
	m_graphics = graphics;
	m_resourceView = nullptr;
}

D3D12Texture::~D3D12Texture()
{

}

bool D3D12Texture::Create(int width, int height, int channels, unsigned char* data)
{
	//= SUBRESROUCE DATA =======================================================================
	D3D12_SUBRESOURCE_DATA subresource;
	ZeroMemory(&subresource, sizeof(subresource));
	subresource.pData = data;
	subresource.RowPitch = (width * channels) * sizeof(unsigned char);
	subresource.SlicePitch = (width * height * channels) * sizeof(unsigned char);
	//==========================================================================================

	//= ID3D12Resource =======================================================================
	D3D12_RESOURCE_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	HRESULT result;
	/*HRESULT result = m_graphics->GetAPI()->GetDevice()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		__uuidof(ID3D12Resource),
		(void**)&m_resourceView
	);*/

	/*if (FAILED(result))
	{
		LOG_INFO("Failed to create ID3D12Resource.");
		return false;
	}*/
	//=========================================================================================

	//= ID3D12DescriptorHeap ==================================================================
	// create the descriptor heap that will store our srv
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
	ZeroMemory(&heapDesc, sizeof(heapDesc));
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	ID3D12DescriptorHeap* m_srvHeap;
	//result = m_graphics->GetAPI()->GetDevice()->CreateDescriptorHeap(&heapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_srvHeap);

	if (FAILED(result))
	{
		LOG_INFO("Failed to create ID3D12DescriptorHeap.");
		return false;
	}
	//=========================================================================================

	//= SHADER RESOURCE VIEW ==================================================================
	// create a shader resource view(descriptor that points to the texture and describes it)
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	//m_graphics->GetAPI()->GetDevice()->CreateShaderResourceView(m_resourceView, &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
	//==========================================================================================

	return true;
}

bool D3D12Texture::CreateAndGenerateMipchain(int width, int height, int channels, unsigned char* data)
{
	LOG_INFO("D3D12Texture::CreateAndGenerateMipchain() is not implemented yet.");
	return true;
}

bool D3D12Texture::CreateFromMipchain(int width, int height, int channels, const std::vector<std::vector<unsigned char>>& mipchain)
{
	LOG_INFO("D3D12Texture::CreateFromMipchain() is not implemented yet.");
	return true;
}
