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

#pragma once

//= INCLUDES =====================
#include "IComponent.h"
#include "..\..\Math\Vector3.h"
#include "..\..\Math\Quaternion.h"
//================================

// = BULLET FORWARD DECLARATIONS =
class btSoftBody;
//================================

namespace Spartan
{
    // = FORWARD DECLARATIONS =
    class Physics;
    //=========================

    class SPARTAN_CLASS SoftBody : public IComponent
    {
    public:
        SoftBody(Context* context, Entity* entity, uint32_t id = 0);
        ~SoftBody();

        //= ICOMPONENT ===============================
        void OnInitialize() override;
        void OnRemove() override;
        void OnStart() override;
        void OnTick(float delta_time) override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        //= POSITION =======================================
        Math::Vector3 GetPosition() const;
        void SetPosition(const Math::Vector3& position) const;
        //==================================================

        //= ROTATION ======================================
        Math::Quaternion GetRotation() const;
        void SetRotation(const Math::Quaternion& rotation) const;
        //=================================================

        void Activate() const;
        const Math::Vector3& GetCenterOfMass() const { return m_center_of_mass; }

    private:
        void CreateBox();
        void CreateAeroCloth() const;

        void Body_Release();
        void Body_AddToWorld();
        void Body_RemoveFromWorld();

        Physics* m_physics              = nullptr;
        btSoftBody* m_soft_body         = nullptr;
        bool m_in_world                 = false;
        Math::Vector3 m_center_of_mass  = Math::Vector3::Zero;
        float m_mass                    = 0.0f;
    };
}
