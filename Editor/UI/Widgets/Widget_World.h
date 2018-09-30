/*
Copyright(c) 2016-2018 Panos Karabelas

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

namespace Directus { class Actor; }

class Widget_World : public Widget
{
public:
	Widget_World();
	void Initialize(Directus::Context* context) override;
	void Tick(float deltaTime) override;

	static std::weak_ptr<Directus::Actor> GetActorSelected() { return m_actorSelected; }
	static void SetSelectedActor(std::weak_ptr<Directus::Actor> actor) ;

private:
	// Tree
	void Tree_Show();
	void OnTreeBegin();
	void OnTreeEnd();
	void Tree_AddActor(Directus::Actor* actor);
	void HandleClicking();
	void Actor_HandleDragDrop(Directus::Actor* actorPtr);

	// Misc
	void Popups();
	void Popup_ContextMenu();	
	void Popup_ActorRename();
	void HandleKeyShortcuts();

	// Context menu actions
	void Action_Actor_Delete(std::weak_ptr<Directus::Actor> actor);
	Directus::Actor* Action_Actor_CreateEmpty();
	void Action_Actor_CreateCube();
	void Action_Actor_CreateQuad();
	void Action_Actor_CreateSphere();
	void Action_Actor_CreateCylinder();
	void Action_Actor_CreateCone();
	void Action_actor_CreateCamera();
	void Action_actor_CreateLightDirectional();
	void Action_actor_CreateLightPoint();
	void Action_actor_CreateLightSpot();
	void Action_actor_CreateRigidBody();
	void Action_actor_CreateCollider();
	void Action_actor_CreateConstraint();
	void Action_actor_CreateAudioSource();
	void Action_actor_CreateAudioListener();
	
	static std::weak_ptr<Directus::Actor> m_actorSelected;
};
