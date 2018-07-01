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

//= INCLUDES ==================
#include <memory>
#include <vector>
#include "RI/Backend_Def.h"
#include "../Core/EngineDefs.h"
//=============================

namespace Directus
{
	class Context;
	class Light;
	namespace Math
	{
		class Vector3;
		class Matrix;
	}

	class ENGINE_CLASS ShadowCascades
	{
	public:
		ShadowCascades(Context* context, unsigned int cascadeCount, unsigned int resolution, Light* light);
		~ShadowCascades();

		void SetAsRenderTarget(unsigned int cascadeIndex);
		Math::Matrix ComputeProjectionMatrix(unsigned int cascadeIndex);
		float GetSplit(unsigned int cascadeIndex);
		void* GetShaderResource(unsigned int cascadeIndex);
		unsigned int GetCascadeCount()	{ return m_cascadeCount; }
		unsigned int GetResolution()	{ return m_resolution; }
		void SetEnabled(bool enabled);

	private:
		void RenderTargets_Create();
		void RenderTargets_Destroy();

		std::vector<std::shared_ptr<D3D11_RenderTexture>> m_renderTargets;
		Context* m_context;
		RenderingDevice* m_renderingDevice;
		unsigned int m_resolution;
		Light* m_light;
		unsigned int m_cascadeCount;
	};
}