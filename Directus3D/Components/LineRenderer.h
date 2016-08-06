#pragma once

//= INCLUDES =============================
#include "IComponent.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include "../Core/Vertex.h"
//========================================

class __declspec(dllexport) LineRenderer : public IComponent
{
public:
	LineRenderer();
	~LineRenderer();

	virtual void Initialize();
	virtual void Start();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	//= INPUT ==================================================
	void AddLineList(const std::vector<VertexPositionColor>& lineList);
	void AddVertex(const VertexPositionColor& vertex);

	//= MISC ===================================================
	void SetBuffer();
	unsigned int GetVertexCount() { return m_maxVertices; };

private:
	void CreateBuffer();

	//= VERTICES ===================	
	D3D11Buffer* m_vertexBuffer;
	VertexPositionColor* m_vertices;
	int m_maxVertices;
	int m_vertexIndex;	
	//==============================

	//= MISC ========================
	void UpdateVertexBuffer();
	void ClearVertices();
	//==============================
};
