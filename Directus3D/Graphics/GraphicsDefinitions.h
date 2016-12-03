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

#if defined(D3D12)

class ID3D12Device;
class ID3D12DeviceContext;
class D3D12Graphics;

typedef ID3D12Device Device;
typedef ID3D12DeviceContext DeviceContext;
typedef D3D12Graphics GraphicsAPI;

#elif defined(D3D11)

class ID3D11Device;
class ID3D11DeviceContext;
class D3D11Graphics;

typedef ID3D11Device Device;
typedef ID3D11DeviceContext DeviceContext;
typedef D3D11Graphics GraphicsAPI;

#endif

enum InputLayout
{
	Auto,
	Position,
	PositionColor,
	PositionTexture,
	PositionTextureNormalTangent
};

enum CullMode
{
	CullBack,
	CullFront,
	CullNone,
};

enum PrimitiveTopology
{
	TriangleList,
	LineList
};