/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES =======================
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Pools/GameObjectPool.h"
#include "../Core/GameObject.h"
#include "../IO/Log.h"
#include "../Signals/Signaling.h"
//==================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Transform::Transform()
{
	m_positionLocal = Vector3::Zero;
	m_rotationLocal = Quaternion::Identity;
	m_scaleLocal = Vector3::One;
	m_worldMatrix = Matrix::Identity;
	m_parent = nullptr;
}

Transform::~Transform()
{

}

//= INTERFACE ====================================================================================
void Transform::Initialize()
{
	UpdateWorldTransform();
}

void Transform::Start()
{

}

void Transform::Remove()
{

}

void Transform::Update()
{

}

void Transform::Serialize()
{
	Serializer::SaveVector3(m_positionLocal);
	Serializer::SaveQuaternion(m_rotationLocal);
	Serializer::SaveVector3(m_scaleLocal);
	Serializer::SaveVector3(m_lookAt);

	if (m_parent)
		Serializer::SaveSTR(m_parent->g_gameObject->GetID());
	else
		Serializer::SaveSTR(NULL_GAMEOBJECT_ID);
}

void Transform::Deserialize()
{
	m_positionLocal = Serializer::LoadVector3();
	m_rotationLocal = Serializer::LoadQuaternion();
	m_scaleLocal = Serializer::LoadVector3();
	m_lookAt = Serializer::LoadVector3();

	// get parent transform
	string parentGameObjectID = Serializer::LoadSTR();
	if (parentGameObjectID != NULL_GAMEOBJECT_ID)
	{
		GameObject* parent = GameObjectPool::GetInstance().GetGameObjectByID(parentGameObjectID);
		if (parent)
			parent->GetTransform()->AddChild(this);
	}

	UpdateWorldTransform();
}
//================================================================================================

void Transform::UpdateWorldTransform()
{
	// Create local translation, rotation and scale matrices
	Matrix translationLocalMatrix = Matrix::CreateTranslation(m_positionLocal);
	Matrix rotationLocalMatrix = m_rotationLocal.RotationMatrix();
	Matrix scaleLocalMatrix = Matrix::CreateScale(m_scaleLocal);

	// Calculate the world matrix
	Matrix localMatrix = scaleLocalMatrix * rotationLocalMatrix * translationLocalMatrix;
	m_worldMatrix = localMatrix * GetParentWorldTransform();

	// If there is no parent, local space equals world space
	if (!HasParent())
	{
		m_position = m_positionLocal;
		m_rotation = m_rotationLocal;
		m_scale = m_scaleLocal;
	}
	else // ... decompose world matrix to get world position, rotation and scale
		m_worldMatrix.Decompose(m_scale, m_rotation, m_position);

	// update children
	for (auto i = 0; i < m_children.size(); i++)
		if (m_children[i]) m_children[i]->UpdateWorldTransform();

	EMIT_SIGNAL(SIGNAL_TRANSFORM_UPDATED);
}

/*------------------------------------------------------------------------------
									[POSITION]
------------------------------------------------------------------------------*/
Vector3 Transform::GetPosition() const
{
	return m_position;
}

Vector3 Transform::GetPositionLocal() const
{
	return m_positionLocal;
}

void Transform::SetPosition(const Vector3& position)
{
	SetPositionLocal(!HasParent() ? position : GetParent()->GetWorldTransform().Inverted() * position);
}

void Transform::SetPositionLocal(const Vector3& position)
{
	if (m_positionLocal == position)
		return;

	m_positionLocal = position;
	UpdateWorldTransform();
}

/*------------------------------------------------------------------------------
								[ROTATION]
------------------------------------------------------------------------------*/

Quaternion Transform::GetRotation() const
{
	return m_rotation;
}

Quaternion Transform::GetRotationLocal() const
{
	return m_rotationLocal;
}

void Transform::SetRotation(const Quaternion& rotation)
{
	SetRotationLocal(!HasParent() ? rotation : GetParent()->GetRotation().Inverse() * rotation);
}

void Transform::SetRotationLocal(const Quaternion& rotation)
{
	if (m_rotationLocal == rotation)
		return;

	m_rotationLocal = rotation;
	UpdateWorldTransform();
}

/*------------------------------------------------------------------------------
								[SCALE]
------------------------------------------------------------------------------*/
Vector3 Transform::GetScale() const
{
	return m_scale;
}

Vector3 Transform::GetScaleLocal() const
{
	return m_scaleLocal;
}

void Transform::SetScale(const Vector3& scale)
{
	SetScaleLocal(!HasParent() ? scale : scale / GetParent()->GetScale());
}

void Transform::SetScaleLocal(const Vector3& scale)
{
	if (m_scaleLocal == scale)
		return;

	m_scaleLocal = scale;

	// A scale of 0 will cause a division by zero when 
	// decomposing the world transform matrix.
	if (m_scaleLocal.x == 0.0f) m_scaleLocal.x = M_EPSILON;
	if (m_scaleLocal.y == 0.0f) m_scaleLocal.y = M_EPSILON;
	if (m_scaleLocal.z == 0.0f) m_scaleLocal.z = M_EPSILON;

	UpdateWorldTransform();
}

//= TRANSLATION/ROTATION ========================================================
void Transform::Translate(const Vector3& delta)
{
	if (!HasParent())
		SetPositionLocal(m_positionLocal + delta);
	else
		SetPositionLocal(m_positionLocal + GetParent()->GetWorldTransform().Inverted() * delta);
}

