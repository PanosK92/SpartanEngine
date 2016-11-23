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

#pragma once

//= INCLUDES ==================
#include "IComponent.h"
#include <vector>
#include "../Math/Vector3.h"
#include "../Math/Matrix.h"
#include "../Math/Quaternion.h"
//=============================

class DllExport Transform : public IComponent
{
public:
	enum Space
	{
		Local,
		World
	};

	Transform();
	~Transform();

	/*------------------------------------------------------------------------------
									[INTERFACE]
	------------------------------------------------------------------------------*/
	virtual void Initialize();
	virtual void Start();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	void UpdateWorldTransform();

	/*------------------------------------------------------------------------------
									[POSITION]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector3 GetPosition() { return m_position; }
	Directus::Math::Vector3 GetPositionLocal() { return m_positionLocal; }
	void SetPosition(const Directus::Math::Vector3& position);
	void SetPositionLocal(const Directus::Math::Vector3& position);

	/*------------------------------------------------------------------------------
									[ROTATION]
	------------------------------------------------------------------------------*/
	Directus::Math::Quaternion GetRotation() { return m_rotation; }
	Directus::Math::Quaternion GetRotationLocal() { return m_rotationLocal; }
	void SetRotation(const Directus::Math::Quaternion& rotation);
	void SetRotationLocal(const Directus::Math::Quaternion& rotation);

	/*------------------------------------------------------------------------------
									[SCALE]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector3 GetScale() { return m_scale; }
	Directus::Math::Vector3 GetScaleLocal() { return m_scaleLocal; }
	void SetScale(const Directus::Math::Vector3& scale);
	void SetScaleLocal(const Directus::Math::Vector3& scale);

	//= TRANSLATION/ROTATION ========================================================
	void Translate(const Directus::Math::Vector3& delta);
	void Rotate(const Directus::Math::Quaternion& delta, Space space);

	/*------------------------------------------------------------------------------
									[DIRECTIONS]
	------------------------------------------------------------------------------*/
	Directus::Math::Vector3 GetUp();
	Directus::Math::Vector3 GetForward();
	Directus::Math::Vector3 GetRight();

	/*------------------------------------------------------------------------------
								[HIERARCHY]
	------------------------------------------------------------------------------*/
	bool IsRoot() { return !HasParent(); }
	bool HasParent() { return m_parent ? true : false; }
	void SetParent(Transform* parent);
	void BecomeOrphan();
	bool HasChildren() { return GetChildrenCount() > 0 ? true : false; }
	void AddChild(Transform* child);
	Transform* GetRoot() { return HasParent() ? GetParent()->GetRoot() : this; }
	Transform* GetParent() { return m_parent; }
	Transform* GetChildByIndex(int index);
	Transform* GetChildByName(const std::string& name);
	std::vector<Transform*> GetChildren() { return m_children; }
	std::vector<GameObject*> GetChildrenAsGameObjects();
	int GetChildrenCount() { return (int)m_children.size(); }
	void ResolveChildrenRecursively();
	bool IsDescendantOf(Transform* transform) const;
	std::vector<Transform*> GetDescendants();
	std::string GetID() const;

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	void LookAt(const Directus::Math::Vector3& v) { m_lookAt = v; }
	Directus::Math::Matrix GetTransformMatrix() { return m_mTransform; }
	GameObject* GetGameObject() { return g_gameObject; }
	std::string GetName();

private:

	// local
	Directus::Math::Vector3 m_positionLocal;
	Directus::Math::Quaternion m_rotationLocal;
	Directus::Math::Vector3 m_scaleLocal;

	// world 
	Directus::Math::Vector3 m_position;
	Directus::Math::Quaternion m_rotation;
	Directus::Math::Vector3 m_scale;

	Directus::Math::Matrix m_mTransform;
	Directus::Math::Vector3 m_lookAt;

	Transform* m_parent; // the parent of this transform
	std::vector<Transform*> m_children; // the children of this transform

	/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void GetDescendants(std::vector<Transform*>& descendants);
	Directus::Math::Matrix GetParentTransformMatrix();
};
