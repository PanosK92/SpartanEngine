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

//= INCLUDES =============================
#include "../../RHI/RHI_Definition.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector4.h"
#include "../../World/Components/Camera.h"
#include "../../World/Components/Light.h"
#include "../../Resource/ResourceCache.h"
#include "../../RHI/RHI_Shader.h"
//========================================

namespace Directus
{
	class LightShader : public RHI_Shader
	{
	public:
		LightShader(std::shared_ptr<RHI_Device> rhiDevice);
		~LightShader() {}

		void UpdateConstantBuffer(
			const Math::Matrix& mViewProjection_Orthographic,
			const Math::Matrix& mView,
			const Math::Matrix& mProjection,
			const std::vector<Entity*>& lights,
			bool doSSR
		);

		std::shared_ptr<RHI_ConstantBuffer> GetConstantBuffer()	{ return m_cbuffer; }

	private:
		const static int maxLights = 64;
		struct LightBuffer
		{
			Math::Matrix mvp;
			Math::Matrix viewProjectionInverse;
			
			//= DIRECTIONAL LIGHT ==========	
			Math::Vector4 dirLightColor;
			Math::Vector4 dirLightIntensity;
			Math::Vector4 dirLightDirection;
			//==============================

			//= POINT LIGHTS =============================
			Math::Vector4 pointLightPosition[maxLights];
			Math::Vector4 pointLightColor[maxLights];
			Math::Vector4 pointLightIntenRange[maxLights];
			//============================================

			//= SPOT LIGHTS ==================================
			Math::Vector4 spotLightColor[maxLights];
			Math::Vector4 spotLightPosition[maxLights];
			Math::Vector4 spotLightDirection[maxLights];
			Math::Vector4 spotLightIntenRangeAngle[maxLights];
			//================================================

			float pointLightCount;
			float spotLightCount;
			Math::Vector2 padding;
		};

		std::shared_ptr<RHI_ConstantBuffer> m_cbuffer;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}