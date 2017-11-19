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
		m_id = GENERATE_GUID;
		m_name = NOT_ASSIGNED;
		m_gameObjID = NOT_ASSIGNED_HASH;
		m_modelID = NOT_ASSIGNED_HASH;
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
	void Mesh::Serialize(StreamIO* stream)
	{
		stream->Write(m_id);
		stream->Write(m_gameObjID);
		stream->Write(m_modelID);
		stream->Write(m_name);
		stream->Write(m_vertexCount);
		stream->Write(m_indexCount);
		stream->Write(m_triangleCount);

		for (const auto& vertex : m_vertices)
		{
			stream->Write(vertex);
		}

		for (const auto& index : m_indices)
		{
			stream->Write(index);
		}
	}

	void Mesh::Deserialize(StreamIO* stream)
	{
		stream->Read(m_id);
		stream->Read(m_gameObjID);
		stream->Read(m_modelID);
		stream->Read(m_name);
		stream->Read(m_vertexCount);
		stream->Read(m_indexCount);
		stream->Read(m_triangleCount);

		for (unsigned int i = 0; i < m_vertexCount; i++)
		{
			m_vertices.emplace_back(VertexPosTexTBN());
			stream->Read(m_vertices.back());
		}

		for (unsigned int i = 0; i < m_indexCount; i++)
		{
			m_indices.emplace_back(stream->ReadInt());
		}

		m_boundingBox.ComputeFromMesh(this);
	}

	void Mesh::SetVertices(const vector<VertexPosTexTBN>& vertices)
	{
		m_vertices = vertices;
		Update();
	}

	void Mesh::SetIndices(const vector<unsigned int>& indices)
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