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

//= INCLUDES ==============
#include "RHI_Definition.h"
//=========================

namespace Directus
{
	struct RHI_PipelineState
	{
		RHI_PipelineState() {}

		PrimitiveTopology_Mode primitiveTopology;
		Cull_Mode cullMode;
		Fill_Mode fillMode;
		std::shared_ptr<RHI_Sampler> sampler;
		std::shared_ptr<RHI_Shader> vertexShader;
		std::shared_ptr<RHI_Shader> pixelShader;
		std::shared_ptr<RHI_IndexBuffer> indexBuffer;
		std::shared_ptr<RHI_VertexBuffer> vertexBuffer;
		std::shared_ptr<RHI_ConstantBuffer> constantBuffer;
		std::shared_ptr<RHI_Viewport> viewport;
	};
}