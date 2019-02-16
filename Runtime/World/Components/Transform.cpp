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

//= INCLUDES ===========================
#include "Transform.h"
#include "../Entity.h"
#include "../../Logging/Log.h"
#include "../../IO/FileStream.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Core/Engine.h"
#include "../../Math/Vector3.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Transform::Transform(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_positionLocal		= Vector3::Zero;
		m_rotationLocal		= Quaternion(0, 0, 0, 1);
		m_scaleLocal		= Vector3::One;
		m_matrix			= Matrix::Identity;
		m_matrixLocal		= Matrix::Identity;
		m_wvp_previous		= Matrix::Identity;
		m_parent			= nullptr;

		REGISTER_ATTRIBUTE_VALUE_VALUE(m_positionLocal,	Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotationLocal,	Quaternion);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_scaleLocal, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrix, Matrix);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_matrixLocal, Matrix);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_lookAt, Vector3);
	}

	Transform::~Transform()
	{

	}

	//= ICOMPONENT ==================================================================================
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
		stream->Write(m_parent ? m_parent->GetEntity_PtrRaw()->GetID() : NOT_ASSIGNED_HASH);
	}

	void Transform::Deserialize(FileStream* stream)
	{
		stream->Read(&m_positionLocal);
		stream->Read(&m_rotationLocal);
		stream->Read(&m_scaleLocal);
		stream->Read(&m_lookAt);
		unsigned int parententityID = 0;
		stream->Read(&parententityID);

		if (parententityID != NOT_ASSIGNED_HASH)
		{
			if (auto parent = GetContext()->GetSubsystem<World>()->Entity_GetByID(parententityID))
			{
				parent->GetTransform_PtrRaw()->AddChild(this);
			}
		}

		UpdateTransform();
	}
	//===============================================================================================
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

	//= TRANSLATION ==================================================================================
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
	//================================================================================================

	//= ROTATION =====================================================================================
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
	//================================================================================================

	//= SCALE ========================================================================================
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

		// A scale of 0 will cause a division by zero when 
		// decomposing the world transform matrix.
		m_scaleLocal.x = (m_scaleLocal.x == 0.0f) ? M_EPSILON : m_scaleLocal.x;
		m_scaleLocal.y = (m_scaleLocal.y == 0.0f) ? M_EPSILON : m_scaleLocal.y;
		m_scaleLocal.z = (m_scaleLocal.z == 0.0f) ? M_EPSILON : m_scaleLocal.z;

		UpdateTransform();
	}
	//================================================================================================

	//= TRANSLATION/ROTATION =========================================================================
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

	Vector3 Transform::GetUp()
	{
		return GetRotationLocal() * Vector3::Up;
	}

	Vector3 Transform::GetForward()
	{
		return GetRotationLocal() * Vector3::Forward;
	}

	Vector3 Transform::GetRight()
	{
		return GetRotationLocal() * Vector3::Right;
	}
	//================================================================================================

	//= HIERARCHY ====================================================================================
	// Sets a parent for this transform
	void Transform::SetParent(Transform* newParent)
	{
		// This is the most complex function 
		// in this script, tweak it with great caution.

		// if the new parent is null, it means that this should become a root transform
		if (!newParent)
		{
			BecomeOrphan();
			return;
		}

		// make sure the new parent is not this transform
		if (GetID() == newParent->GetID())
			return;

		// make sure the new parent is different from the existing parent
		if (HasParent())
		{
			if (GetParent()->GetID() == newParent->GetID())
				return;
		}

		// if the new parent is a descendant of this transform
		if (newParent->IsDescendantOf(this))
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
		auto parentOld = m_parent;
		m_parent = newParent;
		if (parentOld) parentOld->AcquireChildren(); // update the old parent (so it removes this child)

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

		if (GetID() == child->GetID())
			return;

		child->SetParent(this);
	}

	// Returns a child with the given index
	Transform* Transform::GetChildByIndex(int index)
	{
		if (!HasChildren())
		{
			LOG_WARNING(GetentityName() + " has no children.");
			return nullptr;
		}

		// prevent an out of vector bounds error
		if (index >= GetChildrenCount())
		{
			LOG_WARNING("There is no child with an index of \"" + to_string(index) + "\".");
			return nullptr;
		}

		return m_children[index];
	}

	Transform* Transform::GetChildByName(const string& name)
	{
		for (const auto& child : m_children)
		{
			if (child->GetentityName() == name)
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

		auto entities = GetContext()->GetSubsystem<World>()->Entities_GetAll();
		for (const auto& entity : entities)
		{
			if (!entity)
				continue;

			// get the possible child
			Transform* possibleChild = entity->GetTransform_PtrRaw();

			// if it doesn't have a parent, forget about it.
			if (!possibleChild->HasParent())
				continue;

			// if it's parent matches this transform
			if (possibleChild->GetParent()->GetID() == m_ID)
			{
				// welcome home son
				m_children.emplace_back(possibleChild);

				// make the child do the same thing all over, essentialy
				// resolving the entire hierarchy.
				possibleChild->AcquireChildren();
			}
		}
	}

	bool Transform::IsDescendantOf(Transform* transform)
	{
		vector<Transform*> descendants;
		transform->GetDescendants(&descendants);

		for (const auto& descendant : descendants)
		{
			if (descendant->GetID() == m_ID)
				return true;
		}

		return false;
	}

	void Transform::GetDescendants(vector<Transform*>* descendants)
	{
		// Depth first acquisition of descendants
		for (const auto& child : m_children)
		{
			descendants->push_back(child);
			child->GetDescendants(descendants);
		}
	}

	Matrix Transform::GetParentTransformMatrix()
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
		Transform* tempRef = m_parent;

		// delete the original reference
		m_parent = nullptr;

		// Update the transform without the parent now
		UpdateTransform();

		// make the parent search for children,
		// that's indirect way of making tha parent "forget"
		// about this child, since it won't be able to find it
		if (tempRef)
		{
			tempRef->AcquireChildren();
		}
	}
}