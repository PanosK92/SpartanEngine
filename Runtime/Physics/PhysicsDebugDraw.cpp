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

//================================
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Rendering/Renderer.h"
#include "../Logging/Log.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	PhysicsDebugDraw::PhysicsDebugDraw(Renderer* renderer)
	{
		m_renderer	= renderer;
		m_debugMode = DBG_DrawWireframe | DBG_DrawContactPoints | DBG_DrawConstraints | DBG_DrawConstraintLimits | DBG_DrawNormals | DBG_DrawFrames;
	}

	void PhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
	{
		m_renderer->DrawLine(ToVector3(from), ToVector3(to), ToVector4(fromColor), ToVector4(toColor));
	}

	void PhysicsDebugDraw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
	{
		const btVector3& from = PointOnB;
		btVector3 to = PointOnB + normalOnB * distance;
		drawLine(from, to, color);
	}

	void PhysicsDebugDraw::reportErrorWarning(const char* error_warning)
	{
		LOGF_WARNING("%s", error_warning);
	}
}
