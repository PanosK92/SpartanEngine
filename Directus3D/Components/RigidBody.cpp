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

//= INCLUDES ================================================
#include "RigidBody.h"
#include "Transform.h"
#include "Collider.h"
#include "../Core/Settings.h"
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Physics/PhysicsEngine.h"
#include <LinearMath/btMotionState.h>
#include "../Physics/BulletPhysicsHelper.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include "../IO/Log.h"
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

class MotionState : public btMotionState
{
protected:
	Transform* transform;
	Vector3 colliderCenter;

public:
	MotionState(Transform* transform, Vector3 colliderCenter)
	{
		this->transform = transform;
		this->colliderCenter = colliderCenter;
	}

	virtual ~MotionState()
	{
	}

	virtual void getWorldTransform(btTransform& worldTrans) const
	{
		Vector3 position = transform->GetPosition(); // engine pos
		Quaternion rotation = transform->GetRotation(); // engine rotation
		Vector3 offset = colliderCenter; // engine collider offset
		offset = rotation * offset;

		worldTrans.setOrigin(Vector3ToBtVector3(position + offset));
		worldTrans.setRotation(QuaternionToBtQuaternion(rotation));
	}

	virtual void setWorldTransform(const btTransform& worldTrans)
	{
		if (!transform)
			return;

		Vector3 btPosition = BtVector3ToVector3(worldTrans.getOrigin()); // bullet position
		Quaternion btRotation = BtQuaternionToQuaternion(worldTrans.getRotation()); // bullet rotation
		Vector3 offset = colliderCenter; // engine collider offset
		offset = btRotation * offset;

		transform->SetPosition(btPosition - offset);
		transform->SetRotation(btRotation);
	}
};

RigidBody::RigidBody()
{
	m_rigidBody = nullptr;
	m_shape = nullptr;
	m_rotationLock = Vector3::Zero;
}

RigidBody::~RigidBody()
{
	delete m_rigidBody;
	m_rigidBody = nullptr;
}

/*------------------------------------------------------------------------------
								[INTERFACE]
------------------------------------------------------------------------------*/
void RigidBody::Initialize()
{
	ConstructRigidBody(1.0f, 0.5f, 0.5f);
}

void RigidBody::Remove()
{
	g_physics->RemoveRigidBody(m_rigidBody);
}

void RigidBody::Update()
{
	if (GET_ENGINE_MODE == Editor_Stop)
	{
		//SetPosition(g_transform->GetPosition());
		//SetRotation(g_transform->GetRotation());
		//SetColliderScale(g_transform->GetScale());
	}
	else if(GET_ENGINE_MODE == Editor_Play)
	{
		LOG("Play");
	}
}

void RigidBody::Serialize()
{
	Serializer::SaveFloat(GetMass());
	Serializer::SaveFloat(GetRestitution());
	Serializer::SaveFloat(GetDrag());
	Serializer::SaveVector3(m_rotationLock);
}

void RigidBody::Deserialize()
{
	float mass = Serializer::LoadFloat();
	float restitution = Serializer::LoadFloat();
	float friction = Serializer::LoadFloat();
	m_rotationLock = Serializer::LoadVector3();

	ConstructRigidBody(mass, restitution, friction);
	DeactivateRigidBody();
}

bool RigidBody::IsStatic()
{
	if (GetMass() != 0.0f)
		return true;

	return true;
}

/*------------------------------------------------------------------------------
								[PROPERTIES]
------------------------------------------------------------------------------*/
float RigidBody::GetMass()
{
	return m_rigidBody->getInvMass();
}

void RigidBody::SetMass(float mass)
{
	// remove the rigidBody from the world
	g_physics->RemoveRigidBody(m_rigidBody);

	btVector3 inertia(0, 0, 0);

	if (m_rigidBody->getCollisionShape())
		m_rigidBody->getCollisionShape()->calculateLocalInertia(mass, inertia);

	m_rigidBody->setMassProps(mass, inertia);
	m_rigidBody->updateInertiaTensor();

	// add the rigiBody back to the world,
	// so that the data structures are properly updated
	g_physics->AddRigidBody(m_rigidBody);
}

float RigidBody::GetDrag()
{
	if (!m_rigidBody)
		return 0.0f;

	return float(m_rigidBody->getFriction());
}

void RigidBody::SetDrag(float drag) const
{
	if (!m_rigidBody)
		return;

	m_rigidBody->setFriction(btScalar(drag));
}

float RigidBody::GetAngularDrag() const
{
	if (!m_rigidBody)
		return 0.0f;

	return (float)m_rigidBody->getRollingFriction();
}

void RigidBody::SetAngularDrag(float angularDrag) const
{
	if (!m_rigidBody)
		return;

	m_rigidBody->setRollingFriction(btScalar(angularDrag));
}

float RigidBody::GetRestitution() const
{
	if (!m_rigidBody)
		return 0.0f;

	return m_rigidBody->getRestitution();
}

void RigidBody::SetRestitution(float restitution) const
{
	if (!m_rigidBody)
		return;

	m_rigidBody->setRestitution(restitution);
}


void RigidBody::ApplyForce(Vector3 force, ForceMode mode)
{
	ActivateRigidBody();

	if (mode == Force)
		m_rigidBody->applyCentralForce(Vector3ToBtVector3(force));
	else if (mode == Impulse)
		m_rigidBody->applyCentralImpulse(Vector3ToBtVector3(force));
}

