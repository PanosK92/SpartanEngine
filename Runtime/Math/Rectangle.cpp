/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =======================
#include "Spartan.h"
#include "../Rendering/Renderer.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Vertex.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::Math
{
    const Rectangle Rectangle::Zero(0.0f, 0.0f, 0.0f, 0.0f);

    bool Rectangle::CreateBuffers(Renderer* renderer)
    {
        SP_ASSERT(renderer != nullptr);

        // Compute screen coordinates
        const RHI_Viewport& viewport = renderer->GetViewport();
        const float sc_left          = -(viewport.width * 0.5f) + left;
        const float sc_right         = sc_left + Width();
        const float sc_top           = (viewport.height * 0.5f) - top;
        const float sc_bottom        = sc_top - Height();

        // Create vertex buffer
        const RHI_Vertex_PosTex vertices[6] = 
        {
            // First triangle
            RHI_Vertex_PosTex(Vector3(sc_left,  sc_top,    0.0f), Vector2(0.0f, 0.0f)), // Top left
            RHI_Vertex_PosTex(Vector3(sc_right, sc_bottom, 0.0f), Vector2(1.0f, 1.0f)), // Bottom right
            RHI_Vertex_PosTex(Vector3(sc_left,  sc_bottom, 0.0f), Vector2(0.0f, 1.0f)), // Bottom left
            // Second triangle
            RHI_Vertex_PosTex(Vector3(sc_left,  sc_top,    0.0f), Vector2(0.0f, 0.0f)), // Top left
            RHI_Vertex_PosTex(Vector3(sc_right, sc_top,    0.0f), Vector2(1.0f, 0.0f)), // Top right
            RHI_Vertex_PosTex(Vector3(sc_right, sc_bottom, 0.0f), Vector2(1.0f, 1.0f))  // Bottom right
        };

        m_vertex_buffer = make_shared<RHI_VertexBuffer>(renderer->GetRhiDevice());
        if (!m_vertex_buffer->Create(vertices, 6))
        {
            LOG_ERROR("Failed to create vertex buffer.");
            return false;
        }

        // Create index buffer
        const uint32_t indices[6] = { 0, 1, 2, 3, 4, 5 };

        m_index_buffer = make_shared<RHI_IndexBuffer>(renderer->GetRhiDevice());
        if (!m_index_buffer->Create(indices, 6))
        {
            LOG_ERROR("Failed to create index buffer.");
            return false;
        }

        return true;
    }
}
