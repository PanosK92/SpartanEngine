/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
//=================

namespace spartan
{
    class Entity;
    class Light;
    class Render;
    class Physics;
    class Material;
    class Camera;
    class AudioSource;
    class Terrain;
    class Spline;
    class SplineFollower;
    class Pedestrians;
    class Navigation;
    class Volume;
    class Script;
    class Component;
    class ParticleSystem;
    class Water;
    class SpawnPoint;
    class CarReset;
    class Text3D;
}

class Properties : public Widget
{
public:
    Properties(Editor* editor);

    void OnTickVisible() override;

    static void InspectMaterial(const std::shared_ptr<spartan::Material> material);
    static void ClearMaterialInspection();

private:
    void ShowEntity(spartan::Entity* entity) const;
    void ShowScript(spartan::Script* script) const;
    void ShowLight(spartan::Light* light) const;
    void ShowRender(spartan::Render* render) const;
    void ShowPhysics(spartan::Physics* rigid_body) const;
    void ShowMaterial(spartan::Material* material, spartan::Render* render = nullptr) const;
    void ShowCamera(spartan::Camera* camera) const;
    void ShowTerrain(spartan::Terrain* terrain) const;
    void ShowSpline(spartan::Spline* spline) const;
    void ShowSplineFollower(spartan::SplineFollower* follower) const;
    void ShowPedestrians(spartan::Pedestrians* pedestrians) const;
    void ShowNavigation(spartan::Navigation* navigation) const;
    void ShowAudioSource(spartan::AudioSource* audio_source) const;
    void ShowVolume(spartan::Volume* volume) const;
    void ShowParticleSystem(spartan::ParticleSystem* particle_system) const;
    void ShowWater(spartan::Water* water) const;
    void ShowSpawnPoint(spartan::SpawnPoint* spawn_point) const;
    void ShowCarReset(spartan::CarReset* car_reset) const;
    void ShowText3D(spartan::Text3D* text_3d) const;

    void ShowAddComponentButton() const;
    void ComponentContextMenu_Add() const;
    void ShowSaveAsPrefabPopup(spartan::Entity* entity) const;
};