void RigidBody::ApplyForceAtPosition(Vector3 force, Vector3 position, ForceMode mode)
{
	ActivateRigidBody();

	if (mode == Force)
		m_rigidBody->applyForce(Vector3ToBtVector3(force), Vector3ToBtVector3(position));
	else if (mode == Impulse)
		m_rigidBody->applyImpulse(Vector3ToBtVector3(force), Vector3ToBtVector3(position));
}

void RigidBody::ApplyTorque(Vector3 torque, ForceMode mode)
{
	ActivateRigidBody();

	if (mode == Force)
		m_rigidBody->applyTorque(Vector3ToBtVector3(torque));
	else if (mode == Impulse)
		m_rigidBody->applyTorqueImpulse(Vector3ToBtVector3(torque));
}

void RigidBody::SetUseGravity(bool use)
{

}

bool RigidBody::GetUseGravity()
{
	return true;
}

Vector3 RigidBody::GetGravity() const
{
	return BtVector3ToVector3(m_rigidBody->getGravity());
}

void RigidBody::SetGravity(Vector3 acceleration) const
{
	m_rigidBody->setGravity(Vector3ToBtVector3(acceleration));
}

void RigidBody::SetKinematic(bool kinematic)
{

}

bool RigidBody::GetKinematic()
{
	return false;
}

//= POSITION LOCK =================================
void RigidBody::SetPositionLock(bool lock)
{

}

void RigidBody::SetPositionLock(Vector3 lock)
{

}

Vector3 RigidBody::GetPositionLock()
{
	return Vector3::Zero;
}
//=================================================

//= POSITION LOCK =================================
void RigidBody::SetRotationLock(bool lock)
{
	if (lock)
		SetRotationLock(Vector3(1, 1, 1));
	else
		SetRotationLock(Vector3(0, 0, 0));
}

void RigidBody::SetRotationLock(Vector3 lock)
{
	m_rotationLock = lock;
	
	Vector3 rotationFreedom = Vector3(!lock.x, !lock.y, !lock.z);
	m_rigidBody->setAngularFactor(Vector3ToBtVector3(rotationFreedom));
}

Vector3 RigidBody::GetRotationLock() const
{
	return m_rotationLock;
}
//=================================================

/*------------------------------------------------------------------------------
							[POSITION]
------------------------------------------------------------------------------*/
Vector3 RigidBody::GetPosition() const
{
	if (!m_rigidBody)
		return Vector3::Zero;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	return BtVector3ToVector3(currentTransform.getOrigin());
}

void RigidBody::SetPosition(Vector3 position) const
{
	if (!m_rigidBody)
		return;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	m_rigidBody->getWorldTransform().setOrigin(currentTransform.getOrigin());
}

/*------------------------------------------------------------------------------
							[ROTATION]
------------------------------------------------------------------------------*/
Quaternion RigidBody::GetRotation() const
{
	if (!m_rigidBody)
		return Quaternion::Identity();

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	return BtQuaternionToQuaternion(currentTransform.getRotation());
}

void RigidBody::SetRotation(Quaternion rotation) const
{
	if (!m_rigidBody)
		return;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	m_rigidBody->getWorldTransform().setRotation(currentTransform.getRotation());
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
void RigidBody::SetCollisionShape(btCollisionShape* shape)
{
	if (!m_rigidBody)
		return;

	m_shape = shape;

	ConstructRigidBody(GetMass(), GetRestitution(), GetDrag());
}

btRigidBody* RigidBody::GetBtRigidBody() const
{
	return m_rigidBody;
}

/*------------------------------------------------------------------------------
								[COLLIDER]
------------------------------------------------------------------------------*/
Vector3 RigidBody::GetColliderScale() const
{
	if (g_gameObject->HasComponent<Collider>())
		return g_gameObject->GetComponent<Collider>()->GetScale();

	return Vector3(0, 0, 0);
}

void RigidBody::SetColliderScale(Vector3 scale) const
{
	if (g_gameObject->HasComponent<Collider>())
		g_gameObject->GetComponent<Collider>()->SetScale(scale);
}

Vector3 RigidBody::GetColliderCenter() const
{
	Vector3 center = Vector3(0, 0, 0);

	if (g_gameObject->HasComponent<Collider>())
		center = g_gameObject->GetComponent<Collider>()->GetCenter();

	return center;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void RigidBody::ConstructRigidBody(float mass, float restitution, float friction)
{
	btVector3 inertia(0, 0, 0);

	// in case there is an existing rigidBody, remove it
	if (m_rigidBody)
	{
		inertia = m_rigidBody->getLocalInertia(); // save the inertia
		g_physics->RemoveRigidBody(m_rigidBody);
		delete m_rigidBody;
		m_rigidBody = nullptr;
	}

	// crete motion state
	MotionState* motionState = new MotionState(g_transform, GetColliderCenter());

	if (m_shape) // if a shape has been assigned
		m_shape->calculateLocalInertia(mass, inertia); // calculate local inertia

	// crete contruction info
	btRigidBody::btRigidBodyConstructionInfo rigidBodyConstructionInfo(mass, motionState, m_shape, inertia);

	rigidBodyConstructionInfo.m_restitution = restitution;
	rigidBodyConstructionInfo.m_friction = friction;

	// construct it and save it
	m_rigidBody = new btRigidBody(rigidBodyConstructionInfo);

	SetRotationLock(m_rotationLock);

	// add it to the world
	g_physics->AddRigidBody(m_rigidBody);
}

void RigidBody::ActivateRigidBody() const
{
	m_rigidBody->activate(true);
}

void RigidBody::DeactivateRigidBody() const
{
	m_rigidBody->setActivationState(0);
}
