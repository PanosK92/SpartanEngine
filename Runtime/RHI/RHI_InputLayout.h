/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include <memory>
#include <vector>
#include "RHI_Definition.h"
#include "RHI_Vertex.h"
#include "../Core/SpartanObject.h"
#include "../Logging/Log.h"
//================================

namespace Spartan
{
    struct VertexAttribute 
    {
        VertexAttribute(const std::string& name, const uint32_t location, const uint32_t binding, const RHI_Format format, const uint32_t offset)
        {
            this->name     = name;
            this->location = location;
            this->binding  = binding;
            this->format   = format;
            this->offset   = offset;
        }

        std::string name;
        uint32_t location;
        uint32_t binding;
        RHI_Format format;
        uint32_t offset;
    };

    class SPARTAN_CLASS RHI_InputLayout : public SpartanObject
    {
    public:
        RHI_InputLayout(const std::shared_ptr<RHI_Device>& rhi_device)
        {
            m_rhi_device = rhi_device;
        }

        ~RHI_InputLayout();

        bool Create(const RHI_Vertex_Type vertex_type, void* vertex_shader_blob = nullptr)
        {
            const uint32_t binding = 0;

            if (vertex_type == RHI_Vertex_Type::Undefined)
            {
                m_vertex_size = sizeof(RHI_Vertex_Undefined);
            }
            else if (vertex_type == RHI_Vertex_Type::Pos)
            {
                m_vertex_attributes =
                {
                    { "POSITION", 0, binding, RHI_Format_R32G32B32_Float, offsetof(RHI_Vertex_Pos, pos) }
                };

                m_vertex_size = sizeof(RHI_Vertex_Pos);
            }
            else if (vertex_type == RHI_Vertex_Type::PosTex)
            {
                m_vertex_attributes =
                {
                    { "POSITION", 0, binding, RHI_Format_R32G32B32_Float, offsetof(RHI_Vertex_PosTex, pos) },
                    { "TEXCOORD", 1, binding, RHI_Format_R32G32_Float,    offsetof(RHI_Vertex_PosTex, tex) }
                };

                m_vertex_size = sizeof(RHI_Vertex_PosTex);
            }
            else if (vertex_type == RHI_Vertex_Type::PosCol)
            {
                m_vertex_attributes =
                {
                    { "POSITION",    0, binding, RHI_Format_R32G32B32_Float,    offsetof(RHI_Vertex_PosCol, pos) },
                    { "COLOR",       1, binding, RHI_Format_R32G32B32A32_Float, offsetof(RHI_Vertex_PosCol, col) }
                };

                m_vertex_size = sizeof(RHI_Vertex_PosCol);
            }
            else if (vertex_type == RHI_Vertex_Type::Pos2dTexCol8)
            {
                m_vertex_attributes =
                {
                    { "POSITION",    0, binding, RHI_Format_R32G32_Float,   offsetof(RHI_Vertex_Pos2dTexCol8, pos) },
                    { "TEXCOORD",    1, binding, RHI_Format_R32G32_Float,   offsetof(RHI_Vertex_Pos2dTexCol8, tex) },
                    { "COLOR",       2, binding, RHI_Format_R8G8B8A8_Unorm, offsetof(RHI_Vertex_Pos2dTexCol8, col) }
                };

                m_vertex_size = sizeof(RHI_Vertex_Pos2dTexCol8);
            }
            else if (vertex_type == RHI_Vertex_Type::PosTexNorTan)
            {
                m_vertex_attributes =
                {
                    { "POSITION",   0, binding, RHI_Format_R32G32B32_Float, offsetof(RHI_Vertex_PosTexNorTan, pos) },
                    { "TEXCOORD",   1, binding, RHI_Format_R32G32_Float,    offsetof(RHI_Vertex_PosTexNorTan, tex) },
                    { "NORMAL",     2, binding, RHI_Format_R32G32B32_Float, offsetof(RHI_Vertex_PosTexNorTan, nor) },
                    { "TANGENT",    3, binding, RHI_Format_R32G32B32_Float, offsetof(RHI_Vertex_PosTexNorTan, tan) }
                };

                m_vertex_size = sizeof(RHI_Vertex_PosTexNorTan);
            }


            if (vertex_shader_blob && !m_vertex_attributes.empty())
            {
                return _CreateResource(vertex_shader_blob);
            }

            return true;
        }

        RHI_Vertex_Type GetVertexType()                                const { return m_vertex_type; }
        const uint32_t GetVertexSize()                                 const { return m_vertex_size; }
        const std::vector<VertexAttribute>& GetAttributeDescriptions() const { return m_vertex_attributes; }
        uint32_t GetAttributeCount()                                   const { return static_cast<uint32_t>(m_vertex_attributes.size()); }
        void* GetResource()                                            const { return m_resource; }

        bool operator==(const RHI_InputLayout& rhs) const { return m_vertex_type == rhs.GetVertexType(); }

    private:
        RHI_Vertex_Type m_vertex_type;
        uint32_t m_vertex_size;

        // API
        bool _CreateResource(void* vertex_shader_blob);
        std::shared_ptr<RHI_Device> m_rhi_device;
        void* m_resource = nullptr;
        std::vector<VertexAttribute> m_vertex_attributes;
    };
}
