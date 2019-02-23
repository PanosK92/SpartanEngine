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
#include <memory>
#include <vector>
#include "../../RHI/RHI_Definition.h"
#include "../../RHI/RHI_Viewport.h"
#include "../../Math/Matrix.h"
#include "../../Math/Ray.h"
#include "../../Math/Frustum.h"
#include "../../Math/Vector2.h"
//===================================

namespace Directus
{
	class Entity;
	class Model;
	class Renderable;

	enum ProjectionType
	{
		Projection_Perspective,
		Projection_Orthographic,
	};

	class ENGINE_CLASS Camera : public IComponent
	{
	public:
		Camera(Context* context, Entity* entity, Transform* transform);
		~Camera();

		//= ICOMPONENT ===============================
		void OnInitialize() override;
		void OnTick() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= MATRICES ========================================================
		const Math::Matrix& GetViewMatrix()			{ return m_mView; }
		const Math::Matrix& GetProjectionMatrix()	{ return m_mProjection; }
		const Math::Matrix& GetBaseViewMatrix()		{ return m_mBaseView; }
		//===================================================================

		//= RAYCASTING =========================================================
		// Returns the ray the camera uses to do picking
		const Math::Ray& GetPickingRay() { return m_ray; }

		// Picks the nearest entity under the mouse cursor
		bool Pick(const Math::Vector2& mousePos, std::shared_ptr<Entity>& entity);

		// Converts a world point to a screen point
		Math::Vector2 WorldToScreenPoint(const Math::Vector3& worldPoint);

		// Converts a screen point to a world point
		Math::Vector3 ScreenToWorldPoint(const Math::Vector2& point);
		//======================================================================

		//= PLANES/PROJECTION ================================================================
		void SetNearPlane(float nearPlane);		
		void SetFarPlane(float farPlane);	
		void SetProjection(ProjectionType projection);
		float GetNearPlane()				{ return m_nearPlane; }
		float GetFarPlane()					{ return m_farPlane; }
		ProjectionType GetProjectionType()	{ ComputeProjection();  return m_projectionType; }
		//====================================================================================

		//= FOV ==============================
		float GetFOV_Horizontal_Deg();
		void SetFOV_Horizontal_Deg(float fov);
		//====================================

		//= MISC ========================================================================
		bool IsInViewFrustrum(Renderable* renderable);
		bool IsInViewFrustrum(const Math::Vector3& center, const Math::Vector3& extents);
		const Math::Vector4& GetClearColor() { return m_clearColor; }
		void SetClearColor(const Math::Vector4& color) { m_clearColor = color; }
		//===============================================================================

	private:
		void ComputeViewMatrix();
		void ComputeBaseView();
		void ComputeProjection();

		float m_fovHorizontalRad;
		float m_nearPlane;
		float m_farPlane;
		Math::Ray m_ray;
		Math::Frustum m_frustrum;
		ProjectionType m_projectionType;
		Math::Vector4 m_clearColor;
		Math::Matrix m_mView;
		Math::Matrix m_mProjection;
		Math::Matrix m_mBaseView;
		Math::Vector3 m_position;
		Math::Quaternion m_rotation;
		bool m_isDirty;
		RHI_Viewport m_lastKnownViewport;
	};
}