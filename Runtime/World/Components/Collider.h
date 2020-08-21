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

//= INCLUDES ==================
#include "IComponent.h"
#include "../../Math/Vector3.h"
//=============================

class btCollisionShape;

namespace Spartan
{
    class Mesh;

    enum ColliderShape
    {
        ColliderShape_Box,
        ColliderShape_Sphere,
        ColliderShape_StaticPlane,
        ColliderShape_Cylinder,
        ColliderShape_Capsule,
        ColliderShape_Cone,
        ColliderShape_Mesh,
    };

    class SPARTAN_CLASS Collider : public IComponent
    {
    public:
        Collider(Context* context, Entity* entity, uint32_t id = 0);
        ~Collider() = default;

        //= ICOMPONENT ===============================
        void OnInitialize() override;
        void OnRemove() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        // Bounding box
        const Math::Vector3& GetBoundingBox() const { return m_size; }
        void SetBoundingBox(const Math::Vector3& boundingBox);

        // Collider center
        const Math::Vector3& GetCenter() const { return m_center; }
        void SetCenter(const Math::Vector3& center);

        // Collision shape type
        ColliderShape GetShapeType() const { return m_shapeType; }
        void SetShapeType(ColliderShape type);

        // Collision shape
        const auto& GetShape() const { return m_shape; }

        bool GetOptimize() const { return m_optimize; }
        void SetOptimize(bool optimize);

    private:
        void Shape_Update();
        void Shape_Release();
        void RigidBody_SetShape(btCollisionShape* shape) const;
        void RigidBody_SetCenterOfMass(const Math::Vector3& center) const;

        ColliderShape m_shapeType;
        btCollisionShape* m_shape;
        Math::Vector3 m_size;
        Math::Vector3 m_center;
        uint32_t m_vertexLimit = 100000;
        bool m_optimize = true;
    };
}
