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

//= INCLUDES =================================
#include "Spartan.h"
#include "SoftBody.h"
#include "Transform.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    SoftBody::SoftBody(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_physics = m_context->GetSubsystem<Physics>();
    }

    SoftBody::~SoftBody()
    {
        Body_Release();
    }

    void SoftBody::OnInitialize()
    {
        // Test
        m_mass = 30.0f;
        CreateBox();
        CreateAeroCloth();
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

    void SoftBody::SetPosition(const Math::Vector3& position) const
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

    void SoftBody::SetRotation(const Math::Quaternion& rotation) const
    {
        if (!m_soft_body)
            return;

        // Set rotation to world transform
        const Math::Vector3 oldPosition = GetPosition();
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

    void SoftBody::CreateBox()
    {
        const btVector3 position = btVector3(0, 0, 0);
        const btVector3 size = btVector3(1, 1, 1);
        const btVector3 extent = size * 0.5f;
        const btVector3 vertices[] =
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

        m_physics->AddBody(m_soft_body);
    }

    void SoftBody::CreateAeroCloth() const
    {
        const btScalar s = 5;
        const int segments = 10;
        const int count = 5;
        btVector3 pos(-s * segments, 0, 0);
        const btScalar gap = 0.5;
        btSoftBody* psb = btSoftBodyHelpers::CreatePatch(m_physics->GetSoftWorldInfo(), btVector3(-s, 0, -s * 3),
            btVector3(+s, 0, -s * 3),
            btVector3(-s, 0, +s),
            btVector3(+s, 0, +s),
            segments, segments * 3,
            1 + 2, true);
        psb->getCollisionShape()->setMargin(0.5);
        btSoftBody::Material* pm = psb->appendMaterial();
        psb->m_cfg.aeromodel = btSoftBody::eAeroModel::V_TwoSidedLiftDrag;
        pm->m_kLST              = 0.9f; // Linear stiffness coefficient [0,1]
        pm->m_kAST              = 0.9f; // Area/Angular stiffness coefficient [0,1]
        pm->m_kVST              = 0.9f; // Volume stiffness coefficient [0,1]

        psb->m_cfg.kVCF         = 1.0f;  // Velocities correction factor (Baumgarte)
        psb->m_cfg.kDP          = 0.0f;  // Damping coefficient [0,1]
        psb->m_cfg.kDG          = 0.01f; // Drag coefficient [0,+inf]
        psb->m_cfg.kLF          = 0.1f;  // Lift coefficient [0,+inf]
        psb->m_cfg.kPR          = 0.0f;  // Pressure coefficient [-inf,+inf]
        psb->m_cfg.kVC          = 0.0f;  // Volume conversation coefficient [0,+inf]
        psb->m_cfg.kDF          = 0.2f;  // Dynamic friction coefficient [0,1]
        psb->m_cfg.kMT          = 0.0f;  // Pose matching coefficient [0,1]
        psb->m_cfg.kCHR         = 0.1f;  // Rigid contacts hardness [0,1]
        psb->m_cfg.kKHR         = 0.0f;  // Kinetic contacts hardness [0,1]
        psb->m_cfg.kSHR         = 1.0f;  // Soft contacts hardness [0,1]
        psb->m_cfg.kAHR         = 0.7f;  // Anchors hardness [0,1]

        psb->m_cfg.kSRHR_CL     = 0.0f;  // Soft vs rigid hardness [0,1] (cluster only)
        psb->m_cfg.kSKHR_CL     = 0.0f;  // Soft vs kinetic hardness [0,1] (cluster only)
        psb->m_cfg.kSSHR_CL     = 0.0f;  // Soft vs soft hardness [0,1] (cluster only)
        psb->m_cfg.kSR_SPLT_CL  = 0.0f;  // Soft vs rigid impulse split [0,1] (cluster only)
        psb->m_cfg.kSK_SPLT_CL  = 0.0f;  // Soft vs rigid impulse split [0,1] (cluster only)
        psb->m_cfg.kSS_SPLT_CL  = 0.0f;  // Soft vs rigid impulse split [0,1] (cluster only)

        pm->m_flags -= btSoftBody::fMaterial::DebugDraw;
        psb->m_cfg.piterations = 2;
        psb->generateBendingConstraints(2, pm);
        psb->setWindVelocity(btVector3(4, -12.0, -25.0));
        btTransform trs;
        btQuaternion rot;
        pos += btVector3(s * 2 + gap, 0, 0);
        rot.setRotation(btVector3(1, 0, 0), btScalar(SIMD_PI / 2));
        trs.setIdentity();
        trs.setOrigin(pos);
        trs.setRotation(rot);
        psb->transform(trs);
        psb->setTotalMass(2.0);
        //this could help performance in some cases
        btSoftBodyHelpers::ReoptimizeLinkOrder(psb);
        psb->setPose(true, true);
        m_physics->AddBody(psb);
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
