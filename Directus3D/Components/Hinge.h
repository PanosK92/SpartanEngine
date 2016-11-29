/*
Copyright(c) 2016 Panos Karabelas

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
#include "IComponent.h"
#include "../Math/Vector3.h"
//==========================

class btHingeConstraint;

class DllExport Hinge : public IComponent
{
public:
	Hinge();
	~Hinge();

	/*------------------------------------------------------------------------------
								[INTERFACE]
	------------------------------------------------------------------------------*/
	virtual void Reset();
	virtual void Start();
	virtual void OnDisable();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	void SetConnectedGameObject(GameObject* connectedRigidBody);
	GameObject* GetConnectedGameObject();

	void SetAxis(Directus::Math::Vector3 axis);
	Directus::Math::Vector3 GetAxis();

	void SetPivot(Directus::Math::Vector3 pivot);
	Directus::Math::Vector3 GetPivot();

	void SetPivotConnected(Directus::Math::Vector3 pivot);
	Directus::Math::Vector3 GetPivotConnected();

private:
	btHingeConstraint* m_hinge;
	GameObject* m_connectedGameObject;
	bool m_isConnected;
	Directus::Math::Vector3 m_pivotA;
	Directus::Math::Vector3 m_pivotB;
	Directus::Math::Vector3 m_axisA;
	Directus::Math::Vector3 m_axisB;

	bool m_isDirty;

	/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void ConstructHinge();
	void CalculateConnections();
	void ComponentCheck();
};