void Transform::Rotate(const Quaternion& delta, Space space)
{
	Quaternion rotation;

	switch (space)
	{
	case Local:
		SetRotationLocal((m_rotationLocal * delta).Normalized());
		break;

	case World:
		if (!HasParent())
			SetRotationLocal((delta * m_rotationLocal).Normalized());
		else
			SetRotationLocal(m_rotationLocal * GetRotation().Inverse() * delta * GetRotation());
		break;
	}
}
//===============================================================================

Vector3 Transform::GetUp() const
{
	return GetRotation() * Vector3::Up;
}

Vector3 Transform::GetForward() const
{
	return GetRotation() * Vector3::Forward;
}

Vector3 Transform::GetRight() const
{
	return GetRotation() * Vector3::Right;
}

bool Transform::IsRoot()
{
	return !HasParent();
}

/*------------------------------------------------------------------------------
								[HIERARCHY]
------------------------------------------------------------------------------*/
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
			for (auto i = 0; i < m_children.size(); i++)
				m_children[i]->SetParent(GetParent());
		}
		else // if this transform doesn't have a parent
		{
			// make the children orphans
			for (auto i = 0; i < m_children.size(); i++)
				m_children[i]->BecomeOrphan();
		}
	}

	// Make this transform an orphan, this will also cause the 
	// parent to "forget" about this transform/child
	if (HasParent())
	{
		m_parent->FindChildren();
	}

	// save the new parent as the current parent
	m_parent = newParent;

	// make the new parent "aware" of this transform/child
	if (m_parent)
		m_parent->FindChildren();

	UpdateWorldTransform();
}

// Checks whether this transform has any children
bool Transform::HasChildren() const
{
	if (GetChildrenCount() > 0)
		return true;

	return false;
}

void Transform::AddChild(Transform* child)
{
	if (!child)
		return;

	if (GetID() == child->GetID())
		return;

	child->SetParent(this);
}

Transform* Transform::GetRoot()
{
	if (HasParent())
		return GetParent()->GetRoot();

	return this;
}

// Returns the parent of this transform
Transform* Transform::GetParent() const
{
	return m_parent;
}

// Returns a child with the given index
Transform* Transform::GetChildByIndex(int index)
{
	if (!HasChildren())
	{
		LOG(g_gameObject->GetName() + " has no children.", Log::Warning);
		return nullptr;
	}

	// prevent an out of vector bounds error
	if (index >= GetChildrenCount())
	{
		LOG("There is no child with an index of \"" + to_string(index) + "\".", Log::Warning);
		return nullptr;
	}

	return m_children[index];
}

Transform* Transform::GetChildByName(const std::string& name)
{
	for (auto i = 0; i < m_children.size(); i++)
		if (m_children[i]->GetName() == name)
			return m_children[i];

	return nullptr;
}

vector<Transform*> Transform::GetChildren() const
{
	return m_children;
}

// Returns the number of children
int Transform::GetChildrenCount() const
{
	return m_children.size();
}

// Searches the entiry hierarchy, finds any children and saves them in m_children.
// This is a recursive function, the children will also find their own children and so on...
void Transform::FindChildren()
{
	m_children.clear();
	m_children.shrink_to_fit();

	vector<GameObject*> gameObjects = GameObjectPool::GetInstance().GetAllGameObjects();
	for (auto i = 0; i < gameObjects.size(); i++)
	{
		// get the possible child
		Transform* possibleChild = gameObjects[i]->GetTransform();

		// if it doesn't have a parent, forget about it.
		if (!possibleChild->HasParent())
			continue;

		// if it's parent matches this transform
		if (possibleChild->GetParent()->GetID() == GetID())
		{
			// welcome home son
			m_children.push_back(possibleChild);

			// make the child do the same thing all over, essentialy
			// resolving the entire hierarchy.
			possibleChild->FindChildren();
		}
	}
}

bool Transform::IsDescendantOf(Transform* transform) const
{
	vector<Transform*> descendants = transform->GetDescendants();

	for (auto i = 0; i < descendants.size(); i++)
	{
		if (GetID() == descendants[i]->GetID())
			return true;
	}

	return false;
}

vector<Transform*> Transform::GetDescendants()
{
	vector<Transform*> descendants;

	// the recursion happens in another private function (it has the same name)
	// so we can keep our vector<Transform*> descendants intact and return it
	GetDescendants(descendants);

	return descendants;
}

string Transform::GetID() const
{
	return g_gameObject->GetID();
}

void Transform::LookAt(const Vector3& v)
{
	m_lookAt = v;
}

// Makes this transform have no parent
void Transform::BecomeOrphan()
{
	// if there is no parent, no need to do anything
	if (!m_parent) return;

	// create a temporary reference to the parent
	Transform* tempRef = m_parent;

	// delete the original reference
	m_parent = nullptr;

	// make the parent search for children,
	// that's indirect way of making tha parent "forget"
	// about this child, since it won't be able to find it
	if (tempRef)
		tempRef->FindChildren();
}

// Checks whether this transform has a parent
bool Transform::HasParent() const
{
	if (m_parent == nullptr)
		return false;

	return true;
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
Matrix Transform::GetWorldTransform() const
{
	return m_worldMatrix;
}

GameObject* Transform::GetGameObject() const
{
	return g_gameObject;
}

string Transform::GetName() const
{
	return GetGameObject()->GetName();
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void Transform::GetDescendants(vector<Transform*>& descendants)
{
	for (int i = 0; i < m_children.size(); i++)
	{
		descendants.push_back(m_children[i]);
		m_children[i]->GetDescendants(descendants);
	}
}

Matrix Transform::GetParentWorldTransform() const
{
	if (!HasParent())
		return Matrix::Identity;

	return GetParent()->GetWorldTransform();
}
