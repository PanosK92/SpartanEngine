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

namespace Directus
{
	class Entity;
	class Transform;
	class Light;
	class Renderable;
	class RigidBody;
	class Collider;
	class Constraint;
	class Material;
	class Camera;
	class AudioSource;
	class AudioListener;
	class Script;
	class IComponent;
}

class ButtonColorPicker;

class Widget_Properties : public Widget
{
public:
	Widget_Properties(Directus::Context* context);
	void Tick(float deltaTime) override;

	static void Inspect(std::weak_ptr<Directus::Entity> entity);
	static void Inspect(std::weak_ptr<Directus::Material> material);

	// Inspected resources
	static std::weak_ptr<Directus::Entity> m_inspectedentity;
	static std::weak_ptr<Directus::Material> m_inspectedMaterial;

private:
	void ShowTransform(std::shared_ptr<Directus::Transform>& transform);
	void ShowLight(std::shared_ptr<Directus::Light>& light);
	void ShowRenderable(std::shared_ptr<Directus::Renderable>& renderable);
	void ShowRigidBody(std::shared_ptr<Directus::RigidBody>& rigidBody);
	void ShowCollider(std::shared_ptr<Directus::Collider>& collider);
	void ShowConstraint(std::shared_ptr<Directus::Constraint>& constraint);
	void ShowMaterial(std::shared_ptr<Directus::Material>& material);
	void ShowCamera(std::shared_ptr<Directus::Camera>& camera);
	void ShowAudioSource(std::shared_ptr<Directus::AudioSource>& audioSource);
	void ShowAudioListener(std::shared_ptr<Directus::AudioListener>& audioListener);
	void ShowScript(std::shared_ptr<Directus::Script>& script);

	void ShowAddComponentButton();
	void ComponentContextMenu_Add();
	void Drop_AutoAddComponents();

	// Color pickers
	std::unique_ptr<ButtonColorPicker> m_colorPicker_material;
	std::unique_ptr<ButtonColorPicker> m_colorPicker_light;
	std::unique_ptr<ButtonColorPicker> m_colorPicker_camera;
};
