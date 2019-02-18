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

//= INCLUDES ===================================================
#include "RigidBody.h"
#include "Transform.h"
#include "Collider.h"
#include "Constraint.h"
#include "../Entity.h"
#include "../../Core/Engine.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../IO/FileStream.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#pragma warning(pop)
//==============================================================

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
	static const float DEFAULT_DEACTIVATION_TIME = 2000;

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
			worldTrans.setOrigin(ToBtVector3(lastPos + lastRot * m_rigidBody->GetCenterOfMass()));
			worldTrans.setRotation(ToBtQuaternion(lastRot));

			m_rigidBody->m_hasSimulated = true;
		}

		// Update from bullet, BULLET -> ENGINE
		void setWorldTransform(const btTransform& worldTrans) override
		{
			Quaternion newWorldRot	= ToQuaternion(worldTrans.getRotation());
			Vector3 newWorldPos		= ToVector3(worldTrans.getOrigin()) - newWorldRot * m_rigidBody->GetCenterOfMass();

			m_rigidBody->GetTransform()->SetPosition(newWorldPos);
			m_rigidBody->GetTransform()->SetRotation(newWorldRot);

			m_rigidBody->m_hasSimulated = true;
		}
	};

	RigidBody::RigidBody(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_inWorld			= false;
		m_mass				= DEFAULT_MASS;
		m_restitution		= DEFAULT_RESTITUTION;
		m_friction			= DEFAULT_FRICTION;
		m_frictionRolling	= DEFAULT_FRICTION_ROLLING;
		m_useGravity		= true;
		m_isKinematic		= false;
		m_hasSimulated		= false;
		m_positionLock		= Vector3::Zero;
		m_rotationLock		= Vector3::Zero;
		m_physics			= GetContext()->GetSubsystem<Physics>().get();
		m_collisionShape	= nullptr;
		m_rigidBody			= nullptr;

		REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_frictionRolling, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_useGravity, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_isKinematic, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_gravity, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_positionLock, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotationLock, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_centerOfMass, Vector3);	
	}

	RigidBody::~RigidBody()
	{
		Body_Release();
	}

	//= ICOMPONENT ==========================================================
	void RigidBody::OnInitialize()
	{
		Body_AcquireShape();
		Body_AddToWorld();
	}

	void RigidBody::OnRemove()
	{
		Body_Release();
	}

	void RigidBody::OnStart()
	{
		Activate();
	}

	void RigidBody::OnTick()
	{
		// When in editor mode, get position from transform (so the user can move the body around)
		if (!Engine::EngineMode_IsSet(Engine_Game))
		{
			SetPosition(GetTransform()->GetPosition());
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
		stream->Write(m_inWorld);
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
		stream->Read(&m_inWorld);

		Body_AcquireShape();
		Body_AddToWorld();
	}

	// = PROPERTIES =========================================================
	void RigidBody::SetMass(float mass)
	{
		mass = Max(mass, 0.0f);
		if (mass != m_mass)
		{
			m_mass = mass;
			Body_AddToWorld();
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
		Body_AddToWorld();
	}

	void RigidBody::SetGravity(const Vector3& acceleration)
	{
		if (m_gravity == acceleration)
			return;

		m_gravity = acceleration;
		Body_AddToWorld();
	}

	void RigidBody::SetIsKinematic(bool kinematic)
	{
		if (kinematic == m_isKinematic)
			return;

		m_isKinematic = kinematic;
		Body_AddToWorld();
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

	//= CENTER OF MASS ===============================================
	void RigidBody::SetCenterOfMass(const Vector3& centerOfMass)
	{
		m_centerOfMass = centerOfMass;
		SetPosition(GetPosition());
	}
	//================================================================

	//= POSITION ============================================================
	Vector3 RigidBody::GetPosition() const
	{
		if (m_rigidBody)
		{
			const btTransform& transform = m_rigidBody->getWorldTransform();
			return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_centerOfMass;
		}
	
		return Vector3::Zero;
	}

	void RigidBody::SetPosition(const Vector3& position)
	{
		if (!m_rigidBody)
			return;

		btTransform& worldTrans = m_rigidBody->getWorldTransform();
		worldTrans.setOrigin(ToBtVector3(position + ToQuaternion(worldTrans.getRotation()) * m_centerOfMass));

		// Don't allow position update when the game is running
		if (!m_hasSimulated && m_physics->IsSimulating())
		{
			btTransform interpTrans = m_rigidBody->getInterpolationWorldTransform();
			interpTrans.setOrigin(worldTrans.getOrigin());
			m_rigidBody->setInterpolationWorldTransform(interpTrans);
		}

		Activate();
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

		Vector3 oldPosition = GetPosition();
		btTransform& worldTrans = m_rigidBody->getWorldTransform();
		worldTrans.setRotation(ToBtQuaternion(rotation));
		if (m_centerOfMass != Vector3::Zero)
		{
			worldTrans.setOrigin(ToBtVector3(oldPosition + rotation * m_centerOfMass));
		}

		if (!m_hasSimulated || m_physics->IsSimulating())
		{
			btTransform interpTrans = m_rigidBody->getInterpolationWorldTransform();
			interpTrans.setRotation(worldTrans.getRotation());
			if (m_centerOfMass != Vector3::Zero)
			{
				interpTrans.setOrigin(worldTrans.getOrigin());
			}
			m_rigidBody->setInterpolationWorldTransform(interpTrans);
		}

		m_rigidBody->updateInertiaTensor();

		Activate();
	}

	//= MISC ====================================================================
	void RigidBody::ClearForces() const
	{
		if (!m_rigidBody)
			return;

		m_rigidBody->clearForces();
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

	void RigidBody::AddConstraint(Constraint* constraint)
	{
		m_constraints.emplace_back(constraint);
	}

	void RigidBody::RemoveConstraint(Constraint* constraint)
	{
		for (auto it = m_constraints.begin(); it != m_constraints.end(); )
		{
			auto itConstraint = *it;
			if (constraint->GetID() == itConstraint->GetID())
			{
				it = m_constraints.erase(it);
			}
			else
			{
				++it;
			}
		}

		Activate();
	}

	void RigidBody::SetShape(btCollisionShape* shape)
	{
		m_collisionShape = shape;
		if (m_collisionShape)
		{
			Body_AddToWorld();
		}
		else
		{
			Body_RemoveFromWorld();
		}
	}

	void RigidBody::Body_AddToWorld()
	{
		if (m_mass < 0.0f)
		{
			m_mass = 0.0f;
		}

		// Transfer inertia to new collision shape
		btVector3 localInertia = btVector3(0, 0, 0);
		if (m_collisionShape && m_rigidBody)
		{
			localInertia = m_rigidBody ? m_rigidBody->getLocalInertia() : localInertia;
			m_collisionShape->calculateLocalInertia(m_mass, localInertia);
		}
		
		Body_Release();

		// CONSTRUCTION
		{
			// Create a motion state (memory will be freed by the RigidBody)
			auto motionState = new MotionState(this);
			
			// Info
			btRigidBody::btRigidBodyConstructionInfo constructionInfo(m_mass, motionState, m_collisionShape, localInertia);
			constructionInfo.m_mass				= m_mass;
			constructionInfo.m_friction			= m_friction;
			constructionInfo.m_rollingFriction	= m_frictionRolling;
			constructionInfo.m_restitution		= m_restitution;
			constructionInfo.m_collisionShape	= m_collisionShape;
			constructionInfo.m_localInertia		= localInertia;
			constructionInfo.m_motionState		= motionState;

			m_rigidBody = new btRigidBody(constructionInfo);
			m_rigidBody->setUserPointer(this);
		}

		// Reapply constraint positions for new center of mass shift
		for (const auto& constraint : m_constraints)
		{
			constraint->ApplyFrames();
		}
		
		Flags_UpdateKinematic();
		Flags_UpdateGravity();

		// Transform
		SetPosition(GetTransform()->GetPosition());
		SetRotation(GetTransform()->GetRotation());

		// Constraints
		SetPositionLock(m_positionLock);
		SetRotationLock(m_rotationLock);

		// Add to world
		m_physics->GetWorld()->addRigidBody(m_rigidBody);
		if (m_mass > 0.0f)
		{
			Activate();
		}
		else
		{
			SetLinearVelocity(Vector3::Zero);
			SetAngularVelocity(Vector3::Zero);
		}

		m_hasSimulated	= false;
		m_inWorld		= true;
	}

	void RigidBody::Body_Release()
	{
		if (!m_rigidBody)
			return;

		// Release any constraints that refer to it
		for (const auto& constraint : m_constraints)
		{
			constraint->ReleaseConstraint();
		}

		// Remove it from the world
		Body_RemoveFromWorld();

		// Reset it
		m_rigidBody = nullptr;
	}

	void RigidBody::Body_RemoveFromWorld()
	{
		if (!m_rigidBody)
			return;

		if (m_inWorld)
		{
			m_physics->GetWorld()->removeRigidBody(m_rigidBody);
			delete m_rigidBody->getMotionState();
			delete m_rigidBody;
			m_rigidBody = nullptr;
			m_inWorld = false;
		}
	}

	void RigidBody::Body_AcquireShape()
	{
		if (const auto& collider = m_entity->GetComponent<Collider>())
		{
			m_collisionShape	= collider->GetShape();
			m_centerOfMass		= collider->GetCenter();
		}
	}

	void RigidBody::Flags_UpdateKinematic()
	{
		int flags = m_rigidBody->getCollisionFlags();

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
		m_rigidBody->setDeactivationTime(DEFAULT_DEACTIVATION_TIME);
	}

	void RigidBody::Flags_UpdateGravity()
	{
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
			btVector3 gravity = ToBtVector3(m_physics->GetGravity());
			m_rigidBody->setGravity(gravity);
		}
		else
		{
			m_rigidBody->setGravity(btVector3(0.0f, 0.0f, 0.0f));
		}
	}

	bool RigidBody::IsActivated() const
	{
		return m_rigidBody->isActive();
	}
}