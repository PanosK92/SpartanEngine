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

#pragma once

//= INCLUDES ==================
#include <vector>
#include "../RHI/RHI_Definition.h"
//=============================

namespace Directus
{
	class Mesh
	{
	public:
		Mesh() {}
		~Mesh() { Geometry_Clear(); }

		// Geometry
		void Geometry_Clear();
		void Geometry_Get(
			unsigned int indexOffset,
			unsigned int indexCount,
			unsigned int vertexOffset,
			unsigned vertexCount,
			std::vector<unsigned int>* indices,
			std::vector<RHI_Vertex_PosUvNorTan>* vertices
		);
		unsigned int Geometry_MemoryUsage();

		// Vertices
		void Vertex_Add(const RHI_Vertex_PosUvNorTan& vertex);
		void Vertices_Append(const std::vector<RHI_Vertex_PosUvNorTan>& vertices, unsigned int* vertexOffset);	
		unsigned int Vertices_Count() const;
		std::vector<RHI_Vertex_PosUvNorTan>& Vertices_Get()						{ return m_vertices; }
		void Vertices_Set(const std::vector<RHI_Vertex_PosUvNorTan>& vertices)	{ m_vertices = vertices; }

		// Indices
		void Index_Add(unsigned int index)							{ m_indices.emplace_back(index); }
		std::vector<unsigned int>& Indices_Get()					{ return m_indices; }
		void Indices_Set(const std::vector<unsigned int>& indices)	{ m_indices = indices; }
		unsigned int Indices_Count() const							{ return (unsigned int)m_indices.size(); }
		void Indices_Append(const std::vector<unsigned int>& indices, unsigned int* indexOffset);
	
		// Misc
		unsigned int GetTriangleCount() const { return Indices_Count() / 3; }	
		
	private:
		std::vector<RHI_Vertex_PosUvNorTan> m_vertices;
		std::vector<unsigned int> m_indices;
	};
}