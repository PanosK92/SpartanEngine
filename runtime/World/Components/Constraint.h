/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =====================
#include "Component.h"
#include "../../Math/Vector3.h"
#include "../../Math/Vector2.h"
#include "../../Math/Quaternion.h"
//================================

class btTypedConstraint;

namespace spartan
{
    class PhysicsBody;
    class Entity;
    class Physics;

    enum ConstraintType
    {
        ConstraintType_Point,
        ConstraintType_Hinge,
        ConstraintType_Slider,
        ConstraintType_ConeTwist
    };

    class Constraint : public Component
    {
    public:
        Constraint(Entity* entity);
        ~Constraint();

        //= COMPONENT ================================
        void OnInitialize() override;
        void OnStart() override;
        void OnStop() override;
        void OnRemove() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        ConstraintType GetConstraintType() const { return m_constraintType; }
        void SetConstraintType(ConstraintType type);

        const math::Vector2& GetHighLimit() const { return m_highLimit; }
        // Set high limit. Interpretation is constraint type specific.
        void SetHighLimit(const math::Vector2& limit);

        const math::Vector2& GetLowLimit() const { return m_lowLimit; }
        // Set low limit. Interpretation is constraint type specific.
        void SetLowLimit(const math::Vector2& limit);

        const math::Vector3& GetPosition() const { return m_position; }
        // Set constraint position relative to own body.
        void SetPosition(const math::Vector3& position);

        const math::Quaternion& GetRotation() const { return m_rotation; }
        // Set constraint rotation relative to own body.
        void SetRotation(const math::Quaternion& rotation);

        const math::Vector3& GetPositionOther() const { return m_positionOther; }
        // Set constraint position relative to other body.
        void SetPositionOther(const math::Vector3& position);

        const math::Quaternion& GetRotationOther() const { return m_rotationOther; }
        // Set constraint rotation relative to other body.
        void SetRotationOther(const math::Quaternion& rotation);
        
        std::weak_ptr<Entity> GetBodyOther() const { return m_bodyOther; }
        void SetBodyOther(const std::weak_ptr<Entity>& body_other);

        void ReleaseConstraint();
        void ApplyFrames() const;

    private:
        void Construct();
        void ApplyLimits() const;
        
        btTypedConstraint* m_constraint;

        ConstraintType m_constraintType;
        math::Vector3 m_position;
        math::Quaternion m_rotation;
        math::Vector2 m_highLimit;
        math::Vector2 m_lowLimit;

        std::weak_ptr<Entity> m_bodyOther;
        math::Vector3 m_positionOther;
        math::Quaternion m_rotationOther;
    
        float m_errorReduction;
        float m_constraintForceMixing;
        bool m_enabledEffective;
        bool m_collisionWithLinkedBody;
        bool m_deferredConstruction;
    };
}
