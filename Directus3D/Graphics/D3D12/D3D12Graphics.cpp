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
#include "D3D12Graphics.h"
#include <DXGI1_4.h>
#include "../../Logging/Log.h"
//============================

D3D12Graphics::D3D12Graphics()
{
	m_device = nullptr;
	m_commandQueue = nullptr;
	m_swapChain = nullptr;
	m_renderTargetViewHeap = nullptr;
	m_backBufferRenderTarget[0] = nullptr;
	m_backBufferRenderTarget[0] = nullptr;
	m_driverType = D3D_DRIVER_TYPE_HARDWARE;
	m_featureLevel = D3D_FEATURE_LEVEL_12_0;
}

D3D12Graphics::~D3D12Graphics()
{
	// Windowed mode before shutdown or crash
	if (m_swapChain)
		m_swapChain->SetFullscreenState(false, nullptr);

	// Close the object handle to the fence event.
	auto error = CloseHandle(m_fenceEvent);
	if (error == 0)
		LOG_INFO("Failed to close handle.");

	SafeRelease(m_fence);
	SafeRelease(m_pipelineState);
	SafeRelease(m_commandList);
	SafeRelease(m_commandAllocator);
	SafeRelease(m_backBufferRenderTarget[0]);
	SafeRelease(m_backBufferRenderTarget[1]);
	SafeRelease(m_renderTargetViewHeap);
	SafeRelease(m_swapChain);
	SafeRelease(m_commandQueue);
	SafeRelease(m_device);
}

