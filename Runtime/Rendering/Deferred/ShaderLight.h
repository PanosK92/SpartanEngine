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
#include "../../Math/Matrix.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector4.h"
#include "../../RHI/RHI_Shader.h"
#include "../../RHI/RHI_Definition.h"
#include <vector>
//===================================

namespace Spartan
{
	class Entity;

	class ShaderLight : public RHI_Shader
	{
	public:
		ShaderLight(const std::shared_ptr<RHI_Device>& rhi_device);
		~ShaderLight() = default;

		void UpdateConstantBuffer(
			const Math::Matrix& mViewProjection_Orthographic,
			const Math::Matrix& mView,
			const Math::Matrix& mProjection,
			const std::vector<Entity*>& lights,
			bool doSSR
		) const;
		const auto& GetConstantBuffer() { return m_constant_buffer; }

	private:
		const static int maxLights = 64;
		struct LightBuffer
		{
			Math::Matrix mvp;
			Math::Matrix viewProjectionInverse;
			Math::Vector4 dirLightColor;
			Math::Vector4 dirLightIntensity;
			Math::Vector4 dirLightDirection;
			Math::Vector4 pointLightPosition[maxLights];
			Math::Vector4 pointLightColor[maxLights];
			Math::Vector4 pointLightIntenRange[maxLights];
			Math::Vector4 spotLightColor[maxLights];
			Math::Vector4 spotLightPosition[maxLights];
			Math::Vector4 spotLightDirection[maxLights];
			Math::Vector4 spotLightIntenRangeAngle[maxLights];
			float pointLightCount;
			float spotLightCount;
			Math::Vector2 padding;
		};

		std::shared_ptr<RHI_ConstantBuffer> m_constant_buffer;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}