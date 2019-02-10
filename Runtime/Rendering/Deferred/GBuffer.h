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

//= INCLUDES ========================
#include <map>
#include <memory>
#include <vector>
#include "../../Core/Settings.h"
#include "../../RHI/RHI_Definition.h"
//===================================

namespace Directus
{
	enum GBuffer_Texture_Type
	{
		GBuffer_Target_Unknown,
		GBuffer_Target_Albedo,
		GBuffer_Target_Normal,
		GBuffer_Target_Material,
		GBuffer_Target_Velocity,
		GBuffer_Target_Depth
	};

	class ENGINE_CLASS GBuffer
	{
	public:
		GBuffer(const std::shared_ptr<RHI_Device>& rhiDevice, unsigned int width, unsigned int height);
		~GBuffer();

		const std::shared_ptr<RHI_RenderTexture>& GetTexture(GBuffer_Texture_Type type);
		void Clear();

	private:
		std::map<GBuffer_Texture_Type, std::shared_ptr<RHI_RenderTexture>> m_renderTargets;
	};
}