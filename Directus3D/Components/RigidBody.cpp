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

	m_mass = 0.0f;
	m_restitution = 0.5f;
	m_drag = 0.0f;
	m_angularDrag = 0.0f;
	m_useGravity = true;
	m_kinematic = false;
	m_positionLock = Vector3::Zero;
	m_rotationLock = Vector3::Zero;
}

RigidBody::~RigidBody()
{
	delete m_rigidBody;
	m_rigidBody = nullptr;
}

//= ICOMPONENT ==========================================================
void RigidBody::Initialize()
{
	ConstructRigidBody();
}

void RigidBody::Remove()
{
	g_physics->RemoveRigidBody(m_rigidBody);
}

void RigidBody::Update()
{
	if (GET_ENGINE_MODE == Editor_Stop)
	{
		SetPosition(g_transform->GetPosition());
		SetRotation(g_transform->GetRotation());
		SetColliderScale(g_transform->GetScale());
	}
	else if(GET_ENGINE_MODE == Editor_Play)
	{

	}
}

void RigidBody::Serialize()
{
	Serializer::SaveFloat(m_mass);
	Serializer::SaveFloat(m_drag);
	Serializer::SaveFloat(m_angularDrag);
	Serializer::SaveFloat(m_restitution);
	Serializer::SaveBool(m_useGravity);
	Serializer::SaveVector3(m_gravity);
	Serializer::SaveBool(m_kinematic);
	Serializer::SaveVector3(m_positionLock);
	Serializer::SaveVector3(m_rotationLock);
}

void RigidBody::Deserialize()
{
	m_mass = Serializer::LoadFloat();
	m_drag = Serializer::LoadFloat();
	m_angularDrag = Serializer::LoadFloat();
	m_restitution = Serializer::LoadFloat();
	m_useGravity = Serializer::LoadBool();
	m_gravity = Serializer::LoadVector3();
	m_kinematic = Serializer::LoadBool();
	m_positionLock = Serializer::LoadVector3();
	m_rotationLock = Serializer::LoadVector3();

	ConstructRigidBody();
}
//=======================================================================

// = PROPERTIES =========================================================
float RigidBody::GetMass() const
{
	return m_mass;
}

void RigidBody::SetMass(float mass)
{
	m_mass = mass;
	ConstructRigidBody();
}

float RigidBody::GetDrag() const
{
	return m_drag;
}

void RigidBody::SetDrag(float drag)
{
	m_drag = drag;
	ConstructRigidBody();
}

float RigidBody::GetAngularDrag() const
{
	return m_angularDrag;
}

void RigidBody::SetAngularDrag(float angularDrag) 
{
	m_angularDrag = angularDrag;
	ConstructRigidBody();
}

float RigidBody::GetRestitution() const
{
	return m_restitution;
}

void RigidBody::SetRestitution(float restitution)
{
	m_restitution = restitution;
	ConstructRigidBody();
}

bool RigidBody::GetUseGravity() const
{
	return m_useGravity;
}

void RigidBody::SetUseGravity(bool use)
{
	m_useGravity = use;
	ConstructRigidBody();
}

Vector3 RigidBody::GetGravity() const
{
	return m_gravity;
}

void RigidBody::SetGravity(Vector3 acceleration)
{
	m_gravity = acceleration;
	ConstructRigidBody();
}

void RigidBody::SetKinematic(bool kinematic)
{
	m_kinematic = kinematic;
	ConstructRigidBody();
}

bool RigidBody::GetKinematic() const
{
	return m_kinematic;
}
//=======================================================================

//= FORCE/TORQUE ========================================================
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
//=======================================================================

//= CONSTRAINTS =========================================================
void RigidBody::SetPositionLock(bool lock)
{
	if (lock)
		SetPositionLock(Vector3(1, 1, 1));
	else
		SetPositionLock(Vector3(0, 0, 0));
}

