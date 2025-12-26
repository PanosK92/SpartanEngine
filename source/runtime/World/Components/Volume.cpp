/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ========================
#include "pch.h"
#include "Volume.h"
#include "../Entity.h"
#include "../../Core/Engine.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    Volume::Volume(Entity* entity) : Component(entity)
    {
        // unit cube
        m_bounding_box = BoundingBox::Unit;
    }

    void Volume::Tick()
    {
        // only draw in editor mode (not playing)
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        // transform the bounding box by the entity's transform matrix
        const Matrix& entity_matrix       = GetEntity()->GetMatrix();
        const BoundingBox transformed_box = m_bounding_box * entity_matrix;

        // draw the volume using the renderer
        Renderer::DrawBox(transformed_box);
    }

    void Volume::SetOption(Renderer_Option option, float value)
    {
        m_options[option] = value;
    }

    void Volume::RemoveOption(Renderer_Option option)
    {
        m_options.erase(option);
    }

    float Volume::GetOption(Renderer_Option option) const
    {
        // try to find the specific override
        auto it = m_options.find(option);
        if (it != m_options.end())
        {
            return it->second;
        }

        return 0.0f; 
    }
}
