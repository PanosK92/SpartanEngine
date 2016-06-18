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

//= INCLUDES ===========
#include "D3D11Device.h"

//======================

enum InputLayout
{
	Position,
	PositionColor,
	PositionTexture,
	PositionTextureNormalTangent
};

class D3D11InputLayout
{
public:
	D3D11InputLayout();
	~D3D11InputLayout();

	void Initialize(D3D11Device* d3d11Device);
	bool Create(ID3D10Blob* VSBlob, InputLayout layout);
	void Set();

private:
	D3D11Device* m_D3D11Device;
	ID3D11InputLayout* m_layout;

	/*------------------------------------------------------------------------------
								[LAYOUTS]
	------------------------------------------------------------------------------*/
	bool CreatePos(ID3D10Blob* VSBlob);
	bool CreatePosCol(ID3D10Blob* VSBlob);
	bool CreatePosTex(ID3D10Blob* VSBlob);
	bool CreatePosTexNorTan(ID3D10Blob* VSBlob);

	/*------------------------------------------------------------------------------
								[MISC]
	------------------------------------------------------------------------------*/
	bool CreateLayout(D3D11_INPUT_ELEMENT_DESC vertexInputLayout[], unsigned int elementCount, ID3D10Blob* VSBlob);
};
