/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ======
#include "Widget.h"
#include <memory>
//=================

namespace Directus { class GameObject; }

class Hierarchy : public Widget
{
public:
	Hierarchy();
	void Initialize(Directus::Context* context) override;
	void Update() override;


	static std::weak_ptr<Directus::GameObject> GetSelectedGameObject() { return m_gameObjectSelected; }
	static void SetSelectedGameObject(std::weak_ptr<Directus::GameObject> gameObject) { m_gameObjectSelected = gameObject; }

private:	
	void Tree_Populate();
	void Tree_AddGameObject(const std::weak_ptr<Directus::GameObject>& gameObject);

	void OnTreeBegin();
	void OnTreeEnd();

	void ContextMenu();
	void HandleKeyShortcuts();

	void Action_GameObject_Delete(std::weak_ptr<Directus::GameObject> gameObject);
	std::weak_ptr<Directus::GameObject> Action_GameObject_CreateEmpty();
	void Action_GameObject_CreateCamera();
	void Action_GameObject_CreateLightDirectional();
	void Action_GameObject_CreateLightPoint();
	void Action_GameObject_CreateLightSpot();
	void Action_GameObject_CreateRigidBody();
	void Action_GameObject_CreateCollider();
	void Action_GameObject_CreateAudioSource();
	void Action_GameObject_CreateAudioListener();
	
	static std::weak_ptr<Directus::GameObject> m_gameObjectSelected;
};
