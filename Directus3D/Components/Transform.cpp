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

//= INCLUDES ========================
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "../EventSystem/EventHandler.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Transform::Transform()
{
	m_positionLocal = Vector3::Zero;
	m_rotationLocal = Quaternion::Identity;
	m_scaleLocal = Vector3::One;
	m_mTransform = Matrix::Identity;
	m_parent = nullptr;
}

Transform::~Transform()
{

}

//==========
// INTERFACE
//==========
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
	Serializer::WriteVector3(m_positionLocal);
	Serializer::WriteQuaternion(m_rotationLocal);
	Serializer::WriteVector3(m_scaleLocal);
	Serializer::WriteVector3(m_lookAt);

	if (m_parent)
		Serializer::WriteSTR(m_parent->g_gameObject->GetID());
	else
		Serializer::WriteSTR(DATA_NOT_ASSIGNED);
}

void Transform::Deserialize()
{
	m_positionLocal = Serializer::ReadVector3();
	m_rotationLocal = Serializer::ReadQuaternion();
	m_scaleLocal = Serializer::ReadVector3();
	m_lookAt = Serializer::ReadVector3();

	// get parent transform
	string parentGameObjectID = Serializer::ReadSTR();
	if (parentGameObjectID != DATA_NOT_ASSIGNED)
	{
		GameObject* parent = g_context->GetSubsystem<Scene>()->GetGameObjectByID(parentGameObjectID);
		if (parent)
			parent->GetTransform()->AddChild(this);
	}

	UpdateWorldTransform();
}

//=====================
// UpdateWorldTransform
//=====================
void Transform::UpdateWorldTransform()
{
	// Create local translation, rotation and scale matrices
	Matrix translationLocalMatrix = Matrix::CreateTranslation(m_positionLocal);
	Matrix rotationLocalMatrix = m_rotationLocal.RotationMatrix();
	Matrix scaleLocalMatrix = Matrix::CreateScale(m_scaleLocal);

	// Calculate the world matrix
	Matrix localMatrix = scaleLocalMatrix * rotationLocalMatrix * translationLocalMatrix;
	m_mTransform = localMatrix * GetParentTransformMatrix();

	// If there is no parent, local space equals world space
	if (!HasParent())
	{
		m_position = m_positionLocal;
		m_rotation = m_rotationLocal;
		m_scale = m_scaleLocal;
	}
	else // ... decompose world matrix to get world position, rotation and scale
		m_mTransform.Decompose(m_scale, m_rotation, m_position);

	// update children
	for (auto i = 0; i < m_children.size(); i++)
		if (m_children[i]) m_children[i]->UpdateWorldTransform();
}

//=========
// POSITION
//=========
void Transform::SetPosition(const Vector3& position)
{
	SetPositionLocal(!HasParent() ? position : GetParent()->GetTransformMatrix().Inverted() * position);
}

void Transform::SetPositionLocal(const Vector3& position)
{
	if (m_positionLocal == position)
		return;

	m_positionLocal = position;
	UpdateWorldTransform();
}

//=========
// ROTATION
//=========
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

//======
// SCALE
//======
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

//=====================
// TRANSLATION/ROTATION
//=====================
void Transform::Translate(const Vector3& delta)
{
	if (!HasParent())
		SetPositionLocal(m_positionLocal + delta);
	else
		SetPositionLocal(m_positionLocal + GetParent()->GetTransformMatrix().Inverted() * delta);
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

Vector3 Transform::GetUp()
{
	return GetRotation() * Vector3::Up;
}

Vector3 Transform::GetForward()
{
	return GetRotation() * Vector3::Forward;
}

Vector3 Transform::GetRight()
{
	return GetRotation() * Vector3::Right;
}

//==========
// HIERARCHY
//==========
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
		m_parent->ResolveChildrenRecursively();
	}

	// save the new parent as the current parent
	m_parent = newParent;

	// make the new parent "aware" of this transform/child
	if (m_parent)
		m_parent->ResolveChildrenRecursively();

	UpdateWorldTransform();
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
		LOG_WARNING(g_gameObject->GetName() + " has no children.");
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
	for (auto i = 0; i < m_children.size(); i++)
		if (m_children[i]->GetName() == name)
			return m_children[i];

	return nullptr;
}

vector<GameObject*> Transform::GetChildrenAsGameObjects()
{
	vector<GameObject*> childreGameObjects;

	auto childrenTransforms = GetChildren();
	for (const auto& transform : childrenTransforms)
		childreGameObjects.push_back(transform->GetGameObject());

	return childreGameObjects;
}

// Searches the entiry hierarchy, finds any children and saves them in m_children.
// This is a recursive function, the children will also find their own children and so on...
void Transform::ResolveChildrenRecursively()
{
	m_children.clear();
	m_children.shrink_to_fit();

	auto gameObjects = g_context->GetSubsystem<Scene>()->GetAllGameObjects();
	for (const auto& gameObject : gameObjects)
	{
		// get the possible child
		Transform* possibleChild = gameObject->GetTransform();

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
			possibleChild->ResolveChildrenRecursively();
		}
	}
}

bool Transform::IsDescendantOf(Transform* transform) const
{
	vector<Transform*> descendants;
	transform->GetDescendants(descendants);

	for (const auto& descendant : descendants)
		if (descendant->GetID() == GetID())
			return true;

	return false;
}

void Transform::GetDescendants(vector<Transform*>& descendants)
{
	// Depth first acquisition of descendants
	for (const auto& child : m_children)
	{
		descendants.push_back(child);
		child->GetDescendants(descendants);
	}
}

string Transform::GetID() const
{
	return g_gameObject->GetID();
}

string Transform::GetName()
{
	return GetGameObject()->GetName();
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

	// make the parent search for children,
	// that's indirect way of making tha parent "forget"
	// about this child, since it won't be able to find it
	if (tempRef)
		tempRef->ResolveChildrenRecursively();
}