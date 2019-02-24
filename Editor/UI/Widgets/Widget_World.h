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
	void Tick(float delta_time) override;

private:
	// Tree
	void TreeShow();
	void OnTreeBegin();
	void OnTreeEnd();
	void TreeAddEntity(Directus::Entity* entity);
	void HandleClicking();
	void EntityHandleDragDrop(Directus::Entity* entity_ptr) const;
	void SetSelectedEntity(const std::shared_ptr<Directus::Entity>& entity, bool from_editor = true);

	// Misc
	void Popups();
	void PopupContextMenu();	
	void PopupEntityRename() const;
	static void HandleKeyShortcuts();

	// Context menu actions
	static void ActionEntityDelete(const std::shared_ptr<Directus::Entity>& entity);
	static Directus::Entity* ActionEntityCreateEmpty();
	static void ActionEntityCreateCube();
	static void ActionEntityCreateQuad();
	static void ActionEntityCreateSphere();
	static void ActionEntityCreateCylinder();
	static void ActionEntityCreateCone();
	static void ActionEntityCreateCamera();
	static void ActionEntityCreateLightDirectional();
	static void ActionEntityCreateLightPoint();
	static void ActionEntityCreateLightSpot();
	static void ActionEntityCreateRigidBody();
	static void ActionEntityCreateCollider();
	static void ActionEntityCreateConstraint();
	static void ActionEntityCreateAudioSource();
	static void ActionEntityCreateAudioListener();
	static void ActionEntityCreateSkybox();
	
	std::shared_ptr<Directus::Entity> m_entity_empty;
	bool m_expand_to_showentity = false;
};
