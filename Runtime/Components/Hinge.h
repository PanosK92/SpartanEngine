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

class btHingeConstraint;

namespace Directus
{
	class GameObject;

	class DLL_API Hinge : public Component
	{
	public:
		Hinge();
		~Hinge();

		//= COMPONENT =============================
		void Initialize() override;
		void Start() override;
		void OnDisable() override;
		void Remove() override;
		void Update() override;
		void Serialize(StreamIO* stream) override;
		void Deserialize(StreamIO* stream) override;
		//=========================================

		void SetConnectedGameObject(std::weak_ptr<GameObject> connectedRigidBody);
		std::weak_ptr<GameObject> GetConnectedGameObject();

		void SetAxis(Math::Vector3 axis);
		Math::Vector3 GetAxis();

		void SetPivot(Math::Vector3 pivot);
		Math::Vector3 GetPivot();

		void SetPivotConnected(Math::Vector3 pivot);
		Math::Vector3 GetPivotConnected();

	private:
		btHingeConstraint* m_hinge;
		std::weak_ptr<GameObject> m_connectedGameObject;
		bool m_isConnected;
		Math::Vector3 m_pivotA;
		Math::Vector3 m_pivotB;
		Math::Vector3 m_axisA;
		Math::Vector3 m_axisB;

		bool m_isDirty;

		/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
		------------------------------------------------------------------------------*/
		void ConstructHinge();
		void CalculateConnections();
		void ComponentCheck();
	};
}