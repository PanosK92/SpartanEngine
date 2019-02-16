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

#pragma once

//= INCLUDES ======
#include "Widget.h"
#include <memory>
//=================

namespace Directus { class Entity; }

class Widget_World : public Widget
{
public:
	Widget_World(Directus::Context* context);
	void Tick(float deltaTime) override;

private:
	// Tree
	void Tree_Show();
	void OnTreeBegin();
	void OnTreeEnd();
	void Tree_AddEntity(Directus::Entity* entity);
	void HandleClicking();
	void Entity_HandleDragDrop(Directus::Entity* entityPtr);
	void SetSelectedEntity(std::shared_ptr<Directus::Entity> entity, bool fromEditor = true);

	// Misc
	void Popups();
	void Popup_ContextMenu();	
	void Popup_EntityRename();
	void HandleKeyShortcuts();

	// Context menu actions
	void Action_Entity_Delete(std::shared_ptr<Directus::Entity> entity);
	Directus::Entity* Action_Entity_CreateEmpty();
	void Action_Entity_CreateCube();
	void Action_Entity_CreateQuad();
	void Action_Entity_CreateSphere();
	void Action_Entity_CreateCylinder();
	void Action_Entity_CreateCone();
	void Action_Entity_CreateCamera();
	void Action_Entity_CreateLightDirectional();
	void Action_Entity_CreateLightPoint();
	void Action_Entity_CreateLightSpot();
	void Action_Entity_CreateRigidBody();
	void Action_Entity_CreateCollider();
	void Action_Entity_CreateConstraint();
	void Action_Entity_CreateAudioSource();
	void Action_Entity_CreateAudioListener();
	
	std::shared_ptr<Directus::Entity> m_entity_empty;
	bool m_expandToShowentity = false;
};
