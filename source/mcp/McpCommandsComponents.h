/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "McpCommandsCommon.h"
#include "world/components/Component.h"
#include "world/components/Light.h"
#include "world/components/Physics.h"
#include "geometry/Mesh.h"
#include "resource/IResource.h"
#include "rendering/Color.h"
namespace spartan::mcp_components
{
    void Register();
    bool assign_render_material(Render* render, const std::string& name_or_path, std::string& error);
    std::optional<BodyType> body_type_from_name(const std::string& name);
    std::string body_type_to_name(BodyType type);
    std::optional<ComponentType> component_type_from_name(const std::string& name);
    std::shared_ptr<IResource> get_resource_shared_by_name_or_path(const std::string& name_or_path, ResourceType type);
    std::string json_quaternion(const math::Quaternion& value);
    std::optional<LightType> light_type_from_name(const std::string& name);
    std::string light_type_to_name(LightType type);
    std::optional<MeshType> mesh_type_from_name(const std::string& name);
    bool parse_color(const std::string& value, Color& result);
    bool parse_quaternion(const std::string& value, math::Quaternion& result);
    bool set_light_property(Light* light, const std::string& property, const std::string& value, std::string& error);
}
