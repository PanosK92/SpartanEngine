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

//= INCLUDES =============================
#include "IComponent.h"
#include "../Math/Vector3.h"
#include "../Core/Vertex.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include <memory>
//========================================

class __declspec(dllexport) LineRenderer : public IComponent
{
public:
	LineRenderer();
	~LineRenderer();

	//= INTERFACE ============
	virtual void Initialize();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	//= INPUT ==============================================================================================
	void AddLineList(std::vector<VertexPositionColor> vertices);
	void AddLine(Directus::Math::Vector3 start, Directus::Math::Vector3 end, Directus::Math::Vector4 color);
	void AddVertex(Directus::Math::Vector3 point, Directus::Math::Vector4 color);

	//= PROPERTIES ===============
	void SetBuffer();
	unsigned int GetVertexCount();

private:
	std::vector<VertexPositionColor> m_vertices;
	std::shared_ptr<D3D11Buffer> m_vertexBuffer;
	int m_maximumVertices = 1000000;

	//= MISC =================
	void UpdateVertexBuffer();
	void ClearVertices();
};
