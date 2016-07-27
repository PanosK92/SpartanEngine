#pragma once

//= INCLUDES ================
#include "IComponent.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include "../Core/Vertex.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
//===========================

class __declspec(dllexport) LineRenderer : public IComponent
{
public:
	LineRenderer();
	~LineRenderer();

	virtual void Initialize();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	//= INPUT ==================================================
	void AddLineList(std::vector<VertexPositionColor> lineList);
	void AddVertex(VertexPositionColor vertex);

	//= MISC ===================================================
	void SetBuffer();
	unsigned int GetVertexCount() { return m_maxVertices; };

private:
	//= VERTICES ===================	
	ID3D11Buffer* m_vertexBuffer;
	VertexPositionColor* m_vertices;
	int m_maxVertices = 1000000;
	int m_vertexIndex;	
	//==============================

	//= MISC ========================
	void CreateDynamicVertexBuffer();
	void UpdateVertexBuffer();
	void ClearVertices();
	//==============================
};
