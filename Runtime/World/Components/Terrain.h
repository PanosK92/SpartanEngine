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

//= INCLUDES ========================
#include "IComponent.h"
#include "../../RHI/RHI_Definition.h"
#include <vector>
//===================================

namespace Spartan
{
    class Model;

    class SPARTAN_CLASS Terrain : public IComponent
    {
    public:
        Terrain(Context* context, Entity* entity, uint32_t id = 0);
        ~Terrain() = default;

        //= IComponent ==============
        void OnInitialize() override;
        //===========================

        const auto& GetHeightMap() { return m_height_map; }
        bool SetHeightMap(const std::shared_ptr<RHI_Texture>& height_map);

    private:     
        uint32_t m_width    = 0;
        uint32_t m_height   = 0;
        float m_min_z       = 0.0f;
        float m_max_z       = 1.0f;
        std::shared_ptr<RHI_Texture> m_height_map;
        std::shared_ptr<Model> m_model;
    };
}
