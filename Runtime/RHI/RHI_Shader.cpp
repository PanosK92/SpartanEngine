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

//= INCLUDES ==================
#include "RHI_Shader.h"
#include "RHI_ConstantBuffer.h"
#include "..\Logging\Log.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	void RHI_Shader::AddDefine(const std::string& define, const std::string& value /*= "1"*/)
	{
		m_macros[define] = value;
	}

	void RHI_Shader::UpdateBuffer(void* data)
	{
		if (!data)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		if (!m_constantBuffer)
		{
			LOG_WARNING("Uninitialized buffer.");
			return;
		}

		// Get a pointer of the buffer
		auto buffer = m_constantBuffer->Map();	// Get buffer pointer
		memcpy(buffer, data, m_bufferSize);		// Copy data
		m_constantBuffer->Unmap();				// Unmap buffer
	}

	void RHI_Shader::CreateConstantBuffer(unsigned int size)
	{
		m_constantBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice, size);
	}
}