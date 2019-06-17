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
#include "../../RHI/RHI_Definition.h"
#include "../../RHI/RHI_Viewport.h"
#include "../../Math/Matrix.h"
#include "../../Math/Ray.h"
#include "../../Math/Frustum.h"
#include "../../Math/Vector2.h"
//===================================

namespace Spartan
{
	class Entity;
	class Model;
	class Renderable;

	enum ProjectionType
	{
		Projection_Perspective,
		Projection_Orthographic,
	};

	class SPARTAN_CLASS Camera : public IComponent
	{
	public:
		Camera(Context* context, Entity* entity, Transform* transform);
		~Camera() = default;

		//= ICOMPONENT ===============================
		void OnInitialize() override;
		void OnTick() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		//= MATRICES ============================================================
		const Math::Matrix& GetViewMatrix() const		{ return m_mView; }
		const Math::Matrix& GetProjectionMatrix() const { return m_mProjection; }
		const Math::Matrix& GetBaseViewMatrix() const	{ return m_mBaseView; }
		//=======================================================================

		//= RAYCASTING =================================================================
		// Returns the ray the camera uses to do picking
		const Math::Ray& GetPickingRay() const { return m_ray; }

		// Picks the nearest entity under the mouse cursor
		bool Pick(const Math::Vector2& mouse_position, std::shared_ptr<Entity>& entity);

		// Converts a world point to a screen point
		Math::Vector2 WorldToScreenPoint(const Math::Vector3& position_world) const;

		// Converts a screen point to a world point
		Math::Vector3 ScreenToWorldPoint(const Math::Vector2& position_screen) const;
		//==============================================================================

		//= PLANES/PROJECTION ================================================================
		void SetNearPlane(float near_plane);		
		void SetFarPlane(float far_plane);	
		void SetProjection(ProjectionType projection);
		float GetNearPlane() const			{ return m_near_plane; }
		float GetFarPlane() const			{ return m_far_plane; }
		ProjectionType GetProjectionType()	{ ComputeProjection();  return m_projection_type; }
		//====================================================================================

		//= FOV =========================================================
		auto GetFovHorizontalRad() const { return m_fov_horizontal_rad; }
		float GetFovHorizontalDeg() const;		
		void SetFovHorizontalDeg(float fov);
		//===============================================================

		//= MISC ========================================================================
		bool IsInViewFrustrum(Renderable* renderable);
		bool IsInViewFrustrum(const Math::Vector3& center, const Math::Vector3& extents);
		const Math::Vector4& GetClearColor() const		{ return m_clear_color; }
		void SetClearColor(const Math::Vector4& color)	{ m_clear_color = color; }
		//===============================================================================

	private:
        void MouseLook();
		void ComputeViewMatrix();
		void ComputeBaseView();
		void ComputeProjection();

		float m_fov_horizontal_rad;
		float m_near_plane;
		float m_far_plane;
		Math::Ray m_ray;
		Math::Frustum m_frustrum;
		ProjectionType m_projection_type;
		Math::Vector4 m_clear_color;
		Math::Matrix m_mView;
		Math::Matrix m_mProjection;
		Math::Matrix m_mBaseView;
		Math::Vector3 m_position;
		Math::Quaternion m_rotation;
		bool m_isDirty;
		RHI_Viewport m_last_known_viewport;
	};
}
