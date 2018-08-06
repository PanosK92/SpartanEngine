/*
Copyright(c) 2016-2018 Panos Karabelas

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
#ifdef API_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_Device.h"
#include "../../Math/Vector4.h"
#include "../../Logging/Log.h"
#include <vulkan/vulkan.h>
#include <string>
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace Vulkan_Device
	{
		inline bool AcquireValidationLayers(const std::vector<const char*>& validationLayers)
		{
			uint32_t layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (const char* layerName : validationLayers)
			{
				bool layerFound = false;

				for (const auto& layerProperties : availableLayers)
				{
					if (strcmp(layerName, layerProperties.layerName) == 0)
					{
						layerFound = true;
						break;
					}
				}

				if (!layerFound)
				{
					LOG_ERROR("Vulkan_Device::RHI_Device: Validation layer was requested, but not available.");
					return false;
				}
			}

			return true;
		}

		VkInstance instance;
		const std::vector<const char*> validationLayers = { "VK_LAYER_LUNARG_standard_validation" };
	}

	RHI_Device::RHI_Device(void* drawHandle)
	{
		m_format				= Texture_Format_R8G8B8A8_UNORM;
		m_depthEnabled			= true;
		m_alphaBlendingEnabled	= false;
		m_initialized			= false;
		m_device				= nullptr;
		m_deviceContext			= nullptr;

		LOG_INFO("Hello Vulkan!");

		// Validation layer
		bool validationLayerAvailable = false;
		{
			#ifdef DEBUG
			const bool validationLayerEnabled = true;
			#else
			const bool validationLayerEnabled = false;
			#endif
			
			if (validationLayerEnabled)
			{
				validationLayerAvailable = Vulkan_Device::AcquireValidationLayers(Vulkan_Device::validationLayers);
			}
		}
		
		// Create instance
		{
			VkApplicationInfo appInfo	= {};
			appInfo.sType				= VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName	= "Hello Triangle";
			appInfo.applicationVersion	= VK_MAKE_VERSION(1, 0, 0);
			appInfo.pEngineName			= "Directus3D";
			appInfo.engineVersion		= VK_MAKE_VERSION(1, 0, 0);
			appInfo.apiVersion			= VK_API_VERSION_1_1;

			VkInstanceCreateInfo createInfo = {};
			createInfo.sType				= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo		= &appInfo;
			if (validationLayerAvailable) 
			{
				createInfo.enabledLayerCount	= static_cast<uint32_t>(Vulkan_Device::validationLayers.size());
				createInfo.ppEnabledLayerNames	= Vulkan_Device::validationLayers.data();
			}
			else 
			{
				createInfo.enabledLayerCount = 0;
			}

			auto result = vkCreateInstance(&createInfo, nullptr, &Vulkan_Device::instance);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR("Vulkan_Device::RHI_Device: Failed to create instance.");
				return;
			}
		}
		
		// Get available extensions
		{
			uint32_t extensionCount = 0;
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
			for (const auto& extension : extensions)
			{
				LOGF_INFO("Vulkan_Device::RHI_Device: Available extension: %s", extension.extensionName);
			}
		}

		//LOGF_INFO("Vulkan_Device::RHI_Device: Feature level %s - %s", featureLevelStr.data(), D3D11_Device::GetAdapterDescription(adapter).data());
		m_device		= nullptr;
		m_deviceContext = nullptr;
		m_initialized	= false;
	}

	RHI_Device::~RHI_Device()
	{
		vkDestroyInstance(Vulkan_Device::instance, nullptr);
	}

	void RHI_Device::Draw(unsigned int vertexCount)
	{
		
	}

	void RHI_Device::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		
	}

	void RHI_Device::ClearBackBuffer(const Vector4& color)
	{

	}

	void RHI_Device::ClearRenderTarget(void* renderTarget, const Math::Vector4& color)
	{
		
	}

	void RHI_Device::ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, uint8_t stencil)
	{
		
	}

	void RHI_Device::Present()
	{
		
	}

	void RHI_Device::Bind_BackBufferAsRenderTarget()
	{
		
	}

	void RHI_Device::Bind_VertexShader(void* buffer)
	{
		
	}

	void RHI_Device::Bind_PixelShader(void* buffer)
	{
		
	}

	void RHI_Device::Bind_ConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* const* buffer)
	{
		
	}

	void RHI_Device::Bind_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers)
	{
		
	}

	void RHI_Device::Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil)
	{
		
	}

	void RHI_Device::Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources)
	{
		
	}

	bool RHI_Device::SetResolution(int width, int height)
	{
		return true;
	}

	void RHI_Device::SetViewport(const RHI_Viewport& viewport)
	{
		
	}

	bool RHI_Device::EnableDepth(bool enable)
	{
		return true;
	}

	bool RHI_Device::EnableAlphaBlending(bool enable)
	{
		return true;
	}

	void RHI_Device::EventBegin(const std::string& name)
	{
		
	}

	void RHI_Device::EventEnd()
	{
		
	}

	bool RHI_Device::Profiling_CreateQuery(void** query, Query_Type type)
	{
		return true;
	}

	void RHI_Device::Profiling_QueryStart(void* queryObject)
	{
		
	}

	void RHI_Device::Profiling_QueryEnd(void* queryObject)
	{
		
	}

	void RHI_Device::Profiling_GetTimeStamp(void* queryObject)
	{
		
	}

	float RHI_Device::Profiling_GetDuration(void* queryDisjoint, void* queryStart, void* queryEnd)
	{
		return 0.0f;
	}

	bool RHI_Device::Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		return true;
	}

	bool RHI_Device::Set_FillMode(Fill_Mode fillMode)
	{
		return true;
	}

	bool RHI_Device::Set_InputLayout(void* inputLayout)
	{
		return true;
	}

	bool RHI_Device::Set_CullMode(Cull_Mode cullMode)
	{
		return true;
	}
}

#endif