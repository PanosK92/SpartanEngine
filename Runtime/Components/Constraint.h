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

//= INCLUDES ===============
#include "Component.h"
#include "../Math/Vector3.h"
//==========================

class btTypedConstraint;

namespace Directus
{
	class RigidBody;
	class GameObject;

	enum ConstraintType
	{
		ConstraintType_Point2Point,
		ConstraintType_Hinge,
		ConstraintType_Slider,
		ConstraintType_ConeTwist
	};

	class ENGINE_API Constraint : public Component
	{
	public:
		Constraint();
		~Constraint();

		//= COMPONENT =============================
		void Initialize() override;
		void Start() override;
		void OnDisable() override;
		void Remove() override;
		void Update() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//=========================================


	private:
		void ConstructConstraint();
		void ReleaseConstraint();
		std::unique_ptr<btTypedConstraint> m_constraint;
		std::weak_ptr<RigidBody> m_bodyOwn;
		std::weak_ptr<RigidBody> m_bodyOther;
		bool m_isDirty;
	};
}