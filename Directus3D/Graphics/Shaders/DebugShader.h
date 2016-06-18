/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ====================
#include "../../Math/Vector3.h"
#include "../D3D11/D3D11Device.h"
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"

//===============================

class DebugShader
{
public:
	DebugShader();
	~DebugShader();

	void Initialize(D3D11Device* d3d11device);
	void Render(int vertexCount, Directus::Math::Matrix worldMatrix, Directus::Math::Matrix viewMatrix, Directus::Math::Matrix projectionMatrix, ID3D11ShaderResourceView* depthMap);

private:
	struct DefaultBuffer
	{
		Directus::Math::Matrix worldViewProjection;
		Directus::Math::Matrix viewProjection;
	};

	D3D11Buffer* m_miscBuffer;

	void SetShaderBuffers(Directus::Math::Matrix worldMatrix, Directus::Math::Matrix viewMatrix, Directus::Math::Matrix projectionMatrix, ID3D11ShaderResourceView* depthMap);
	void RenderShader(unsigned int vertexCount);

	D3D11Device* m_D3D11Device;
	D3D11Shader* m_shader;
};
