/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =====================
#include "Runtime/Core/Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "../RHI_IndexBuffer.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void RHI_IndexBuffer::_destroy()
    {
        
    }
    
    bool RHI_IndexBuffer::_create(const void* indices)
    {
        //ThrowIfFailed(m_device->CreateCommittedResource(
        //    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        //    D3D12_HEAP_FLAG_NONE,
        //    &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
        //    D3D12_RESOURCE_STATE_COPY_DEST,
        //    nullptr,
        //    IID_PPV_ARGS(&m_indexBuffer)));

        //ThrowIfFailed(m_device->CreateCommittedResource(
        //    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        //    D3D12_HEAP_FLAG_NONE,
        //    &CD3DX12_RESOURCE_DESC::Buffer(SampleAssets::IndexDataSize),
        //    D3D12_RESOURCE_STATE_GENERIC_READ,
        //    nullptr,
        //    IID_PPV_ARGS(&indexBufferUploadHeap)));

        //NAME_D3D12_OBJECT(m_indexBuffer);

        //// Copy data to the intermediate upload heap and then schedule a copy 
        //// from the upload heap to the index buffer.
        //D3D12_SUBRESOURCE_DATA indexData = {};
        //indexData.pData                  = pMeshData + SampleAssets::IndexDataOffset;
        //indexData.RowPitch               = SampleAssets::IndexDataSize;
        //indexData.SlicePitch             = indexData.RowPitch;

        //UpdateSubresources<1>(m_commandList.Get(), m_indexBuffer.Get(), indexBufferUploadHeap.Get(), 0, 0, 1, &indexData);
        //m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));

        //// Describe the index buffer view.
        //m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        //m_indexBufferView.Format = SampleAssets::StandardIndexFormat;
        //m_indexBufferView.SizeInBytes = SampleAssets::IndexDataSize;

        //m_numIndices = SampleAssets::IndexDataSize / 4;    // R32_UINT (SampleAssets::StandardIndexFormat) = 4 bytes each.

        return true;
    }
    
    void* RHI_IndexBuffer::Map()
    {
        return nullptr;
    }
    
    void RHI_IndexBuffer::Unmap()
    {

    }
}
