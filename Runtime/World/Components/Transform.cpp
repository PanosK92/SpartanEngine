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

//= INCLUDES ===================
#include "Spartan.h"
#include "Transform.h"
#include "../World.h"
#include "../Entity.h"
#include "../../IO/FileStream.h"
//==============================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    Transform::Transform(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id, this)
    {
        m_positionLocal = Vector3::Zero;
        m_rotationLocal = Quaternion(0, 0, 0, 1);
        m_scaleLocal    = Vector3::One;
        m_matrix        = Matrix::Identity;
        m_matrixLocal   = Matrix::Identity;
        m_wvp_previous  = Matrix::Identity;
        m_parent        = nullptr;

        REGISTER_ATTRIBUTE_VALUE_VALUE(m_positionLocal, Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotationLocal, Quaternion);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_scaleLocal,    Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrix,        Matrix);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrixLocal,   Matrix);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_lookAt,        Vector3);
    }

    void Transform::OnInitialize()
    {
        UpdateTransform();
    }

    void Transform::Serialize(FileStream* stream)
    {
        stream->Write(m_positionLocal);
        stream->Write(m_rotationLocal);
        stream->Write(m_scaleLocal);
        stream->Write(m_lookAt);
        stream->Write(m_parent ? m_parent->GetEntity()->GetId() : 0);
    }

    void Transform::Deserialize(FileStream* stream)
    {
        stream->Read(&m_positionLocal);
        stream->Read(&m_rotationLocal);
        stream->Read(&m_scaleLocal);
        stream->Read(&m_lookAt);
        uint32_t parententity_id = 0;
        stream->Read(&parententity_id);

        if (parententity_id != 0)
        {
            if (const auto parent = GetContext()->GetSubsystem<World>()->EntityGetById(parententity_id))
            {
                parent->GetTransform()->AddChild(this);
            }
        }

        UpdateTransform();
    }

    void Transform::UpdateTransform()
    {
        // Compute local transform
        m_matrixLocal = Matrix(m_positionLocal, m_rotationLocal, m_scaleLocal);

        // Compute world transform
        if (!HasParent())
        {
            m_matrix = m_matrixLocal;
        }
        else
        {
            m_matrix = m_matrixLocal * GetParentTransformMatrix();
        }
        
        // Update children
        for (const auto& child : m_children)
        {
            child->UpdateTransform();
        }
    }

    void Transform::SetPosition(const Vector3& position)
    {
        if (GetPosition() == position)
            return;

        SetPositionLocal(!HasParent() ? position : position * GetParent()->GetMatrix().Inverted());
    }

    void Transform::SetPositionLocal(const Vector3& position)
    {
        if (m_positionLocal == position)
            return;

        m_positionLocal = position;
        UpdateTransform();
    }

    void Transform::SetRotation(const Quaternion& rotation)
    {
        if (GetRotation() == rotation)
            return;

        SetRotationLocal(!HasParent() ? rotation : rotation * GetParent()->GetRotation().Inverse());
    }

    void Transform::SetRotationLocal(const Quaternion& rotation)
    {
        if (m_rotationLocal == rotation)
            return;

        m_rotationLocal = rotation;
        UpdateTransform();
    }

    void Transform::SetScale(const Vector3& scale)
    {
        if (GetScale() == scale)
            return;

        SetScaleLocal(!HasParent() ? scale : scale / GetParent()->GetScale());
    }

    void Transform::SetScaleLocal(const Vector3& scale)
    {
        if (m_scaleLocal == scale)
            return;

        m_scaleLocal = scale;

        // A scale of 0 will cause a division by zero when decomposing the world transform matrix.
        m_scaleLocal.x = (m_scaleLocal.x == 0.0f) ? Helper::EPSILON : m_scaleLocal.x;
        m_scaleLocal.y = (m_scaleLocal.y == 0.0f) ? Helper::EPSILON : m_scaleLocal.y;
        m_scaleLocal.z = (m_scaleLocal.z == 0.0f) ? Helper::EPSILON : m_scaleLocal.z;

        UpdateTransform();
    }

    void Transform::Translate(const Vector3& delta)
    {
        if (!HasParent())
        {
            SetPositionLocal(m_positionLocal + delta);
        }
        else
        {
            SetPositionLocal(m_positionLocal + GetParent()->GetMatrix().Inverted() * delta);
        }
    }

    void Transform::Rotate(const Quaternion& delta)
    {
        if (!HasParent())
        {
            SetRotationLocal((m_rotationLocal * delta).Normalized());
        }
        else
        {
            SetRotationLocal(m_rotationLocal * GetRotation().Inverse() * delta * GetRotation());
        }    
    }

    Vector3 Transform::GetUp() const
    {
        return GetRotationLocal() * Vector3::Up;
    }

    Vector3 Transform::GetDown() const
    {
        return GetRotationLocal() * Vector3::Down;
    }

    Vector3 Transform::GetForward() const
    {
        return GetRotationLocal() * Vector3::Forward;
    }

    Vector3 Transform::GetBackward() const
    {
        return GetRotationLocal() * Vector3::Backward;
    }

    Vector3 Transform::GetRight() const
    {
        return GetRotationLocal() * Vector3::Right;
    }

    Vector3 Transform::GetLeft() const
    {
        return GetRotationLocal() * Vector3::Left;
    }

    // Sets a parent for this transform
    void Transform::SetParent(Transform* new_parent)
    {
        // This is the most complex function 
        // in this script, tweak it with great caution.

        // if the new parent is null, it means that this should become a root transform
        if (!new_parent)
        {
            BecomeOrphan();
            return;
        }

        // make sure the new parent is not this transform
        if (GetId() == new_parent->GetId())
            return;

        // make sure the new parent is different from the existing parent
        if (HasParent())
        {
            if (GetParent()->GetId() == new_parent->GetId())
                return;
        }

        // if the new parent is a descendant of this transform
        if (new_parent->IsDescendantOf(this))
        {
            // if this transform already has a parent
            if (this->HasParent())
            {
                // assign the parent of this transform to the children
                for (const auto& child : m_children)
                {
                    child->SetParent(GetParent());
                }
            }
            else // if this transform doesn't have a parent
            {
                // make the children orphans
                for (const auto& child : m_children)
                {
                    child->BecomeOrphan();
                }
            }
        }

        // Switch parent but keep a pointer to the old one
        auto parent_old = m_parent;
        m_parent = new_parent;
        if (parent_old) parent_old->AcquireChildren(); // update the old parent (so it removes this child)

        // make the new parent "aware" of this transform/child
        if (m_parent)
        {
            m_parent->AcquireChildren();
        }

        UpdateTransform();
    }

    void Transform::AddChild(Transform* child)
    {
        if (!child)
            return;

        if (GetId() == child->GetId())
            return;

        child->SetParent(this);
    }

    // Returns a child with the given index
    Transform* Transform::GetChildByIndex(const uint32_t index)
    {
        if (!HasChildren())
        {
            LOG_WARNING("%s has no children.", GetEntityName().c_str());
            return nullptr;
        }

        // prevent an out of vector bounds error
        if (index >= GetChildrenCount())
        {
            LOG_WARNING("There is no child with an index of \"%d\".", index);
            return nullptr;
        }

        return m_children[index];
    }

    Transform* Transform::GetChildByName(const string& name)
    {
        for (const auto& child : m_children)
        {
            if (child->GetEntityName() == name)
                return child;
        }

        return nullptr;
    }

    // Searches the entire hierarchy, finds any children and saves them in m_children.
    // This is a recursive function, the children will also find their own children and so on...
    void Transform::AcquireChildren()
    {
        m_children.clear();
        m_children.shrink_to_fit();

        auto entities = GetContext()->GetSubsystem<World>()->EntityGetAll();
        for (const auto& entity : entities)
        {
            if (!entity)
                continue;

            // get the possible child
            auto possible_child = entity->GetTransform();

            // if it doesn't have a parent, forget about it.
            if (!possible_child->HasParent())
                continue;

            // if it's parent matches this transform
            if (possible_child->GetParent()->GetId() == GetId())
            {
                // welcome home son
                m_children.emplace_back(possible_child);

                // make the child do the same thing all over, essentially resolving the entire hierarchy.
                possible_child->AcquireChildren();
            }
        }
    }

    bool Transform::IsDescendantOf(const Transform* transform) const
    {
        for (const Transform* child : transform->GetChildren())
        {
            if (GetId() == child->GetId())
                return true;

            if (child->HasChildren())
            {
                if (IsDescendantOf(child))
                    return true;
            }
        }

        return false;
    }

    void Transform::GetDescendants(vector<Transform*>* descendants)
    {
        for (Transform* child : m_children)
        {
            descendants->emplace_back(child);

            if (child->HasChildren())
            {
                child->GetDescendants(descendants);
            }
        }
    }

    Matrix Transform::GetParentTransformMatrix() const
    {
        return HasParent() ? GetParent()->GetMatrix() : Matrix::Identity;
    }

    // Makes this transform have no parent
    void Transform::BecomeOrphan()
    {
        // if there is no parent, no need to do anything
        if (!m_parent)
            return;

        // create a temporary reference to the parent
        auto temp_ref = m_parent;

        // delete the original reference
        m_parent = nullptr;

        // Update the transform without the parent now
        UpdateTransform();

        // make the parent search for children,
        // that's indirect way of making the parent "forget"
        // about this child, since it won't be able to find it
        if (temp_ref)
        {
            temp_ref->AcquireChildren();
        }
    }
}
