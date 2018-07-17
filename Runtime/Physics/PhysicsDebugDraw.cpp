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

//==============================
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Logging/Log.h"
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	PhysicsDebugDraw::PhysicsDebugDraw()
	{
		m_isDirty = false;
		m_debugMode = 0;
	}

	PhysicsDebugDraw::~PhysicsDebugDraw()
	{
	}

	void PhysicsDebugDraw::Release()
	{
	}

	void PhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
	{
		m_lines.emplace_back(RHI_Vertex_PosCol{ ToVector3(from), ToVector4(fromColor) });
		m_lines.emplace_back(RHI_Vertex_PosCol{ ToVector3(to), ToVector4(toColor) });

		m_isDirty = true;
	}

	void PhysicsDebugDraw::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
	{
		m_lines.emplace_back(RHI_Vertex_PosCol{ ToVector3(from), ToVector4(color) });
		m_lines.emplace_back(RHI_Vertex_PosCol{ ToVector3(to), ToVector4(color) });

		m_isDirty = true;
	}

	void PhysicsDebugDraw::drawSphere(const btVector3& p, btScalar radius, const btVector3& color)
	{
		int lats = 5;
		int longs = 5;

		for (int i = 0; i <= lats; i++)
		{
			btScalar lat0 = SIMD_PI * (-btScalar(0.5) + (btScalar)(i - 1) / lats);
			btScalar z0 = radius * sin(lat0);
			btScalar zr0 = radius * cos(lat0);

			btScalar lat1 = SIMD_PI * (-btScalar(0.5) + (btScalar)i / lats);
			btScalar z1 = radius * sin(lat1);
			btScalar zr1 = radius * cos(lat1);

			for (int j = 0; j <= longs; j++)
			{
				btScalar lng = 2 * SIMD_PI * (btScalar)(j - 1) / longs;
				btScalar x = cos(lng);
				btScalar y = sin(lng);

				const btVector3& from = btVector3(x * zr0, y * zr0, z0);
				const btVector3& to = btVector3(x * zr1, y * zr1, z1);

				drawLine(from, to, color);
			}
		}
	}

	void PhysicsDebugDraw::drawTriangle(const btVector3& a, const btVector3& b, const btVector3& c, const btVector3& color, btScalar alpha)
	{
		drawLine(a, b, color);
		drawLine(b, c, color);
		drawLine(c, a, color);
	}

	void PhysicsDebugDraw::drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color)
	{
		const btVector3& from = PointOnB;
		btVector3 to = PointOnB + normalOnB * distance;
		drawLine(from, to, color);
	}

	void PhysicsDebugDraw::reportErrorWarning(const char* warningString)
	{
		LOG_WARNING("Physics: " + string(warningString));
	}

	void PhysicsDebugDraw::draw3dText(const btVector3& location, const char* textString)
	{
	}

	void PhysicsDebugDraw::setDebugMode(int debugMode)
	{
		m_debugMode = debugMode;
	}

	int PhysicsDebugDraw::getDebugMode() const
	{
		return m_debugMode;
	}

	bool PhysicsDebugDraw::IsDirty()
	{
		return m_isDirty;
	}

	const std::vector<RHI_Vertex_PosCol>& PhysicsDebugDraw::GetLines()
	{
		return m_lines;
	}

	void PhysicsDebugDraw::Clear()
	{
		m_lines.clear();
		m_lines.shrink_to_fit();

		m_isDirty = false;
	}
}
