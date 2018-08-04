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

#pragma once

//= INCLUDES ===============
#include "IRHI_Definition.h"
//==========================

namespace Directus
{
	class RHI_Sampler
	{
	public:
		RHI_Sampler(
			std::shared_ptr<RHI_Device> rhiDevice,
			Texture_Sampler_Filter filter					= Texture_Sampler_Anisotropic,
			Texture_Address_Mode textureAddressMode			= Texture_Address_Wrap,
			Texture_Comparison_Function comparisonFunction	= Texture_Comparison_Always);
		~RHI_Sampler();

		Texture_Sampler_Filter GetFilter()					{ return m_filter; }
		Texture_Address_Mode GetAddressMode()				{ return m_textureAddressMode; }
		Texture_Comparison_Function GetComparisonFunction() { return m_comparisonFunction; }
		void* GetBuffer()									{ return m_buffer; }

	private:
		std::shared_ptr<RHI_Device> m_rhiDevice;
		void* m_buffer;
		Texture_Sampler_Filter m_filter;
		Texture_Address_Mode m_textureAddressMode;
		Texture_Comparison_Function m_comparisonFunction;
	};
}