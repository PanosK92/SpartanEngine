/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ========================
#include "../Graphics/Mesh.h"
#include "../Logging/Log.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../IO/StreamIO.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Mesh::Mesh()
	{
		// Mesh	
		m_id = GENERATE_GUID;
		m_name = DATA_NOT_ASSIGNED;
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
		m_boundingBox = BoundingBox();
		m_onUpdate = nullptr;
	}

	Mesh::~Mesh()
	{
		m_vertices.clear();
		m_indices.clear();
		m_name.clear();
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
	}

	//= IO =========================================================================
	void Mesh::Serialize()
	{
		StreamIO::WriteSTR(m_id);
		StreamIO::WriteSTR(m_gameObjID);
		StreamIO::WriteSTR(m_modelID);
		StreamIO::WriteSTR(m_name);
		StreamIO::WriteInt(m_vertexCount);
		StreamIO::WriteInt(m_indexCount);
		StreamIO::WriteInt(m_triangleCount);

		for (const auto& vertex : m_vertices)
		{
			SaveVertex(vertex);
		}

		for (const auto& index : m_indices)
		{
			StreamIO::WriteInt(index);
		}
	}

	void Mesh::Deserialize()
	{
		m_id = StreamIO::ReadSTR();
		m_gameObjID = StreamIO::ReadSTR();
		m_modelID = StreamIO::ReadSTR();
		m_name = StreamIO::ReadSTR();
		m_vertexCount = StreamIO::ReadInt();
		m_indexCount = StreamIO::ReadInt();
		m_triangleCount = StreamIO::ReadInt();

		for (unsigned int i = 0; i < m_vertexCount; i++)
		{
			m_vertices.push_back(VertexPosTexNorTan());
			LoadVertex(m_vertices.back());
		}

		for (unsigned int i = 0; i < m_indexCount; i++)
		{
			m_indices.push_back(StreamIO::ReadInt());
		}

		m_boundingBox.ComputeFromMesh(this);
	}

	void Mesh::SetVertices(const vector<VertexPosTexNorTan>& vertices)
	{
		m_vertices = vertices;
		Update();
	}

	void Mesh::SetIndices(const vector<unsigned>& indices)
	{
		m_indices = indices;
		Update();
	}
	//==============================================================================

	//= PROCESSING =================================================================
	void Mesh::Update()
	{
		m_vertexCount = (unsigned int)m_vertices.size();
		m_indexCount = (unsigned int)m_indices.size();
		m_triangleCount = m_indexCount / 3;

		m_boundingBox.ComputeFromMesh(this);

		if (m_onUpdate)
		{
			m_onUpdate();
		}
	}

	// This is attached to CreateBuffers() which is part of the MeshFilter component.
	// Whenever something changes, the buffers are auto-updated.
	void Mesh::SubscribeToUpdate(function<void()> function)
	{
		m_onUpdate = function;

		if (m_onUpdate)
		{
			m_onUpdate();
		}
	}

	void Mesh::SetScale(float scale)
	{
		SetScale(this, scale);
		Update();
	}
	//==============================================================================

	//= IO =========================================================================
	void Mesh::SaveVertex(const VertexPosTexNorTan& vertex)
	{
		StreamIO::WriteFloat(vertex.position.x);
		StreamIO::WriteFloat(vertex.position.y);
		StreamIO::WriteFloat(vertex.position.z);

		StreamIO::WriteFloat(vertex.uv.x);
		StreamIO::WriteFloat(vertex.uv.y);

		StreamIO::WriteFloat(vertex.normal.x);
		StreamIO::WriteFloat(vertex.normal.y);
		StreamIO::WriteFloat(vertex.normal.z);

		StreamIO::WriteFloat(vertex.tangent.x);
		StreamIO::WriteFloat(vertex.tangent.y);
		StreamIO::WriteFloat(vertex.tangent.z);
	}

	void Mesh::LoadVertex(VertexPosTexNorTan& vertex)
	{
		vertex.position.x = StreamIO::ReadFloat();
		vertex.position.y = StreamIO::ReadFloat();
		vertex.position.z = StreamIO::ReadFloat();

		vertex.uv.x = StreamIO::ReadFloat();
		vertex.uv.y = StreamIO::ReadFloat();

		vertex.normal.x = StreamIO::ReadFloat();
		vertex.normal.y = StreamIO::ReadFloat();
		vertex.normal.z = StreamIO::ReadFloat();

		vertex.tangent.x = StreamIO::ReadFloat();
		vertex.tangent.y = StreamIO::ReadFloat();
		vertex.tangent.z = StreamIO::ReadFloat();
	}
	//==============================================================================

	//= HELPER FUNCTIONS ===========================================================
	void Mesh::SetScale(Mesh* meshData, float scale)
	{
		for (unsigned int i = 0; i < meshData->GetVertexCount(); i++)
		{
			meshData->GetVertices()[i].position *= scale;
		}
	}
	//==============================================================================
}