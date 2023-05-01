/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "pch.h"
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
    Transform::Transform(Entity* entity, uint64_t id /*= 0*/) : IComponent(entity, id, this)
    {
        m_position_local  = Vector3::Zero;
        m_rotation_local  = Quaternion(0, 0, 0, 1);
        m_scale_local     = Vector3::One;
        m_matrix          = Matrix::Identity;
        m_matrix_local    = Matrix::Identity;
        m_matrix_previous = Matrix::Identity;
        m_parent          = nullptr;
        m_is_dirty        = true;

        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_local, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_local, Quaternion);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_scale_local,    Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrix,         Matrix);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrix_local,   Matrix);
    }

    void Transform::OnInitialize()
    {
        m_is_dirty = true;
    }

    void Transform::OnTick()
    {
        if (!m_is_dirty)
            return;

        UpdateTransform();

        m_is_dirty                    = false;
        m_position_changed_this_frame = false;
        m_rotation_changed_this_frame = true;
        m_scale_changed_this_frame    = true;
    }

    void Transform::Serialize(FileStream* stream)
    {
        // Properties
        stream->Write(m_position_local);
        stream->Write(m_rotation_local);
        stream->Write(m_scale_local);

        // Hierarchy
        stream->Write(m_parent ? m_parent->GetEntity()->GetObjectId() : 0);
    }

    void Transform::Deserialize(FileStream* stream)
    {
        // Properties
        stream->Read(&m_position_local);
        stream->Read(&m_rotation_local);
        stream->Read(&m_scale_local);

        // Hierarchy
        uint64_t parent_entity_id = 0;
        stream->Read(&parent_entity_id);
        if (parent_entity_id != 0)
        {
            if (const shared_ptr<Entity>& parent = World::GetEntityById(parent_entity_id))
            {
                parent->GetTransform()->AddChild(this);
            }
        }

        UpdateTransform();
    }

    void Transform::UpdateTransform()
    {
        // Compute local transform
        m_matrix_local = Matrix(m_position_local, m_rotation_local, m_scale_local);

        // Compute world transform
        if (m_parent)
        {
            m_matrix = m_matrix_local * m_parent->GetMatrix();
        }
        else
        {
            m_matrix = m_matrix_local;
        }

        // Update children
        for (Transform* child : m_children)
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
        if (m_position_local == position)
            return;

        m_position_local = position;
        UpdateTransform();

        m_position_changed_this_frame = true;
    }

    void Transform::SetRotation(const Quaternion& rotation)
    {
        if (GetRotation() == rotation)
            return;

        SetRotationLocal(!HasParent() ? rotation : rotation * GetParent()->GetRotation().Inverse());
    }

    void Transform::SetRotationLocal(const Quaternion& rotation)
    {
        if (m_rotation_local == rotation)
            return;

        m_rotation_local = rotation;
        UpdateTransform();

        m_rotation_changed_this_frame = true;
    }

    void Transform::SetScale(const Vector3& scale)
    {
        if (GetScale() == scale)
            return;

        SetScaleLocal(!HasParent() ? scale : scale / GetParent()->GetScale());
    }

    void Transform::SetScaleLocal(const Vector3& scale)
    {
        if (m_scale_local == scale)
            return;

        m_scale_local = scale;

        // A scale of 0 will cause a division by zero when decomposing the world transform matrix.
        m_scale_local.x = (m_scale_local.x == 0.0f) ? Helper::EPSILON : m_scale_local.x;
        m_scale_local.y = (m_scale_local.y == 0.0f) ? Helper::EPSILON : m_scale_local.y;
        m_scale_local.z = (m_scale_local.z == 0.0f) ? Helper::EPSILON : m_scale_local.z;

        UpdateTransform();

        m_scale_changed_this_frame = true;
    }

    void Transform::Translate(const Vector3& delta)
    {
        if (!HasParent())
        {
            SetPositionLocal(m_position_local + delta);
        }
        else
        {
            SetPositionLocal(m_position_local + GetParent()->GetMatrix().Inverted() * delta);
        }
    }

    void Transform::Rotate(const Quaternion& delta)
    {
        if (!HasParent())
        {
            SetRotationLocal((m_rotation_local * delta).Normalized());
        }
        else
        {
            SetRotationLocal(m_rotation_local * GetRotation().Inverse() * delta * GetRotation());
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

    Transform* Transform::GetChildByIndex(const uint32_t index)
    {
        if (!HasChildren())
        {
            SP_LOG_WARNING("%s has no children.", GetEntity()->GetObjectName().c_str());
            return nullptr;
        }

        // prevent an out of vector bounds error
        if (index >= GetChildrenCount())
        {
            SP_LOG_WARNING("There is no child with an index of \"%d\".", index);
            return nullptr;
        }

        return m_children[index];
    }

    Transform* Transform::GetChildByName(const string& name)
    {
        for (Transform* child : m_children)
        {
            if (child->GetEntity()->GetObjectName() == name)
                return child;
        }

        return nullptr;
    }

    void Transform::SetParent(Transform* new_parent)
    {
        // Early exit if the parent is this transform (which is invalid).
        if (new_parent)
        {
            if (GetObjectId() == new_parent->GetObjectId())
                return;
        }

        // Early exit if the parent is already set.
        if (m_parent && new_parent)
        {
            if (m_parent->GetObjectId() == new_parent->GetObjectId())
                return;
        }

        // If the new parent is a descendant of this transform (e.g. dragging and dropping an entity onto one of it's children).
        if (new_parent && new_parent->IsDescendantOf(this))
        {
            // Assign the parent of this transform to the children.
            for (Transform* child : m_children)
            {
                child->SetParent_Internal(m_parent);
            }

            // Remove any children
            m_children.clear();
        }

        // Remove this child from it's previous parent
        if (m_parent)
        {
            m_parent->RemoveChild_Internal(this);
        }

        // Add this child to the new parent
        if (new_parent)
        {
            new_parent->AddChild_Internal(this);
            new_parent->MakeDirty();
        }

        // Assign the new parent.
        m_parent   = new_parent;
        m_is_dirty = true;
    }

    void Transform::AddChild(Transform* child)
    {
        SP_ASSERT(child != nullptr);

        // Ensure that the child is not this transform.
        if (child->GetObjectId() == GetObjectId())
            return;

        child->SetParent(this);
    }

    void Transform::RemoveChild(Transform* child)
    {
        SP_ASSERT(child != nullptr);

        // Ensure the transform is not itself
        if (child->GetObjectId() == GetObjectId())
            return;

        // Remove the child.
        m_children.erase(remove_if(m_children.begin(), m_children.end(), [child](Transform* vec_transform) { return vec_transform->GetObjectId() == child->GetObjectId(); }), m_children.end());

        // Remove the child's parent.
        child->SetParent(nullptr);
    }

    void Transform::SetParent_Internal(Transform* new_parent)
    {
        // Ensure that parent is not this transform.
        if (new_parent)
        {
            if (GetObjectId() == new_parent->GetObjectId())
                return;
        }

        // Mark as dirty if the parent is about to really change.
        if ((m_parent && !new_parent) || (!m_parent && new_parent))
        {
            m_is_dirty = true;
        }

        // Assign the new parent.
        m_parent = new_parent;
    }

    void Transform::AddChild_Internal(Transform* child)
    {
        SP_ASSERT(child != nullptr);

        // Ensure that the child is not this transform.
        if (child->GetObjectId() == GetObjectId())
            return;

        lock_guard lock(m_child_add_remove_mutex);

        // If this is not already a child, add it.
        if (!(find(m_children.begin(), m_children.end(), child) != m_children.end()))
        {
            m_children.emplace_back(child);
        }
    }

    void Transform::RemoveChild_Internal(Transform* child)
    {
        SP_ASSERT(child != nullptr);

        // Ensure the transform is not itself
        if (child->GetObjectId() == GetObjectId())
            return;

        lock_guard lock(m_child_add_remove_mutex);

        // Remove the child
        m_children.erase(remove_if(m_children.begin(), m_children.end(), [child](Transform* vec_transform) { return vec_transform->GetObjectId() == child->GetObjectId(); }), m_children.end());
    }

    // Searches the entire hierarchy, finds any children and saves them in m_children.
    // This is a recursive function, the children will also find their own children and so on...
    void Transform::AcquireChildren()
    {
        m_children.clear();
        m_children.shrink_to_fit();

        auto entities = World::GetAllEntities();
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
            if (possible_child->GetParent()->GetObjectId() == GetObjectId())
            {
                // welcome home son
                m_children.emplace_back(possible_child);

                // make the child do the same thing all over, essentially resolving the entire hierarchy.
                possible_child->AcquireChildren();
            }
        }
    }

    bool Transform::IsDescendantOf(Transform* transform) const
    {
        SP_ASSERT(transform != nullptr);

        if (!m_parent)
            return false;

        if (m_parent->GetObjectId() == transform->GetObjectId())
            return true;

        for (Transform* child : transform->GetChildren())
        {
            if (IsDescendantOf(child))
                return true;
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

    Entity* Transform::GetDescendantByName(const std::string& name)
    {
        vector<Transform*> descendants;
        GetDescendants(&descendants);

        for (Transform* transform : descendants)
        {
            if (transform->GetEntity()->GetObjectName() == name)
                return transform->GetEntity();
        }

        return nullptr;
    }

    Matrix Transform::GetParentTransformMatrix() const
    {
        return HasParent() ? GetParent()->GetMatrix() : Matrix::Identity;
    }
}
