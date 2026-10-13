/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==========================
#include "pch.h"
#include "PreviewRig.h"
#include "world/Entity.h"
#include "world/World.h"
#include "world/components/Camera.h"
#include "world/components/Light.h"
#include "world/components/Render.h"
#include <algorithm>
#include <cmath>
//=====================================

using namespace std;
using namespace spartan;

namespace preview_rig
{
    rig_ids create(Entity* root, const math::Vector3& center, const float radius, const string& prefix)
    {
        rig_ids ids;
        if (!root)
        {
            return ids;
        }

        Entity* rig_entity = World::CreateEntity();
        rig_entity->SetObjectName(prefix + "_rig");
        rig_entity->SetTransient(true);
        rig_entity->SetParent(root);
        ids.rig = rig_entity->GetObjectId();

        Entity* camera_entity = World::CreateEntity();
        camera_entity->SetObjectName(prefix + "_camera");
        camera_entity->SetTransient(true);
        camera_entity->SetParent(rig_entity);
        Camera* camera = camera_entity->AddComponent<Camera>();
        camera->SetFovHorizontalDeg(50.0f);
        camera->SetExposureMode(CameraExposureMode::automatic);
        ids.camera = camera_entity->GetObjectId();

        Entity* key_entity = World::CreateEntity();
        key_entity->SetObjectName(prefix + "_key_light");
        key_entity->SetTransient(true);
        key_entity->SetParent(rig_entity);
        key_entity->SetRotation(math::Quaternion::FromLookRotation(math::Vector3(-0.45f, -0.8f, 0.65f), math::Vector3::Up));
        Light* key_light = key_entity->AddComponent<Light>();
        key_light->SetLightType(LightType::Directional);
        key_light->SetIntensity(85000.0f);
        key_light->SetTemperature(5600.0f);

        Entity* fill_entity = World::CreateEntity();
        fill_entity->SetObjectName(prefix + "_fill_light");
        fill_entity->SetTransient(true);
        fill_entity->SetParent(rig_entity);
        fill_entity->SetPosition(center + math::Vector3(radius * 2.0f, radius * 1.2f, -radius * 1.5f));
        Light* fill_light = fill_entity->AddComponent<Light>();
        fill_light->SetLightType(LightType::Point);
        fill_light->SetIntensity(5000.0f);
        fill_light->SetRange(radius * 8.0f);
        fill_light->SetTemperature(7500.0f);
        // a fill only lifts the shadow side, casting would also split the preview atlas six more ways
        fill_light->SetFlag(LightFlags::Shadows, false);

        return ids;
    }

    bool compute_bounds(const vector<Entity*>& entities, math::Vector3& center, float& radius)
    {
        bool bounds_found = false;
        math::BoundingBox bounds;
        for (Entity* entity : entities)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (!render || !render->GetMesh())
            {
                continue;
            }

            const math::BoundingBox render_bounds = render->GetBoundingBoxMesh() * entity->GetMatrix();
            if (!bounds_found)
            {
                bounds       = render_bounds;
                bounds_found = true;
            }
            else
            {
                bounds.Merge(render_bounds);
            }
        }

        // half the diagonal is the enclosing sphere, the longest axis alone under estimates it and crops elongated assets
        center = bounds_found ? bounds.GetCenter() : math::Vector3::Zero;
        radius = bounds_found ? max(0.001f, bounds.GetSize().Length() * 0.5f) : 1.0f;
        return bounds_found;
    }

    void place_camera(Entity* camera_entity, const math::Vector3& center, const float radius, const float yaw, const float pitch, const float zoom, const float aspect)
    {
        if (!camera_entity)
        {
            return;
        }

        const math::Vector3 direction(cos(pitch) * sin(yaw), sin(pitch), -cos(pitch) * cos(yaw));

        Camera* camera             = camera_entity->GetComponent<Camera>();
        const float fov_horizontal = camera ? camera->GetFovHorizontalRad() : 50.0f * math::deg_to_rad;
        const float fov_vertical   = 2.0f * atan(tan(fov_horizontal * 0.5f) / max(0.05f, aspect));

        // fit the bounding sphere against the tighter frustum axis, otherwise a wide or tall panel crops the asset
        const float fov_fit = min(fov_horizontal, fov_vertical);
        float distance      = radius * 1.15f / max(0.01f, sin(fov_fit * 0.5f));
        distance           /= max(0.05f, zoom);

        // the near plane is fixed engine side, keep small assets in front of it
        const float near_plane = camera ? camera->GetNearPlane() : 0.1f;
        distance               = max(distance, near_plane + radius * 1.05f);

        const math::Vector3 position = center + direction * distance;
        camera_entity->SetPosition(position);
        camera_entity->SetRotation(math::Quaternion::FromLookRotation((center - position).Normalized(), math::Vector3::Up));

        // the near cascade is centered on the camera, sized to just reach past the subject so its texels land on it
        if (Entity* rig_entity = camera_entity->GetParent())
        {
            for (Entity* child : rig_entity->GetChildren())
            {
                Light* light = child->GetComponent<Light>();
                if (light && light->GetLightType() == LightType::Directional)
                {
                    light->SetCascadeNearExtent((distance + radius) * 1.1f);
                }
            }
        }
    }
}
