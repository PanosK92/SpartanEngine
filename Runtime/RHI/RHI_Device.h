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

#pragma once

//= INCLUDES ==================
#include <string>
#include <vector>
#include <memory>
#include "RHI_Definition.h"
#include "../Core/EngineDefs.h"
//=============================

namespace Spartan
{
    class Context;
	namespace Math
	{ 
		class Vector4;
		class Rectangle;
	}

	struct DisplayMode
	{
		DisplayMode() = default;
		DisplayMode(const uint32_t width, const uint32_t height, const uint32_t refresh_rate_numerator, const uint32_t refresh_rate_denominator)
		{
			this->width						= width;
			this->height					= height;
			this->refreshRateNumerator		= refresh_rate_numerator;
			this->refreshRateDenominator	= refresh_rate_denominator;
			this->refreshRate				= static_cast<float>(refresh_rate_numerator) / static_cast<float>(refresh_rate_denominator);
		}

		uint32_t width					= 0;
		uint32_t height					= 0;
		uint32_t refreshRateNumerator	= 0;
		uint32_t refreshRateDenominator = 0;
		float refreshRate				= 0;
	};

	struct DisplayAdapter
	{
		DisplayAdapter(const std::string& name, const uint32_t memory, const uint32_t vendor_id, void* data)
		{
			this->name		= name;
			this->memory	= memory;
			this->vendorID	= vendor_id;
			this->data		= data;
		}

		bool IsNvidia() const	{ return vendorID == 0x10DE || name.find("NVIDIA") != std::string::npos; }
		bool IsAmd() const		{ return vendorID == 0x1002 || vendorID == 0x1022 || name.find("AMD") != std::string::npos; }
		bool IsIntel() const	{ return vendorID == 0x163C || vendorID == 0x8086 || vendorID == 0x8087 || name.find("Intel") != std::string::npos;}

		std::string name		= "Unknown";
		uint32_t vendorID	= 0;
		uint32_t memory		= 0;
		void* data				= nullptr;
	};

	class SPARTAN_CLASS RHI_Device
	{
	public:
		RHI_Device(Context* context);
		~RHI_Device();

		//= API ===================================================================================
		bool ProfilingCreateQuery(void** query, RHI_Query_Type type) const;
		bool ProfilingQueryStart(void* query_object) const;
		bool ProfilingGetTimeStamp(void* query_object) const;
		float ProfilingGetDuration(void* query_disjoint, void* query_start, void* query_end) const;
		void ProfilingReleaseQuery(void* query_object);
		uint32_t ProfilingGetGpuMemory();
		uint32_t ProfilingGetGpuMemoryUsage();
		//=========================================================================================

		//= ADAPTERS ============================================================================================================
		void AddDisplayMode(uint32_t width, uint32_t height, uint32_t refresh_rate_numerator, uint32_t refresh_rate_denominator);
		bool GetDisplayModeFastest(DisplayMode* display_mode);
		void AddAdapter(const std::string& name, uint32_t memory, uint32_t vendor_id, void* adapter);
		void SetPrimaryAdapter(const DisplayAdapter* primary_adapter);
		const std::vector<DisplayAdapter>& GetAdapters()	const { return m_display_adapters; }
		const DisplayAdapter* GetPrimaryAdapter()			const { return m_primary_adapter; }
		//=======================================================================================================================

		auto IsInitialized()                const { return m_initialized; }
        RHI_Context* GetContextRhi()	    const { return m_rhi_context.get(); }
        Context* GetContext()               const { return m_context; }
        uint32_t GetEnabledGraphicsStages() const { return m_enabled_graphics_shader_stages; }

	private:
		std::shared_ptr<RHI_Context> m_rhi_context;
		Context* m_context = nullptr;

		bool m_initialized = false;
		const DisplayAdapter* m_primary_adapter = nullptr;
		std::vector<DisplayMode> m_displayModes;
		std::vector<DisplayAdapter> m_display_adapters;
        uint32_t m_enabled_graphics_shader_stages = 0;
	};
}
