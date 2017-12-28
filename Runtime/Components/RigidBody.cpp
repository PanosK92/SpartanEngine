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

//= INCLUDES ================================================
#include "RigidBody.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <LinearMath/btMotionState.h>
#include "Transform.h"
#include "Collider.h"
#include "../Core/Engine.h"
#include "../Scene/GameObject.h"
#include "../Physics/Physics.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "../Math/Quaternion.h"
#include "../IO/FileStream.h"
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	static const float DEFAULT_MASS = 0.0f;
	static const float DEFAULT_FRICTION = 0.5f;
	static const float DEFAULT_FRICTION_ROLLING = 0.0f;
	static const float DEFAULT_RESTITUTION = 0.0f;	

	class MotionState : public btMotionState
	{
		RigidBody* m_rigidBody;
	public:
		MotionState(RigidBody* rigidBody) { m_rigidBody = rigidBody; }
		// Update from engine, ENGINE -> BULLET
		void getWorldTransform(btTransform& worldTrans) const override
		{
			Vector3 lastPos		= m_rigidBody->GetTransform()->GetPosition();
			Quaternion lastRot	= m_rigidBody->GetTransform()->GetRotation();
			worldTrans.setOrigin(ToBtVector3(lastPos + lastRot * m_rigidBody->GetColliderCenter()));
			worldTrans.setRotation(ToBtQuaternion(lastRot));

			m_rigidBody->m_hasSimulated = true;
		}

		// Update from bullet, BULLET -> ENGINE
		void setWorldTransform(const btTransform& worldTrans) override
		{
			Quaternion newWorldRot	= ToQuaternion(worldTrans.getRotation());
			Vector3 newWorldPos		= ToVector3(worldTrans.getOrigin()) - newWorldRot * m_rigidBody->GetColliderCenter();

			m_rigidBody->GetTransform()->SetPosition(newWorldPos);
			m_rigidBody->GetTransform()->SetRotation(newWorldRot);

			m_rigidBody->m_hasSimulated = true;
		}
	};

	RigidBody::RigidBody()
	{
		m_inWorld = false;
		m_mass = DEFAULT_MASS;
		m_restitution = DEFAULT_RESTITUTION;
		m_friction = DEFAULT_FRICTION;
		m_frictionRolling = DEFAULT_FRICTION_ROLLING;
		m_useGravity = true;
		m_isKinematic = false;
		m_hasSimulated = false;
		m_positionLock = Vector3::Zero;
		m_rotationLock = Vector3::Zero;
	}

	RigidBody::~RigidBody()
	{
		ReleaseRigidBody();
	}

	//= ICOMPONENT ==========================================================
	void RigidBody::Initialize()
	{
		AddBodyToWorld();
	}

	void RigidBody::Update()
	{
		// To make the body able to get positioned directly by the use without worrying about Bullet 
		// reseting it's state, we secretly set is as kinematic when the engine is not simulating (e.g. Editor Mode)
		if (!m_rigidBody)
			return;

		EngineMode engineMode = GetContext()->GetSubsystem<Engine>()->GetMode();

		// Editor -> Kinematic (so the user can move it around)
		if (engineMode == Editor && !m_rigidBody->isKinematicObject())
		{
			AddBodyToWorld();
		}

		// Game -> Dynamic (so bullet starts simulating)
		if (engineMode == Game && !m_isKinematic && m_rigidBody->isKinematicObject())
		{
			AddBodyToWorld();
		}
	}

	void RigidBody::Serialize(FileStream* stream)
	{
		stream->Write(m_mass);
		stream->Write(m_friction);
		stream->Write(m_frictionRolling);
		stream->Write(m_restitution);
		stream->Write(m_useGravity);
		stream->Write(m_gravity);
		stream->Write(m_isKinematic);
		stream->Write(m_positionLock);
		stream->Write(m_rotationLock);
	}

	void RigidBody::Deserialize(FileStream* stream)
	{
		stream->Read(&m_mass);
		stream->Read(&m_friction);
		stream->Read(&m_frictionRolling);
		stream->Read(&m_restitution);
		stream->Read(&m_useGravity);
		stream->Read(&m_gravity);
		stream->Read(&m_isKinematic);
		stream->Read(&m_positionLock);
		stream->Read(&m_rotationLock);

		AddBodyToWorld();
	}

	// = PROPERTIES =========================================================
	void RigidBody::SetMass(float mass)
	{
		mass = Max(mass, 0.0f);
		if (mass != m_mass)
		{
			m_mass = mass;
			AddBodyToWorld();
		}
	}

	void RigidBody::SetFriction(float friction)
	{
		if (!m_rigidBody || m_friction == friction)
			return;

		m_friction = friction;
		m_rigidBody->setFriction(friction);
	}

	void RigidBody::SetFrictionRolling(float frictionRolling)
	{
		if (!m_rigidBody || m_frictionRolling == frictionRolling)
			return;

		m_frictionRolling = frictionRolling;
		m_rigidBody->setRollingFriction(frictionRolling);
	}

	void RigidBody::SetRestitution(float restitution)
	{
		if (!m_rigidBody || m_restitution == restitution)
			return;

		m_restitution = restitution;
		m_rigidBody->setRestitution(restitution);
	}

	void RigidBody::SetUseGravity(bool gravity)
	{
		if (gravity == m_useGravity)
			return;

		m_useGravity = gravity;
		AddBodyToWorld();
	}

	void RigidBody::SetGravity(const Vector3& acceleration)
	{
		m_gravity = acceleration;
		AddBodyToWorld();
	}

	void RigidBody::SetKinematic(bool kinematic)
	{
		if (kinematic == m_isKinematic)
			return;

		m_isKinematic = kinematic;
		AddBodyToWorld();
	}

	//= FORCE/TORQUE ========================================================
	void RigidBody::SetLinearVelocity(const Vector3& velocity) const
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->setLinearVelocity(ToBtVector3(velocity));
		if (velocity != Vector3::Zero)
		{
			Activate();
		}
	}

	void RigidBody::SetAngularVelocity(const Vector3& velocity)
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->setAngularVelocity(ToBtVector3(velocity));
		if (velocity != Vector3::Zero)
		{
			Activate();
		}
	}

	void RigidBody::ApplyForce(const Vector3& force, ForceMode mode) const
	{
		if (!m_rigidBody)
			return;

		Activate();

		if (mode == Force)
		{
			m_rigidBody->applyCentralForce(ToBtVector3(force));
		}
		else if (mode == Impulse)
		{
			m_rigidBody->applyCentralImpulse(ToBtVector3(force));
		}
	}

	void RigidBody::ApplyForceAtPosition(const Vector3& force, Vector3 position, ForceMode mode) const
	{
		if (!m_rigidBody)
			return;

		Activate();

		if (mode == Force)
		{
			m_rigidBody->applyForce(ToBtVector3(force), ToBtVector3(position));
		}
		else if (mode == Impulse)
		{
			m_rigidBody->applyImpulse(ToBtVector3(force), ToBtVector3(position));
		}
	}

	void RigidBody::ApplyTorque(const Vector3& torque, ForceMode mode) const
	{
		if (!m_rigidBody)
			return;

		Activate();

		if (mode == Force)
		{
			m_rigidBody->applyTorque(ToBtVector3(torque));
		}
		else if (mode == Impulse)
		{
			m_rigidBody->applyTorqueImpulse(ToBtVector3(torque));
		}
	}

	//= CONSTRAINTS =========================================================
	void RigidBody::SetPositionLock(bool lock)
	{
		if (lock)
		{
			SetPositionLock(Vector3::One);
		}
		else
		{
			SetPositionLock(Vector3::Zero);
		}
	}

	void RigidBody::SetPositionLock(const Vector3& lock)
	{
		if (!m_rigidBody || m_positionLock == lock)
			return;

		m_positionLock = lock;
		Vector3 linearFactor = Vector3(!lock.x, !lock.y, !lock.z);
		m_rigidBody->setLinearFactor(ToBtVector3(linearFactor));
	}

	void RigidBody::SetRotationLock(bool lock)
	{
		if (lock)
		{
			SetRotationLock(Vector3::One);
		}
		else
		{
			SetRotationLock(Vector3::Zero);
		}
	}

	void RigidBody::SetRotationLock(const Vector3& lock)
	{
		if (!m_rigidBody || m_rotationLock == lock)
			return;

		m_rotationLock = lock;
		Vector3 angularFactor = Vector3(!lock.x, !lock.y, !lock.z);
		m_rigidBody->setAngularFactor(ToBtVector3(angularFactor));
	}

	//= POSITION ============================================================
	Vector3 RigidBody::GetPosition() const
	{
		return m_rigidBody ? ToVector3(m_rigidBody->getWorldTransform().getOrigin()) : Vector3::Zero;
	}

	void RigidBody::SetPosition(const Vector3& position)
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->getWorldTransform().setOrigin(ToBtVector3(position + ToQuaternion(m_rigidBody->getWorldTransform().getRotation()) * GetColliderCenter()));
	}

	//= ROTATION ============================================================
	Quaternion RigidBody::GetRotation() const
	{
		return m_rigidBody ? ToQuaternion(m_rigidBody->getWorldTransform().getRotation()) : Quaternion::Identity;
	}

	void RigidBody::SetRotation(const Quaternion& rotation)
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->getWorldTransform().setRotation(ToBtQuaternion(rotation));
	}

	//= MISC ====================================================================
	void RigidBody::SetCollisionShape(weak_ptr<btCollisionShape> shape)
	{
		m_shape = shape;
		if (!m_shape.expired())
		{
			AddBodyToWorld();
		}
		else
		{
			RemoveBodyFromWorld();
		}
	}

	void RigidBody::ClearForces() const
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->clearForces();
	}

	Vector3 RigidBody::GetColliderCenter()
	{
		Collider* collider = GetGameObject()->GetComponent<Collider>().lock().get();
		return collider ? collider->GetCenter() : Vector3::Zero;
	}

	void RigidBody::Activate() const
	{
		if (!m_rigidBody)
			return;

		if (m_mass > 0.0f)
		{
			m_rigidBody->activate(true);
		}
	}

	void RigidBody::Deactivate() const
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->setActivationState(WANTS_DEACTIVATION);
	}
	//===========================================================================

	//= HELPER FUNCTIONS ========================================================
	void RigidBody::AddBodyToWorld()
	{
		if (m_mass < 0.0f)
		{
			m_mass = 0.0f;
		}

		// Remove existing btRigidBody but keep it's inertia
		btVector3 localInertia(0, 0, 0);
		if (m_rigidBody)
		{
			localInertia = m_rigidBody->getLocalInertia();
			ReleaseRigidBody();
		}

		// Calculate local inertia
		if (!m_shape.expired())
		{
			m_shape.lock()->calculateLocalInertia(m_mass, localInertia);
		}

		// Construction of btRigidBody
		{
			// Create a motion state (memory will be freed by the RigidBody)
			MotionState* motionState = new MotionState(this);

			// Info
			btRigidBody::btRigidBodyConstructionInfo constructionInfo(m_mass, motionState, !m_shape.expired() ? m_shape.lock().get() : nullptr, localInertia);
			constructionInfo.m_mass = m_mass;
			constructionInfo.m_friction = m_friction;
			constructionInfo.m_rollingFriction = m_frictionRolling;
			constructionInfo.m_restitution = m_restitution;
			constructionInfo.m_startWorldTransform;
			constructionInfo.m_collisionShape = !m_shape.expired() ? m_shape.lock().get() : nullptr;
			constructionInfo.m_localInertia = localInertia;
			constructionInfo.m_motionState = motionState;

			m_rigidBody = make_shared<btRigidBody>(constructionInfo);
		}

		UpdateGravity();
		
		// Collision flags
		{ 
			int flags = m_rigidBody->getCollisionFlags();

			// Editor -> Kinematic (so the user can move it around)
			bool originalKinematicState = m_isKinematic;
			if (GetContext()->GetSubsystem<Engine>()->GetMode() == Editor)
			{
				m_isKinematic = true;
			}

			if (m_isKinematic)
			{
				flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
			}
			else
			{
				flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
			}

			m_rigidBody->setCollisionFlags(flags);
			m_rigidBody->forceActivationState(m_isKinematic ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);

			m_isKinematic = originalKinematicState;
		}

		m_rigidBody->setDeactivationTime(2000);

		// Set initial transform
		SetPosition(GetTransform()->GetPosition());
		SetRotation(GetTransform()->GetRotation());

		// Constraints
		SetPositionLock(m_positionLock);
		SetRotationLock(m_rotationLock);

		// Add btRigidBody to world
		GetContext()->GetSubsystem<Physics>()->GetWorld()->addRigidBody(m_rigidBody.get());
		if (m_mass > 0.0f)
		{
			Activate();
		}
		else
		{
			SetLinearVelocity(Vector3::Zero);
			SetAngularVelocity(Vector3::Zero);
		}

		m_hasSimulated = false;
		m_inWorld = true;
	}

	void RigidBody::RemoveBodyFromWorld()
	{
		if (!m_rigidBody)
			return;

		if (m_inWorld)
		{
			GetContext()->GetSubsystem<Physics>()->GetWorld()->removeRigidBody(m_rigidBody.get());
			m_inWorld = false;
		}
	}

	void RigidBody::UpdateGravity()
	{
		if (!m_rigidBody)
			return;

		int flags = m_rigidBody->getFlags();
		if (m_useGravity)
		{
			flags &= ~BT_DISABLE_WORLD_GRAVITY;
		}
		else
		{
			flags |= BT_DISABLE_WORLD_GRAVITY;
		}
		m_rigidBody->setFlags(flags);

		if (m_useGravity)
		{
			btVector3 gravity = ToBtVector3(GetContext()->GetSubsystem<Physics>()->GetGravity());
			m_rigidBody->setGravity(gravity);
		}
		else
		{
			m_rigidBody->setGravity(btVector3(0.0f, 0.0f, 0.0f));
		}
	}

	void RigidBody::ReleaseRigidBody()
	{
		if (!m_rigidBody)
			return;

		// Remove RigidBody from world
		GetContext()->GetSubsystem<Physics>()->GetWorld()->removeRigidBody(m_rigidBody.get());
		// Delete it's motion state
		delete m_rigidBody->getMotionState();
	}

	bool RigidBody::IsActivated() const
	{
		return m_rigidBody->isActive();
	}
	//===========================================================================
}