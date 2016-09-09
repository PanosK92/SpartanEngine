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
#include "../Physics/PhysicsWorld.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "../IO/Log.h"
#include "../Core/Helper.h"
#include <LinearMath/btMotionState.h>
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

class MotionState : public btMotionState
{
	RigidBody* m_rigidBody;
public:
	MotionState(RigidBody* rigidBody) { m_rigidBody = rigidBody; }

	// Update bullet, ENGINE -> BULLET
	void btMotionState::getWorldTransform(btTransform& worldTransform) const override
	{
		Vector3 enginePositon = m_rigidBody->g_transform->GetPosition();
		Quaternion engineRotation = m_rigidBody->g_transform->GetRotation();

		worldTransform.setOrigin(ToBtVector3(enginePositon + engineRotation * m_rigidBody->GetColliderCenter()));
		worldTransform.setRotation(ToBtQuaternion(engineRotation));
	}

	// Update from bullet, ENGINE <- BULLET
	void btMotionState::setWorldTransform(const btTransform& worldTransform) override
	{
		Quaternion bulletRot = ToQuaternion(worldTransform.getRotation());
		Vector3 bulletPos = ToVector3(worldTransform.getOrigin()) - bulletRot *  m_rigidBody->GetColliderCenter();

		m_rigidBody->g_transform->SetPosition(bulletPos);
		m_rigidBody->g_transform->SetRotation(bulletRot);
	}
};

RigidBody::RigidBody()
{
	m_inWorld = false;
	m_mass = 0.0f;
	m_restitution = 0.5f;
	m_drag = 0.0f;
	m_angularDrag = 0.0f;
	m_useGravity = true;
	m_kinematic = false;
	m_positionLock = Vector3::Zero;
	m_rotationLock = Vector3::Zero;

	m_rigidBody = nullptr;
	m_shape = nullptr;
}

RigidBody::~RigidBody()
{
	DeleteBtRigidBody();
}

//= ICOMPONENT ==========================================================
void RigidBody::Initialize()
{
	AddBodyToWorld();
}

void RigidBody::Start()
{

}

void RigidBody::Remove()
{
	
}

void RigidBody::Update()
{
	if (GET_ENGINE_MODE == Editor_Idle)
	{
		SetPosition(g_transform->GetPosition());
		SetRotation(g_transform->GetRotation());
	}
	else if (GET_ENGINE_MODE == Editor_Playing)
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

	AddBodyToWorld();
}
//=======================================================================

// = PROPERTIES =========================================================
float RigidBody::GetMass() const
{
	return m_mass;
}

void RigidBody::SetMass(float mass)
{
	m_mass = max(mass, 0.0f);
	AddBodyToWorld();
}

float RigidBody::GetDrag() const
{
	return m_drag;
}

void RigidBody::SetDrag(float drag)
{
	m_drag = drag;
	AddBodyToWorld();
}

float RigidBody::GetAngularDrag() const
{
	return m_angularDrag;
}

void RigidBody::SetAngularDrag(float angularDrag)
{
	m_angularDrag = angularDrag;
	AddBodyToWorld();
}

float RigidBody::GetRestitution() const
{
	return m_restitution;
}

void RigidBody::SetRestitution(float restitution)
{
	m_restitution = restitution;
	AddBodyToWorld();
}

bool RigidBody::GetUseGravity() const
{
	return m_useGravity;
}

void RigidBody::SetUseGravity(bool use)
{
	m_useGravity = use;
	AddBodyToWorld();
}

Vector3 RigidBody::GetGravity() const
{
	return m_gravity;
}

void RigidBody::SetGravity(const Vector3& acceleration)
{
	m_gravity = acceleration;
	AddBodyToWorld();
}

void RigidBody::SetKinematic(bool kinematic)
{
	m_kinematic = kinematic;
	AddBodyToWorld();
}

bool RigidBody::GetKinematic() const
{
	return m_kinematic;
}
//=======================================================================

//= FORCE/TORQUE ========================================================
void RigidBody::SetLinearVelocity(const Vector3& velocity) const
{
	if (!m_rigidBody)
		return;

	m_rigidBody->setLinearVelocity(ToBtVector3(velocity));
	if (velocity != Vector3::Zero)
		Activate();
}

void RigidBody::SetAngularVelocity(const Vector3& velocity)
{
	if (!m_rigidBody)
		return;

	m_rigidBody->setAngularVelocity(ToBtVector3(velocity));
	if (velocity != Vector3::Zero)
		Activate();
}

void RigidBody::ApplyForce(const Vector3& force, ForceMode mode) const
{
	Activate();

	if (mode == Force)
		m_rigidBody->applyCentralForce(ToBtVector3(force));
	else if (mode == Impulse)
		m_rigidBody->applyCentralImpulse(ToBtVector3(force));
}

void RigidBody::ApplyForceAtPosition(const Vector3& force, Vector3 position, ForceMode mode) const
{
	Activate();

	if (mode == Force)
		m_rigidBody->applyForce(ToBtVector3(force), ToBtVector3(position));
	else if (mode == Impulse)
		m_rigidBody->applyImpulse(ToBtVector3(force), ToBtVector3(position));
}

void RigidBody::ApplyTorque(const Vector3& torque, ForceMode mode) const
{
	Activate();

	if (mode == Force)
		m_rigidBody->applyTorque(ToBtVector3(torque));
	else if (mode == Impulse)
		m_rigidBody->applyTorqueImpulse(ToBtVector3(torque));
}
//=======================================================================

//= CONSTRAINTS =========================================================
void RigidBody::SetPositionLock(bool lock)
{
	if (lock)
		SetPositionLock(Vector3::One);
	else
		SetPositionLock(Vector3::Zero);
}

