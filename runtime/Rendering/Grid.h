/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =====================
#include <vector>
#include <memory>
#include "../RHI/RHI_Definition.h"
#include "../Math/Matrix.h"
#include "../Core/Definitions.h"
//================================

namespace Spartan
{
    class Context;
    class Transform;

    class SP_CLASS Grid
    {
    public:
        Grid(RHI_Device* rhi_device);
        ~Grid() = default;
        
        const Math::Matrix& ComputeWorldMatrix(Transform* camera);
        const auto& GetVertexBuffer()   const { return m_vertex_buffer; }
        const uint32_t GetVertexCount() const { return m_vertex_count; }

    private:
        void BuildGrid(std::vector<RHI_Vertex_PosCol>* vertices);

        uint32_t m_terrain_height = 200;
        uint32_t m_terrain_width  = 200;
        uint32_t m_vertex_count   = 0;

        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
        Math::Matrix m_world;
    };
}
