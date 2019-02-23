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
	void Tick(float delta_time) override;

	static void Inspect(const std::weak_ptr<Directus::Entity>& entity);
	static void Inspect(std::weak_ptr<Directus::Material> material);

	// Inspected resources
	static std::weak_ptr<Directus::Entity> m_inspected_entity;
	static std::weak_ptr<Directus::Material> m_inspected_material;

private:
	void ShowTransform(std::shared_ptr<Directus::Transform>& transform) const;
	void ShowLight(std::shared_ptr<Directus::Light>& light) const;
	void ShowRenderable(std::shared_ptr<Directus::Renderable>& renderable) const;
	void ShowRigidBody(std::shared_ptr<Directus::RigidBody>& rigid_body) const;
	void ShowCollider(std::shared_ptr<Directus::Collider>& collider) const;
	void ShowConstraint(std::shared_ptr<Directus::Constraint>& constraint) const;
	void ShowMaterial(std::shared_ptr<Directus::Material>& material) const;
	void ShowCamera(std::shared_ptr<Directus::Camera>& camera) const;
	void ShowAudioSource(std::shared_ptr<Directus::AudioSource>& audio_source) const;
	void ShowAudioListener(std::shared_ptr<Directus::AudioListener>& audio_listener) const;
	void ShowScript(std::shared_ptr<Directus::Script>& script) const;

	void ShowAddComponentButton() const;
	void ComponentContextMenu_Add() const;
	void Drop_AutoAddComponents() const;

	// Color pickers
	std::unique_ptr<ButtonColorPicker> m_colorPicker_material;
	std::unique_ptr<ButtonColorPicker> m_colorPicker_light;
	std::unique_ptr<ButtonColorPicker> m_colorPicker_camera;
};
