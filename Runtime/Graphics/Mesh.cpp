/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../FileSystem/FileSystem.h"
#include "../IO/FileStream.h"
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
	Mesh::Mesh(Context* context) : IResource(context)
	{
		//= IResource ===========
		RegisterResource<Mesh>();
		//=======================

		m_modelName		= NOT_ASSIGNED_HASH;
		m_vertexCount	= 0;
		m_indexCount	= 0;
		m_triangleCount = 0;
		m_boundingBox	= BoundingBox();
	}

	Mesh::~Mesh()
	{
		Clear();
	}

	void Mesh::ClearGeometry()
	{
		m_vertices.clear();
		m_vertices.shrink_to_fit();
		m_indices.clear();
		m_indices.shrink_to_fit();
	}

	void Mesh::Clear()
	{
		ClearGeometry();
		m_modelName		= NOT_ASSIGNED_HASH;
		m_vertexCount	= 0;
		m_indexCount	= 0;
		m_triangleCount = 0;
	}

	//= RESOURCE ==============================================================
	bool Mesh::LoadFromFile(const string& filePath)
	{
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		Clear();

		file->Read(&m_vertices);
		file->Read(&m_indices);
		file->Read(&m_modelName);
		file->Read(&m_resourceName);
		
		Construct();
		ClearGeometry();

		return true;
	}

	bool Mesh::SaveToFile(const string& filePath)
	{
		// If the geometry data has been cleared, load it again
		// as we don't want to replaced existing data with nothing.
		// If the geometry data is not cleared, it won't be loaded again.
		GetGeometry(&m_vertices, &m_indices);

		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		file->Write(m_vertices);
		file->Write(m_indices);
		file->Write(m_modelName);
		file->Write(m_resourceName);

		return true;
	}

	unsigned int Mesh::GetMemory()
	{
		// Vertices & Indices
		unsigned int size = 0;
		size += unsigned int(m_vertices.size() * sizeof(VertexPosTexTBN));
		size += unsigned int(m_indices.size() * sizeof(unsigned int));

		// Buffers
		size += m_vertexBuffer->GetMemoryUsage();
		size += m_indexBuffer->GetMemoryUsage();

		return size;
	}

	void Mesh::GetGeometry(vector<VertexPosTexTBN>* vertices, vector<unsigned>* indices)
	{
		if (!m_vertices.empty() && !m_indices.empty())
		{
			vertices = &m_vertices;
			indices = &m_indices;
			return;
		}

		auto file = make_unique<FileStream>(m_resourceFilePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return;

		// Read vertices and indices
		file->Read(vertices);
		file->Read(indices);
	}

	//=========================================================================

	bool Mesh::Construct()
	{
		m_vertexCount	= (unsigned int)m_vertices.size();
		m_indexCount	= (unsigned int)m_indices.size();
		m_triangleCount = m_indexCount / 3;
		m_boundingBox.ComputeFromVertices(m_vertices);
		return ConstructBuffers();
	}

	bool Mesh::SetBuffers()
	{
		bool success = true;
		if (m_vertexBuffer)
		{
			m_vertexBuffer->SetIA();
		}
		else
		{
			LOG_WARNING("Mesh: Can't set vertex buffer. \"" + m_resourceName + "\" doesn't have an initialized vertex buffer.");
			success = false;
		}

		if (m_indexBuffer)
		{
			m_indexBuffer->SetIA();
		}
		else
		{
			LOG_WARNING("Mesh: Can't set index buffer. \"" + m_resourceName + "\" doesn't have an initialized index buffer.");
			success = false;
		}

		// Set the type of primitive that should be rendered from mesh
		m_context->GetSubsystem<Graphics>()->SetPrimitiveTopology(TriangleList);

		return success;
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

		bool success = true;

		if (!m_vertices.empty())
		{
			m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphics);
			if (!m_vertexBuffer->Create(m_vertices))
			{
				LOG_ERROR("Mesh: Failed to create vertex buffer for \"" + m_resourceName + "\".");
				success = false;
			}
		}
		else
		{
			LOG_ERROR("Mesh: Can't create vertex buffer for \"" + m_resourceName + "\". Provided vertices are empty.");
			success = false;
		}

		if (!m_indices.empty())
		{
			m_indexBuffer = make_shared<D3D11IndexBuffer>(graphics);
			if (!m_indexBuffer->Create(m_indices))
			{
				LOG_ERROR("MeshFilter: Failed to create index buffer for \"" + m_resourceName + "\".");
				success = false;
			}
		}
		else
		{
			LOG_ERROR("Mesh: Can't create index buffer for \"" + m_resourceName + "\". Provided indices are empty.");
			success = false;
		}

		return success;
	}
	//==============================================================================
}