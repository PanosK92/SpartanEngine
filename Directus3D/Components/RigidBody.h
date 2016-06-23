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

//= INCLUDES ================
#include "IComponent.h"
#include "../Math/Vector3.h"
//===========================

class GameObject;
class btRigidBody;
class btCollisionShape;

enum ForceMode
{
	Force,
	Impulse
};

class __declspec(dllexport) RigidBody : public IComponent
{
public:
	RigidBody();
	~RigidBody();

	/*------------------------------------------------------------------------------
								[INTERFACE]
	------------------------------------------------------------------------------*/
	virtual void Initialize();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	/*------------------------------------------------------------------------------
								[PROPERTIES]
	------------------------------------------------------------------------------*/
	bool IsStatic();

	float GetMass();
	void SetMass(float mass);

	float GetRestitution();
	void SetRestitution(float restitution);

	float GetFriction();
	void SetFriction(float friction);

	void ApplyForce(Directus::Math::Vector3 force, ForceMode mode);
	void ApplyForceAtPosition(Directus::Math::Vector3 force, Directus::Math::Vector3 position, ForceMode mode);
	void ApplyTorque(Directus::Math::Vector3 torque, ForceMode mode);
	void SetGravity(Directus::Math::Vector3 acceleration);

	void SetRotationLock(bool lock);
	void SetRotationLock(Directus::Math::Vector3 lock);
	Directus::Math::Vector3 GetRotationLock();

	/*------------------------------------------------------------------------------
								[POSITION]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector3 GetPosition();
	void SetPosition(Directus::Math::Vector3 position);

	/*------------------------------------------------------------------------------
								[ROTATION]
	------------------------------------------------------------------------------*/
	Directus::Math::Quaternion GetRotation();
	void SetRotation(Directus::Math::Quaternion rotation);

	/*------------------------------------------------------------------------------
								[MISC]
	------------------------------------------------------------------------------*/
	void SetCollisionShape(btCollisionShape* shape);
	btRigidBody* GetBtRigidBody();

private:
	btRigidBody* m_rigidBody;
	btCollisionShape* m_shape;
	Directus::Math::Vector3 m_rotationLock;
	bool m_executeEvent;

	/*------------------------------------------------------------------------------
									[EVENTS]
	------------------------------------------------------------------------------*/
	void OnEditorPlay();
	void OnEditorDebug();

	/*------------------------------------------------------------------------------
								[COLLIDER]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector3 GetColliderScale();
	void SetColliderScale(Directus::Math::Vector3 scale);
	Directus::Math::Vector3 GetColliderCenter();

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void ConstructRigidBody(float mass, float restitution, float friction);
	void ActivateRigidBody();
	void DeactivateRigidBody();
};