bool D3D12Graphics::Initialize(HWND handle)
{
	//= D3D12 DEVICE ======================================================================================
	HRESULT result = D3D12CreateDevice(nullptr, m_featureLevel, __uuidof(ID3D12Device), (void**)&m_device);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create DirectX 12.0 device.");
		return false;
	}
	//=====================================================================================================

	//= COMMAND QUEUE =====================================================================================
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc;
	ZeroMemory(&commandQueueDesc, sizeof(commandQueueDesc));

	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask = 0; // MSDN: For single GPU operation, set this to zero. 

	result = m_device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), (void**)&m_commandQueue);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create command queue.");
		return false;
	}
	//=====================================================================================================

	//= GRAPHICS INTERFACE FACTORY ========================================================================
	IDXGIFactory* factory;
	result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory));
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create a DirectX graphics interface factory.");
		return false;
	}
	//=====================================================================================================

	//= ADAPTER ===========================================================================================
	IDXGIAdapter* adapter;
	result = factory->EnumAdapters(0, &adapter);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to create a primary graphics interface adapter.");
		return false;
	}

	factory->Release();
	//=====================================================================================================

	//= ADAPTER OUTPUT / DISPLAY MODE =====================================================================
	IDXGIOutput* adapterOutput;
	unsigned int numModes;

	// Enumerate the primary adapter output (monitor).
	result = adapter->EnumOutputs(0, &adapterOutput);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to enumerate the primary adapter output.");
		return false;
	}

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to get adapter's display modes.");
		return false;
	}

	// Create display mode list
	DXGI_MODE_DESC* m_displayModeList = new DXGI_MODE_DESC[numModes];
	if (!m_displayModeList)
	{ 
		LOG_ERROR("Failed to create a display mode list.");
		return false;
	}

	// Now fill the display mode list structures.
	result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, m_displayModeList);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to fill the display mode list structures.");
		return false;
	}

	// Release the adapter output.
	adapterOutput->Release();

	// Go through all the display modes and find the one that matches the screen width and height.
	unsigned int numerator = 0, denominator = 1;
	for (auto i = 0; i < numModes; i++)
		if (m_displayModeList[i].Width == (unsigned int)RESOLUTION_WIDTH && m_displayModeList[i].Height == (unsigned int)RESOLUTION_HEIGHT)
		{
			numerator = m_displayModeList[i].RefreshRate.Numerator;
			denominator = m_displayModeList[i].RefreshRate.Denominator;
			break;
		}

	delete[] m_displayModeList;
	//=====================================================================================================

	//= ADAPTER DESCRIPTION ===============================================================================
	DXGI_ADAPTER_DESC adapterDesc;
	// Get the adapter (video card) description.
	result = adapter->GetDesc(&adapterDesc);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to get the adapter's description.");
		return false;
	}

	// Release the adapter.
	adapter->Release();

	// Store the dedicated video card memory in megabytes.
	m_videoCardMemory = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
	//=====================================================================================================

	//= SWAP CHAIN ========================================================================================
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Width = RESOLUTION_WIDTH;
	swapChainDesc.BufferDesc.Height = RESOLUTION_HEIGHT;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = handle;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	swapChainDesc.Windowed = (BOOL)!FULLSCREEN;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // alt + enter fullscreen

	// Create the swap chain
	IDXGISwapChain* tempSwapChain;
	result = factory->CreateSwapChain(m_commandQueue, &swapChainDesc, &tempSwapChain);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to create the swap chain.");
		return false;
	}

	// Upgrade to a DXGISwapChain3 interface so we can use functionality such as getting the current back buffer index.
	result = tempSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swapChain);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to upgrade swap chain to DXGISwapChain3.");
		return false;
	}

	factory->Release();
	//=====================================================================================================

	//= DESCRIPTOR HEAP FOR RENDER TARGET VIEW ============================================================
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc;
	ZeroMemory(&descriptorHeapDesc, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));

	descriptorHeapDesc.NumDescriptors = 2; // Set back buffer count as the number of descriptors
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // Set heap type to render target view
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	result = m_device->CreateDescriptorHeap(&descriptorHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_renderTargetViewHeap);
	if (FAILED(result))
	{ 
		LOG_ERROR("Failed to create descriptor heap for the render target views.");
		return false;
	}

	// Get descriptor handle of the render target views
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
	//=====================================================================================================

	//= RENDER TARGET VIEW ================================================================================
	// Get the size of the handle increment for the given type of descriptor heap.
	UINT renderTargetViewDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Get 1st back buffer pointer
	result = m_swapChain->GetBuffer(0, __uuidof(ID3D12Resource), (void**)&m_backBufferRenderTarget[0]);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to get first back buffer pointer.");
		return false;
	}

	// Create 1st buck buffer render target view
	m_device->CreateRenderTargetView(m_backBufferRenderTarget[0], nullptr, renderTargetViewHandle);

	// Get 2nd back buffer pointer
	result = m_swapChain->GetBuffer(1, __uuidof(ID3D12Resource), (void**)&m_backBufferRenderTarget[1]);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to get second back buffer pointer.");
		return false;
	}

	// Increment view handle to the next descriptor location in the render target view heap.
	renderTargetViewHandle.ptr += renderTargetViewDescriptorSize;

	// Create 2nd buck buffer render target view
	m_device->CreateRenderTargetView(m_backBufferRenderTarget[1], nullptr, renderTargetViewHandle);
	//=====================================================================================================

	// Get the index of the current back buffer (used for rendering).
	m_bufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	//= COMMAND ALLOCATOR =================================================================================
	// MSDN: The command allocator object corresponds to the underlying allocations in which GPU commands are stored
	result = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_commandAllocator);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create command allocator.");
		return false;
	}
	//=====================================================================================================

	//= COMMAND LIST ======================================================================================
	result = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, nullptr, __uuidof(ID3D12CommandList), (void**)&m_commandList);
	if(FAILED(result))
	{
		LOG_ERROR("Failed to create command list.");
		return false;
	}

	// Close the command list as it is created in a recording state.
	result = m_commandList->Close();
	if (FAILED(result))
	{
		return false;
	}
	//=====================================================================================================

	//= FENCE =============================================================================================
	// MSDN: Fence, an object used for synchronization of the CPU and one or more GPUs. 
	result = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_fence);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create fence.");
		return false;
	}

	// Event for the fence object
	m_fenceEvent = CreateEventEx(nullptr, nullptr, FALSE, EVENT_ALL_ACCESS);
	if (m_fenceEvent == nullptr)
	{
		return false;
	}

	m_fenceValue = m_swapChain->GetCurrentBackBufferIndex();
	//=====================================================================================================

	return true;
}
