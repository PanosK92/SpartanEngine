/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "Component.h"
#include <memory>
#include <vector>
#include "../Graphics/Vertex.h"
#include "../Math/Matrix.h"
#include "../Math/Ray.h"
//=============================

namespace Directus
{
	class GameObject;
	class MeshFilter;
	class Model;

	namespace Math
	{
		class Quaternion;
		class Vector2;
		class Vector3;
		class Vector3;
		class Frustrum;
	}

	enum Projection
	{
		Perspective,
		Orthographic,
	};

	class DLL_API Camera : public Component
	{
	public:
		Camera();
		~Camera();

		//= ICOMPONENT ============
		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();
		//=========================

		//= MATRICES ===============================================
		Math::Matrix GetViewMatrix() { return m_mView; }
		Math::Matrix GetProjectionMatrix() { return m_mProjection; }
		Math::Matrix GetBaseViewMatrix() { return m_mBaseView; }
		//==========================================================

		//= RAYCASTING ===================================================
		// Returns a the picking ray as vertices (can be used to render it)
		std::vector<VertexPosCol> GetPickingRay();

		// Returns the nearest GameObject under the cursor
		std::weak_ptr<GameObject> Pick(const Math::Vector2& mousePos);

		// Converts a world point to a screen point
		Math::Vector2 WorldToScreenPoint(const Math::Vector3& worldPoint);

		// Converts a screen point to a world point
		Math::Vector3 ScreenToWorldPoint(const Math::Vector2& point);
		//================================================================

		//= PLANES/PROJECTION =====================================================
		float GetNearPlane() { return m_nearPlane; }
		void SetNearPlane(float nearPlane);
		float GetFarPlane() { return m_farPlane; }
		void SetFarPlane(float farPlane);
		Projection GetProjection() { CalculateProjection();  return m_projection; }
		void SetProjection(Projection projection);
		//=========================================================================

		//= FOV ==============================
		float GetFOV_Horizontal_Deg();
		void SetFOV_Horizontal_Deg(float fov);
		//====================================

		//= MISC ===============================================================
		bool IsInViewFrustrum(MeshFilter* meshFilter);
		Math::Vector4 GetClearColor() { return m_clearColor; }
		void SetClearColor(const Math::Vector4& color) { m_clearColor = color; }
		//======================================================================

	private:
		void CalculateViewMatrix();
		void CalculateBaseView();
		void CalculateProjection();

		float m_fovHorizontal;
		float m_nearPlane;
		float m_farPlane;
		Math::Ray m_ray;
		std::shared_ptr<Math::Frustrum> m_frustrum;
		Projection m_projection;
		Math::Vector4 m_clearColor;

		Math::Matrix m_mView;
		Math::Matrix m_mProjection;
		Math::Matrix m_mBaseView;

		Math::Vector3 m_position;
		Math::Quaternion m_rotation;
		bool m_isDirty;

		Math::Vector2 m_lastKnownResolution;	
	};
}