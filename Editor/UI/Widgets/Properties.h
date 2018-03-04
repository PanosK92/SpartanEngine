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

namespace Directus
{
	class GameObject;
	class Transform;
	class Light;
	class MeshFilter;
	class MeshRenderer;
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

class Properties : public Widget
{
public:
	Properties();
	void Initialize(Directus::Context* context) override;
	void Update() override;
	static void Inspect(std::weak_ptr<Directus::GameObject> gameObject);

private:
	void ShowTransform(Directus::Transform* transform);
	void ShowLight(Directus::Light* light);
	void ShowMeshFilter(Directus::MeshFilter* meshFilter);
	void ShowMeshRenderer(Directus::MeshRenderer* meshRenderer);
	void ShowRigidBody(Directus::RigidBody* rigidBody);
	void ShowCollider(Directus::Collider* collider);
	void ShowConstraint(Directus::Constraint* collider);
	void ShowMaterial(Directus::Material* material);
	void ShowCamera(Directus::Camera* camera);
	void ShowAudioSource(Directus::AudioSource* audioSource);
	void ShowAudioListener(Directus::AudioListener* audioListener);
	void ShowScript(Directus::Script* script);

	void ComponentContextMenu_Options(const char* id, Directus::IComponent* component);
	void ShowAddComponentButton();
	void ComponentContextMenu_Add();

	static std::weak_ptr<Directus::GameObject> m_gameObject;
};
