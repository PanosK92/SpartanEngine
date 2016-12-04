#pragma once

//= INCLUDES =============================
#include "IComponent.h"
#include "../Graphics/D3D11/D3D11Buffer.h"
#include "../Graphics/Vertex.h"
#include <memory>

//========================================

class DllExport LineRenderer : public IComponent
{
public:
	LineRenderer();
	~LineRenderer();

	virtual void Reset();
	virtual void Start();
	virtual void OnDisable();
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
	std::shared_ptr<D3D11Buffer> m_vertexBuffer;
	VertexPositionColor* m_vertices;
	int m_maxVertices;
	int m_vertexIndex;	
	//==============================

	//= MISC ========================
	void UpdateVertexBuffer();
	void ClearVertices();
	//==============================
};
