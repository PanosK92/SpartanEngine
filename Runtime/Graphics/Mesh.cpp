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
#include "../Core/Context.h"
#include "D3D11/D3D11VertexBuffer.h"
#include "D3D11/D3D11IndexBuffer.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Mesh::Mesh(Context* context)
	{
		m_context = context;
		m_id = GENERATE_GUID;
		m_name = NOT_ASSIGNED;
		m_gameObjID = NOT_ASSIGNED_HASH;
		m_modelID = NOT_ASSIGNED_HASH;
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
		m_boundingBox = BoundingBox();
		m_memoryUsageKB = 0;
	}

	Mesh::~Mesh()
	{
		Clear();
	}

	void Mesh::ClearVerticesAndIndices()
	{
		m_vertices.clear();
		m_vertices.shrink_to_fit();
		m_indices.clear();
		m_indices.shrink_to_fit();
	}

	void Mesh::Clear()
	{
		ClearVerticesAndIndices();
		m_name = NOT_ASSIGNED;
		m_gameObjID = NOT_ASSIGNED_HASH;
		m_modelID = NOT_ASSIGNED_HASH;
		m_name.clear();
		m_vertexCount = 0;
		m_indexCount = 0;
		m_triangleCount = 0;
	}

	//= IO =============================================
	void Mesh::Serialize(StreamIO* stream)
	{
		stream->Write(m_id);
		stream->Write(m_gameObjID);
		stream->Write(m_modelID);
		stream->Write(m_name);
		stream->Write(m_vertices);
		stream->Write(m_indices);
	}

	void Mesh::Deserialize(StreamIO* stream)
	{
		Clear();

		stream->Read(&m_id);
		stream->Read(&m_gameObjID);
		stream->Read(&m_modelID);
		stream->Read(&m_name);
		stream->Read(&m_vertices);
		stream->Read(&m_indices);

		Construct();
	}
	//==================================================

	bool Mesh::Construct()
	{
		if (m_vertices.empty())
		{
			LOG_WARNING("Mesh: Can't set vertex buffer. \"" + m_name + "\" doesn't have an initialized vertex buffer.");
		}

		if (m_indices.empty())
		{
			LOG_WARNING("Mesh: Can't set vertex buffer. \"" + m_name + "\" doesn't have an initialized vertex buffer.");
		}

		if (m_vertices.empty() || m_indices.empty())
			return false;

		m_memoryUsageKB = ComputeMemoryUsageKB();
		m_vertexCount = (unsigned int)m_vertices.size();
		m_indexCount = (unsigned int)m_indices.size();
		m_triangleCount = m_indexCount / 3;
		m_boundingBox.ComputeFromMesh(this);

		if (!ConstructBuffers())
		{
			return false;
		}	

		//ClearVerticesAndIndices();

		return true;
	}

	bool Mesh::SetBuffers()
	{
		if (!m_vertexBuffer)
		{
			LOG_WARNING("Mesh: Can't set vertex buffer. \"" + m_name + "\" doesn't have an initialized vertex buffer.");
		}

		if (!m_indexBuffer)
		{
			LOG_WARNING("Mesh: Can't set index buffer. \"" + m_name + "\" doesn't have an initialized index buffer.");
		}

		if (!m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();

		// Set the type of primitive that should be rendered from mesh
		m_context->GetSubsystem<Graphics>()->SetPrimitiveTopology(TriangleList);

		return true;
	}

	//= HELPER FUNCTIONS ===========================================================
	bool Mesh::ConstructBuffers()
	{
		auto graphics = m_context->GetSubsystem<Graphics>();
		if (!graphics->GetDevice())
		{
			LOG_ERROR("Mesh: Aborting vertex buffer creation. Graphics device is not present.");
			return false;
		}

		m_vertexBuffer.reset();
		m_indexBuffer.reset();

		m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphics);
		if (!m_vertexBuffer->Create(m_vertices))
		{
			LOG_ERROR("Mesh: Failed to create vertex buffer \"" + m_name + "\".");
			return false;
		}

		m_indexBuffer = make_shared<D3D11IndexBuffer>(graphics);
		if (!m_indexBuffer->Create(m_indices))
		{
			LOG_ERROR("MeshFilter: Failed to create index buffer \"" + m_name + "\".");
			return false;
		}

		return true;
	}

	unsigned int Mesh::ComputeMemoryUsageKB()
	{
		unsigned int sizeKB = 0;
		sizeKB += m_vertices.size() * sizeof(VertexPosTexTBN);
		sizeKB += m_indices.size() * sizeof(unsigned int);

		return sizeKB / 1000;
	}
	//==============================================================================
}