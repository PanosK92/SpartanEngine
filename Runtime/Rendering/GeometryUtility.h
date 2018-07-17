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

//= INCLUDES ==================
#include <vector>
#include "../RHI/RHI_Definition.h"
#include "../Core/EngineDefs.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS GeometryUtility
	{
	public:
		static void CreateCube(std::vector<RHI_Vertex_PosUVTBN>* vertices, std::vector<unsigned int>* indices);
		static void CreateQuad(std::vector<RHI_Vertex_PosUVTBN>* vertices, std::vector<unsigned int>* indices);
		static void CreateSphere(std::vector<RHI_Vertex_PosUVTBN>* vertices, std::vector<unsigned int>* indices, float radius = 1.0f, int slices = 15, int stacks = 15);
		static void CreateCylinder(std::vector<RHI_Vertex_PosUVTBN>* vertices, std::vector<unsigned int>* indices, float radiusTop = 1.0f, float radiusBottom = 1.0f, float height = 1.0f, int slices = 15, int stacks = 15);
		static void CreateCone(std::vector<RHI_Vertex_PosUVTBN>* vertices, std::vector<unsigned int>* indices, float radius = 1.0f, float height = 2.0f);
	};
}