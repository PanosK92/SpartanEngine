/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =================
#include "Mesh.h"
#include "../RHI/RHI_Vertex.h"
#include "../Logging/Log.h"
//============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	void Mesh::Geometry_Clear()
	{
		m_vertices.clear();
		m_vertices.shrink_to_fit();
		m_indices.clear();
		m_indices.shrink_to_fit();
	}

	unsigned int Mesh::Geometry_MemoryUsage()
	{
		unsigned int size = 0;
		size += unsigned int(m_vertices.size()	* sizeof(RHI_Vertex_PosUvNorTan));
		size += unsigned int(m_indices.size()	* sizeof(unsigned int));

		return size;
	}

	void Mesh::Geometry_Get(unsigned int indexOffset, unsigned int indexCount, unsigned int vertexOffset, unsigned vertexCount, vector<unsigned int>* indices, vector<RHI_Vertex_PosUvNorTan>* vertices)
	{
		if (indexOffset == 0 || indexCount == 0 || vertexOffset == 0 || vertexCount == 0 || !vertices || !indices)
		{
			LOG_ERROR("Mesh::Geometry_Get: Invalid parameters");
			return;
		}

		// Indices
		auto indexFirst	= m_indices.begin() + indexOffset;
		auto indexLast	= m_indices.begin() + indexOffset + indexCount;
		*indices		= vector<unsigned int>(indexFirst, indexLast);

		// Vertices
		auto vertexFirst	= m_vertices.begin() + vertexOffset;
		auto vertexLast		= m_vertices.begin() + vertexOffset + vertexCount;
		*vertices			= vector<RHI_Vertex_PosUvNorTan>(vertexFirst, vertexLast);
	}

	void Mesh::Vertices_Append(const vector<RHI_Vertex_PosUvNorTan>& vertices, unsigned int* vertexOffset)
	{
		if (vertexOffset)
		{
			*vertexOffset = (unsigned int)m_vertices.size();
		}

		m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
	}

	unsigned int Mesh::Vertices_Count() const
	{
		return (unsigned int)m_vertices.size();
	}

	void Mesh::Vertex_Add(const RHI_Vertex_PosUvNorTan& vertex)
	{
		m_vertices.emplace_back(vertex);
	}

	void Mesh::Indices_Append(const vector<unsigned int>& indices, unsigned int* indexOffset)
	{
		if (indexOffset)
		{
			*indexOffset = (unsigned int)m_indices.size();
		}

		m_indices.insert(m_indices.end(), indices.begin(), indices.end());
	}
}