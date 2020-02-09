/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "SoftBody.h"
#include "Transform.h"
#include "../../Core/Engine.h"
#include "../../Core/Context.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletSoftBody/btSoftBody.h>
#include <BulletSoftBody/btSoftBodyHelpers.h>
#pragma warning(pop)
//==============================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    SoftBody::SoftBody(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_physics = m_context->GetSubsystem<Physics>().get();
    }

    SoftBody::~SoftBody()
    {
        Body_Release();
    }

    void SoftBody::OnInitialize()
    {
        // Box
        const btVector3 position    = btVector3(0, 0, 0);
        const btVector3 size        = btVector3(1, 1, 1);
        const btVector3 extent      = size * 0.5f;
        const btVector3 vertices[]  =
        {
            position + extent * btVector3(-1, -1, -1),
            position + extent * btVector3(+1, -1, -1),
            position + extent * btVector3(-1, +1, -1),
            position + extent * btVector3(+1, +1, -1),
            position + extent * btVector3(-1, -1, +1),
            position + extent * btVector3(+1, -1, +1),
            position + extent * btVector3(-1, +1, +1),
            position + extent * btVector3(+1, +1, +1)
        };

        m_soft_body = btSoftBodyHelpers::CreateFromConvexHull(m_physics->GetSoftWorldInfo(), vertices, 8);
        m_soft_body->generateBendingConstraints(2);

        //TRACEDEMO
        //const btScalar s = 5;
        //const int segments = 10;
        //const int count = 5;
        //btVector3 pos(-s * segments, 0, 0);
        //btScalar gap = 0.5;
        //btSoftBody* psb = btSoftBodyHelpers::CreatePatch(m_physics->GetSoftWorldInfo(), btVector3(-s, 0, -s * 3),
        //    btVector3(+s, 0, -s * 3),
        //    btVector3(-s, 0, +s),
        //    btVector3(+s, 0, +s),
        //    segments, segments * 3,
        //    1 + 2, true);
        //psb->getCollisionShape()->setMargin(0.5);
        //btSoftBody::Material* pm = psb->appendMaterial();
        //pm->m_kLST = 0.0004;
        //pm->m_flags -= btSoftBody::fMaterial::DebugDraw;
        //psb->generateBendingConstraints(2, pm);
        //psb->m_cfg.kLF = 0.05;
        //psb->m_cfg.kDG = 0.01;
        //psb->m_cfg.piterations = 2;
        //psb->m_cfg.aeromodel = btSoftBody::eAeroModel::V_TwoSidedLiftDrag;
        //psb->setWindVelocity(btVector3(4, -12.0, -25.0));
        //btTransform trs;
        //btQuaternion rot;
        //pos += btVector3(s * 2 + gap, 0, 0);
        //rot.setRotation(btVector3(1, 0, 0), btScalar(SIMD_PI / 2));
        //trs.setIdentity();
        //trs.setOrigin(pos);
        //trs.setRotation(rot);
        //psb->transform(trs);
        //psb->setTotalMass(2.0);
        ////this could help performance in some cases
        //btSoftBodyHelpers::ReoptimizeLinkOrder(psb);
        //m_physics->AddBody(psb);

        m_mass = 30.0f;
        Body_AddToWorld();
    }

    void SoftBody::OnRemove()
    {

    }

    void SoftBody::OnStart()
    {

    }

    void SoftBody::OnTick(float delta_time)
    {
        // When in editor mode, set engine transform to bullet (so the user can move the body around)
        if (!m_context->m_engine->EngineMode_IsSet(Engine_Game))
        {
            if (GetPosition() != GetTransform()->GetPosition())
            {
                SetPosition(GetTransform()->GetPosition());
            }

            if (GetRotation() != GetTransform()->GetRotation())
            {
                SetRotation(GetTransform()->GetRotation());
            }
        }
    }

    void SoftBody::Serialize(FileStream* stream)
    {

    }

    void SoftBody::Deserialize(FileStream* stream)
    {

    }

    Math::Vector3 SoftBody::GetPosition() const
    {
        if (m_soft_body)
        {
            const btTransform& transform = m_soft_body->getWorldTransform();
            return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_center_of_mass;
        }

        return Math::Vector3::Zero;
    }

    void SoftBody::SetPosition(const Math::Vector3& position)
    {
        if (!m_soft_body)
            return;

        // Set position to world transform
        btTransform& worldTrans = m_soft_body->getWorldTransform();
        worldTrans.setOrigin(ToBtVector3(position + ToQuaternion(worldTrans.getRotation()) * m_center_of_mass));

        Activate();
    }

    Math::Quaternion SoftBody::GetRotation() const
    {
        return m_soft_body ? ToQuaternion(m_soft_body->getWorldTransform().getRotation()) : Math::Quaternion::Identity;
    }

    void SoftBody::SetRotation(const Math::Quaternion& rotation)
    {
        if (!m_soft_body)
            return;

        // Set rotation to world transform
        Math::Vector3 oldPosition = GetPosition();
        btTransform& worldTrans = m_soft_body->getWorldTransform();
        worldTrans.setRotation(ToBtQuaternion(rotation));
        if (m_center_of_mass != Math::Vector3::Zero)
        {
            worldTrans.setOrigin(ToBtVector3(oldPosition + rotation * m_center_of_mass));
        }

        Activate();
    }

    void SoftBody::Activate() const
    {
        if (!m_soft_body)
            return;

        if (m_mass > 0.0f)
        {
            m_soft_body->activate(true);
        }
    }

    void SoftBody::Body_Release()
    {
        if (!m_soft_body)
            return;

        // Remove it from the world
        Body_RemoveFromWorld();

        // Reset it
        m_soft_body = nullptr;
    }

    void SoftBody::Body_AddToWorld()
    {
        if (!m_physics)
            return;

        m_soft_body->setTotalMass(m_mass);
        m_physics->AddBody(m_soft_body);
        m_in_world = true;
    }

    void SoftBody::Body_RemoveFromWorld()
    {
        if (!m_physics || !m_soft_body || !m_in_world)
            return;

        m_physics->RemoveBody(m_soft_body);
        m_in_world  = false;
    }
}
