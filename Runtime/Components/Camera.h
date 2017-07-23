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
#include "IComponent.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Matrix.h"
#include "../Math/Quaternion.h"
#include "../Math/Frustrum.h"
#include <memory>
#include "../Graphics/Vertex.h"
#include <vector>

//=============================

namespace Directus
{
	class GameObject;
	class MeshFilter;
	class Model;

	enum Projection
	{
		Perspective,
		Orthographic,
	};

	class DLL_API Camera : public IComponent
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
		std::vector<VertexPosCol> GetPickingRay();
		std::weak_ptr<GameObject> Pick(const Math::Vector2& mousePos);
		Math::Vector2 WorldToScreenPoint(const Math::Vector3& worldPoint);
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
		bool IsInViewFrustrum(std::weak_ptr<Model> model);
		Math::Vector4 GetClearColor() { return m_clearColor; }
		void SetClearColor(const Math::Vector4& color) { m_clearColor = color; }
		//======================================================================

	private:
		bool SphereIntersects(const Math::Vector3& rayOrigin, const Math::Vector3& rayDirection, const Math::Vector3& sphereCenter, float sphereRadius);
		void CalculateViewMatrix();
		void CalculateBaseView();
		void CalculateProjection();

		float m_fovHorizontal;
		float m_nearPlane;
		float m_farPlane;
		Math::Vector3 m_pickingRayStart;
		Math::Vector3 m_pickingRayEnd;
		Math::Vector3 m_pickingRayDirection;
		std::shared_ptr<Frustrum> m_frustrum;
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