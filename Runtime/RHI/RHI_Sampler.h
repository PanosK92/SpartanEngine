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

//= INCLUDES ==============
#include "RHI_Definition.h"
#include "RHI_Object.h"
#include <memory>
//=========================

namespace Spartan
{
	class RHI_Sampler : public RHI_Object
	{
	public:
		RHI_Sampler(
			const std::shared_ptr<RHI_Device>& rhi_device,
			RHI_Texture_Filter filter						= Texture_Filter_Anisotropic,
			RHI_Sampler_Address_Mode sampler_address_mode	= Sampler_Address_Wrap,
			RHI_Comparison_Function comparison_function		= Comparison_Always);
		~RHI_Sampler();

		RHI_Texture_Filter GetFilter() const					{ return m_filter; }
		RHI_Sampler_Address_Mode GetAddressMode() const			{ return m_sampler_address_mode; }
		RHI_Comparison_Function GetComparisonFunction() const	{ return m_comparison_function; }
		void* GetResource() const								{ return m_buffer_view; }

	private:	
		RHI_Texture_Filter m_filter;
		RHI_Sampler_Address_Mode m_sampler_address_mode;
		RHI_Comparison_Function m_comparison_function;
		std::shared_ptr<RHI_Device> m_rhi_device;

		// API
		void* m_buffer_view = nullptr;
	};
}