void RigidBody::SetPositionLock(const Vector3& lock)
{
	m_positionLock = lock;

	Vector3 translationFreedom = Vector3(!lock.x, !lock.y, !lock.z);
	m_rigidBody->setLinearFactor(ToBtVector3(translationFreedom));
}

Vector3 RigidBody::GetPositionLock() const
{
	return m_positionLock;
}

void RigidBody::SetRotationLock(bool lock)
{
	if (lock)
		SetRotationLock(Vector3::One);
	else
		SetRotationLock(Vector3::Zero);
}

void RigidBody::SetRotationLock(const Vector3& lock)
{
	m_rotationLock = lock;

	Vector3 rotationFreedom = Vector3(!lock.x, !lock.y, !lock.z);
	m_rigidBody->setAngularFactor(ToBtVector3(rotationFreedom));
}

Vector3 RigidBody::GetRotationLock() const
{
	return m_rotationLock;
}
//=======================================================================

//= POSITION ============================================================
Vector3 RigidBody::GetPosition() const
{
	return m_rigidBody ? ToVector3(m_rigidBody->getWorldTransform().getOrigin()) : Vector3::Zero;
}

void RigidBody::SetPosition(const Vector3& position) const
{
	if (!m_rigidBody)
		return;

	Collider* collider = g_gameObject->GetComponent<Collider>();
	Vector3 colliderCenter = collider ? collider->GetCenter() : Vector3::Zero;

	btTransform& worldTrans = m_rigidBody->getWorldTransform();
	worldTrans.setOrigin(ToBtVector3(position + ToQuaternion(worldTrans.getRotation()) * colliderCenter));

	Activate();
}

//= ROTATION ============================================================
Quaternion RigidBody::GetRotation() const
{
	return m_rigidBody ? ToQuaternion(m_rigidBody->getWorldTransform().getRotation()) : Quaternion::Identity;
}

void RigidBody::SetRotation(const Quaternion& rotation) const
{
	if (!m_rigidBody)
		return;

	Vector3 oldPosition = GetPosition();
	btTransform& worldTrans = m_rigidBody->getWorldTransform();
	worldTrans.setRotation(ToBtQuaternion(rotation));
	if (GetColliderCenter() != Vector3::Zero)
		worldTrans.setOrigin(ToBtVector3(oldPosition + rotation * GetColliderCenter()));

	m_rigidBody->updateInertiaTensor();

	Activate();
}

//= MISC ====================================================================
void RigidBody::SetCollisionShape(btCollisionShape* shape)
{
	m_shape = shape;
	AddBodyToWorld();
}

btRigidBody* RigidBody::GetBtRigidBody() const
{
	return m_rigidBody;
}

void RigidBody::ClearForces() const
{
	if (!m_rigidBody)
		return;

	m_rigidBody->clearForces();
}

Vector3 RigidBody::GetColliderCenter() const
{
	Collider* collider = g_gameObject->GetComponent<Collider>();
	return collider ? collider->GetCenter() : Vector3::Zero;
}
//===========================================================================

//= HELPER FUNCTIONS ========================================================
void RigidBody::AddBodyToWorld()
{
	btVector3 inertia(0, 0, 0);

	if (m_mass < 0.0f)
		m_mass = 0.0f;

	// in case there is an existing rigidBody, remove it
	if (m_rigidBody)
	{
		inertia = m_rigidBody->getLocalInertia(); // save the inertia
		DeleteBtRigidBody();
	}

	// Colision Shape
	if (m_shape) // if a shape has been assigned
		m_shape->calculateLocalInertia(m_mass, inertia);

	// Motion state
	MotionState* motionState = new MotionState(this);

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

	UpdateGravity();

	//= COLLISION FLAGS ====================================================================
	int flags = m_rigidBody->getCollisionFlags();

	if (m_kinematic)
		flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
	else
		flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;

	m_rigidBody->setCollisionFlags(flags);
	m_rigidBody->forceActivationState(m_kinematic ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);
	//======================================================================================

	// Constraints
	SetPositionLock(m_positionLock);
	SetRotationLock(m_rotationLock);

	// PHYSICS WORLD - ADD
	g_physicsWorld->GetWorld()->addRigidBody(m_rigidBody);

	if (m_mass > 0.0f)
		Activate();
	else
	{
		SetLinearVelocity(Vector3::Zero);
		SetAngularVelocity(Vector3::Zero);
	}

	m_inWorld = true;
}

void RigidBody::RemoveBodyFromWorld()
{
	if (!m_rigidBody)
		return;

	if (m_inWorld)
	{
		g_physicsWorld->GetWorld()->removeRigidBody(m_rigidBody);
		m_inWorld = false;
	}
}

void RigidBody::UpdateGravity() const
{
	if (!m_rigidBody)
		return;

	btDiscreteDynamicsWorld* world = g_physicsWorld->GetWorld();

	int flags = m_rigidBody->getFlags();
	if (m_useGravity)
		flags &= ~BT_DISABLE_WORLD_GRAVITY;
	else
		flags |= BT_DISABLE_WORLD_GRAVITY;
	m_rigidBody->setFlags(flags);

	if (m_useGravity)
		m_rigidBody->setGravity(world->getGravity());
	else
		m_rigidBody->setGravity(btVector3(0.0f, 0.0f, 0.0f));
}

void RigidBody::DeleteBtRigidBody()
{
	if (!m_rigidBody || !g_physicsWorld)
		return;
	
	g_physicsWorld->GetWorld()->removeRigidBody(m_rigidBody);
	delete m_rigidBody->getMotionState();
	SafeDelete(m_rigidBody);
}

void RigidBody::Activate() const
{
	if (!m_rigidBody)
		return;

	if (m_mass > 0.0f)
		m_rigidBody->activate(true);
}
//===========================================================================