void RigidBody::SetPositionLock(Vector3 lock)
{
	m_positionLock = lock;

	Vector3 translationFreedom = Vector3(!lock.x, !lock.y, !lock.z);
	m_rigidBody->setLinearFactor(Vector3ToBtVector3(translationFreedom));
}

Vector3 RigidBody::GetPositionLock() const
{
	return m_positionLock;
}

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

Vector3 RigidBody::GetRotationLock()
{
	return m_rotationLock;
}
//=======================================================================

/*------------------------------------------------------------------------------
							[POSITION]
------------------------------------------------------------------------------*/
Vector3 RigidBody::GetPosition()
{
	if (!m_rigidBody)
		return Vector3::Zero;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	return BtVector3ToVector3(currentTransform.getOrigin());
}

void RigidBody::SetPosition(Vector3 position)
{
	if (!m_rigidBody)
		return;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	m_rigidBody->getWorldTransform().setOrigin(Vector3ToBtVector3(position));
}

/*------------------------------------------------------------------------------
							[ROTATION]
------------------------------------------------------------------------------*/
Quaternion RigidBody::GetRotation()
{
	if (!m_rigidBody)
		return Quaternion::Identity();

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	return BtQuaternionToQuaternion(currentTransform.getRotation());
}

void RigidBody::SetRotation(Quaternion rotation)
{
	if (!m_rigidBody)
		return;

	btTransform currentTransform;
	m_rigidBody->getMotionState()->getWorldTransform(currentTransform);
	m_rigidBody->getWorldTransform().setRotation(QuaternionToBtQuaternion(rotation));
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
void RigidBody::SetCollisionShape(btCollisionShape* shape)
{
	m_shape = shape;
	ConstructRigidBody();
}

btRigidBody* RigidBody::GetBtRigidBody() const
{
	return m_rigidBody;
}

/*------------------------------------------------------------------------------
								[COLLIDER]
------------------------------------------------------------------------------*/
Vector3 RigidBody::GetColliderScale()
{
	if (g_gameObject->HasComponent<Collider>())
		return g_gameObject->GetComponent<Collider>()->GetScale();

	return Vector3(0, 0, 0);
}

void RigidBody::SetColliderScale(Vector3 scale)
{
	if (g_gameObject->HasComponent<Collider>())
		g_gameObject->GetComponent<Collider>()->SetScale(scale);
}

Vector3 RigidBody::GetColliderCenter()
{
	Vector3 center = Vector3(0, 0, 0);

	if (g_gameObject->HasComponent<Collider>())
		center = g_gameObject->GetComponent<Collider>()->GetCenter();

	return center;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void RigidBody::ConstructRigidBody()
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

	// Motion State
	MotionState* motionState = new MotionState(g_transform, GetColliderCenter());

	// Colision Shape
	if (m_shape) // if a shape has been assigned
		m_shape->calculateLocalInertia(m_mass, inertia);

	// Construction Info
	btRigidBody::btRigidBodyConstructionInfo constructionInfo(m_mass, motionState, m_shape, inertia);
	constructionInfo.m_mass = m_mass;
	constructionInfo.m_friction = m_drag;
	constructionInfo.m_rollingFriction = m_angularDrag;
	constructionInfo.m_restitution = m_restitution;
	constructionInfo.m_startWorldTransform;
	constructionInfo.m_collisionShape = m_shape;
	constructionInfo.m_localInertia = inertia;
	constructionInfo.m_motionState = motionState;

	// RigidBody
	m_rigidBody = new btRigidBody(constructionInfo);

	// Constraints
	SetPositionLock(m_positionLock);
	SetRotationLock(m_rotationLock);

	// PHYSICS WORLD - ADD
	g_physics->AddRigidBody(m_rigidBody);
}

void RigidBody::ActivateRigidBody()
{
	if (!m_rigidBody)
		return;

	m_rigidBody->activate(true);
}