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

//================================
#include "pch.h"
#include "PhysicsDebugDraw.h"
#include "../Rendering/Renderer.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        uint32_t debug_mode = 0;
    }

    PhysicsDebugDraw::PhysicsDebugDraw()
    {
        debug_mode =
            DBG_DrawFrames        | // axes of the coordinate frames
            DBG_DrawWireframe     | // shapes
            DBG_DrawContactPoints |
            DBG_DrawConstraints   |
            DBG_DrawConstraintLimits;
    }

    void PhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color_from, const btVector3& color_to)
    {
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        // a bit dangerous to reinterpret these parameters but this is a performance critical path
        // a better way would be to use a custom physics debug draw since the one from Bullet is extremely slow
        Renderer::DrawLine(
            reinterpret_cast<const Vector3&>(from),
            reinterpret_cast<const Vector3&>(to),
            Color(color_from.getX(), color_from.getY(), color_from.getZ(), 1.0f),
            Color(color_to.getX(), color_to.getY(), color_to.getZ(), 1.0f)
        );
    }

    void PhysicsDebugDraw::drawContactPoint(const btVector3& point_on_b, const btVector3& normal_on_b, btScalar distance, int life_time, const btVector3& color)
    {
        const btVector3& from = point_on_b;
        const btVector3  to   = point_on_b + normal_on_b * distance;

        drawLine(from, to, color);
    }

    void PhysicsDebugDraw::reportErrorWarning(const char* error_warning)
    {
        SP_LOG_WARNING("%s", error_warning);
    }

    void PhysicsDebugDraw::setDebugMode(const int debugMode)
    {
        debug_mode = debugMode;
    }

    int PhysicsDebugDraw::getDebugMode() const
    {
        return debug_mode;
    }
}
