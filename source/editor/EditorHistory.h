/*
Copyright(c) 2015-2026 Panos Karabelas

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

// Shared transactions for editor actions. Only the inspected entity is captured, never the world.
#pragma once
#include "commands/CommandStack.h"
#include "core/Engine.h"
#include "commands/CommandEntityDelete.h"
#include "world/Entity.h"
#include "world/World.h"
#include "world/Environment.h"
#include "world/components/Terrain.h"
#include "io/pugixml.hpp"
#include "imgui/source/imgui.h"
#include "imgui/source/imgui_internal.h"
#include "rendering/Material.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Texture.h"
#include <sstream>
#include <functional>
#include <map>

namespace editor_history
{
    using namespace spartan;
    inline std::weak_ptr<std::atomic<bool>> terrain_jobs;
    class Edit final : public Command
    {
    public:
        std::function<void()> undo, redo;
        std::function<bool()> ready;
        bool CanExecute() const override { return !ready || ready(); }
        void OnApply() override { redo(); }
        void OnRevert() override { undo(); }
    };

    inline void Record(const std::string& key, std::function<void()> undo, std::function<void()> redo,
        std::function<bool()> ready = {}, bool merge = true)
    {
        static std::weak_ptr<Edit> current;
        static std::string current_key;
        static uint64_t revision = 0;
        static ImGuiID active = 0;
        static double activation_time = 0;
        const ImGuiID item = ImGui::GetActiveID();
        const double activated = ImGui::GetTime() - ImGui::GetCurrentContext()->ActiveIdTimer;
        auto command = current.lock();
        if (!merge || !command || current_key != key || revision != CommandStack::Revision() || !item || item != active || std::abs(activated - activation_time) > 0.001)
        {
            command = std::make_shared<Edit>();
            command->undo = std::move(undo);
            command->ready = std::move(ready);
            CommandStack::Push(command);
        }
        command->redo = std::move(redo);
        current = command;
        current_key = key;
        revision = CommandStack::Revision();
        active = item;
        activation_time = activated;
    }

    inline std::string Xml(pugi::xml_node node)
    {
        std::ostringstream stream;
        node.print(stream, "", pugi::format_raw);
        return stream.str();
    }

    inline std::string Capture(Entity* entity, bool components)
    {
        pugi::xml_document doc;
        auto root = doc.append_child("edit");
        root.append_attribute("name") = entity->GetObjectName().c_str();
        root.append_attribute("tags") = entity->GetTagsString().c_str();
        auto prefab = root.append_child("prefab");
        prefab.append_attribute("type") = entity->GetPrefabType().c_str();
        prefab.append_attribute("file") = entity->GetPrefabFilePath().c_str();
        std::map<std::string, std::string> attributes(entity->GetPrefabAttributes().begin(), entity->GetPrefabAttributes().end());
        for (const auto& [key, value] : attributes)
        {
            auto attribute = prefab.append_child("attribute");
            attribute.append_attribute("key") = key.c_str(); attribute.append_attribute("value") = value.c_str();
        }
        root.append_attribute("active") = entity->IsActive();
        root.append_attribute("parent") = entity->GetParent() ? entity->GetParent()->GetObjectId() : uint64_t(0);
        auto write = [&](const char* name, auto value) {
            auto node = root.append_child(name);
            node.append_attribute("x") = value.x; node.append_attribute("y") = value.y; node.append_attribute("z") = value.z;
        };
        write("position", entity->GetPositionLocal());
        write("scale", entity->GetScaleLocal());
        write("rotation", entity->GetRotationLocal());
        root.child("rotation").append_attribute("w") = entity->GetRotationLocal().w;
        if (!components)
        {
            const auto siblings = entity->GetParent() ? entity->GetParent()->GetChildren() : World::GetEntities();
            const auto it = std::find(siblings.begin(), siblings.end(), entity);
            root.append_attribute("order") = static_cast<uint64_t>(std::distance(siblings.begin(), it));
        }
        if (components)
        {
            auto list = root.append_child("components");
            for (auto& component : entity->GetAllComponents())
            {
                if (!component) continue;
                auto node = list.append_child(Component::TypeToString(component->GetType()).c_str());
                component->Save(node);
            }
        }
        if (components && entity->GetComponentByType(ComponentType::Light))
        {
            auto environment = root.append_child("environment");
            const auto settings = Environment::GetSettings();
            environment.append_attribute("utc_days") = settings.utc_days;
            environment.append_attribute("latitude") = settings.latitude;
            environment.append_attribute("longitude") = settings.longitude;
            environment.append_attribute("elevation") = settings.elevation;
            environment.append_attribute("time_scale") = settings.time_scale;
            environment.append_attribute("north_degrees") = settings.north_degrees;
            environment.append_attribute("annual_temperature") = settings.annual_temperature;
            environment.append_attribute("seasonal_amplitude") = settings.seasonal_amplitude;
            environment.append_attribute("daily_amplitude") = settings.daily_amplitude;
            environment.append_attribute("sea_level_pressure") = settings.sea_level_pressure;
            write("wind", World::GetWind());
        }
        return Xml(root);
    }

    inline bool Ready(uint64_t id)
    {
        auto entity = World::GetEntityById(id);
        auto terrain = entity ? entity->GetComponent<Terrain>() : nullptr;
        const auto jobs = terrain_jobs.lock();
        return !terrain || (!terrain->IsGenerating() && (!jobs || !jobs->load()));
    }

    inline void Restore(uint64_t id, const std::string& data, const std::string& other, std::shared_ptr<const TerrainSculptLayer> sculpt = {})
    {
        auto entity = World::GetEntityById(id);
        if (!entity) return;
        pugi::xml_document doc, previous;
        if (!doc.load_string(data.c_str()) || !previous.load_string(other.c_str())) return;
        auto root = doc.child("edit");
        auto old = previous.child("edit");
        if (Xml(root.child("prefab")) != Xml(old.child("prefab")))
        {
            auto prefab = root.child("prefab");
            std::unordered_map<std::string, std::string> attributes;
            for (auto attribute : prefab.children("attribute")) attributes[attribute.attribute("key").value()] = attribute.attribute("value").value();
            entity->SetPrefabData(prefab.attribute("type").value(), attributes);
            entity->SetPrefabFilePath(prefab.attribute("file").value());
        }
        if (std::string(root.attribute("name").value()) != old.attribute("name").value()) entity->SetObjectName(root.attribute("name").value());
        if (std::string(root.attribute("tags").value()) != old.attribute("tags").value()) entity->SetTagsString(root.attribute("tags").value());
        if (root.attribute("active").as_bool() != old.attribute("active").as_bool()) entity->SetActive(root.attribute("active").as_bool());
        if (root.attribute("parent").as_ullong() != old.attribute("parent").as_ullong()) entity->SetParent(World::GetEntityById(root.attribute("parent").as_ullong()));
        if (root.attribute("order") && root.attribute("order").as_ullong() != old.attribute("order").as_ullong())
        {
            const uint32_t index = root.attribute("order").as_uint();
            if (auto parent = entity->GetParent()) parent->MoveChildToIndex(entity, index);
            else World::MoveEntityToIndex(entity, index);
        }
        auto vector = [](pugi::xml_node n) { return math::Vector3(n.attribute("x").as_float(), n.attribute("y").as_float(), n.attribute("z").as_float()); };
        if (Xml(root.child("position")) != Xml(old.child("position"))) entity->SetPositionLocal(vector(root.child("position")));
        if (Xml(root.child("scale")) != Xml(old.child("scale"))) entity->SetScaleLocal(vector(root.child("scale")));
        if (Xml(root.child("rotation")) != Xml(old.child("rotation")))
        {
            auto n = root.child("rotation");
            entity->SetRotationLocal(math::Quaternion(n.attribute("x").as_float(), n.attribute("y").as_float(), n.attribute("z").as_float(), n.attribute("w").as_float()));
        }
        if (root.child("environment") && Xml(root.child("environment")) != Xml(old.child("environment")))
        {
            auto settings = Environment::GetSettings();
            auto environment = root.child("environment");
            if (std::string(environment.attribute("utc_days").value()) != old.child("environment").attribute("utc_days").value())
                settings.utc_days = environment.attribute("utc_days").as_double();
            if (std::string(environment.attribute("latitude").value()) != old.child("environment").attribute("latitude").value())
                settings.latitude = environment.attribute("latitude").as_double();
            if (std::string(environment.attribute("longitude").value()) != old.child("environment").attribute("longitude").value())
                settings.longitude = environment.attribute("longitude").as_double();
            if (std::string(environment.attribute("elevation").value()) != old.child("environment").attribute("elevation").value())
                settings.elevation = environment.attribute("elevation").as_double();
            if (std::string(environment.attribute("time_scale").value()) != old.child("environment").attribute("time_scale").value())
                settings.time_scale = environment.attribute("time_scale").as_double();
            if (std::string(environment.attribute("north_degrees").value()) != old.child("environment").attribute("north_degrees").value())
                settings.north_degrees = environment.attribute("north_degrees").as_float();
            if (std::string(environment.attribute("annual_temperature").value()) != old.child("environment").attribute("annual_temperature").value())
                settings.annual_temperature = environment.attribute("annual_temperature").as_float();
            if (std::string(environment.attribute("seasonal_amplitude").value()) != old.child("environment").attribute("seasonal_amplitude").value())
                settings.seasonal_amplitude = environment.attribute("seasonal_amplitude").as_float();
            if (std::string(environment.attribute("daily_amplitude").value()) != old.child("environment").attribute("daily_amplitude").value())
                settings.daily_amplitude = environment.attribute("daily_amplitude").as_float();
            if (std::string(environment.attribute("sea_level_pressure").value()) != old.child("environment").attribute("sea_level_pressure").value())
                settings.sea_level_pressure = environment.attribute("sea_level_pressure").as_float();
            Environment::SetSettings(settings);
        }
        if (root.child("wind") && Xml(root.child("wind")) != Xml(old.child("wind"))) World::SetWind(vector(root.child("wind")));
        auto list = root.child("components");
        auto old_list = old.child("components");
        for (auto n : old_list.children())
            if (!list.child(n.name())) entity->RemoveComponentByType(Component::StringToType(n.name()));
        for (auto n : list.children())
        {
            if (Xml(n) == Xml(old_list.child(n.name()))) continue;
            auto type = Component::StringToType(n.name());
            auto component = entity->GetComponentByType(type);
            if (!component) component = entity->AddComponent(type);
            if (type == ComponentType::Terrain) static_cast<Terrain*>(component)->LoadEditorState(n, sculpt.get());
            else if (component) component->Load(n);
        }
    }

    class EntityScope
    {
        uint64_t id;
        bool components;
        bool enabled;
        std::string before;
        std::shared_ptr<const TerrainSculptLayer> sculpt_before;
    public:
        explicit EntityScope(Entity* entity, bool include_components = false) :
            id(entity->GetObjectId()), components(include_components), enabled(!Engine::IsFlagSet(EngineMode::Playing) && Ready(entity->GetObjectId())),
            before(enabled ? Capture(entity, include_components) : std::string())
        {
            if (enabled && components)
                if (auto terrain = entity->GetComponent<Terrain>()) sculpt_before = terrain->GetSculptSnapshot();
        }
        ~EntityScope()
        {
            if (!enabled) return;
            auto entity = World::GetEntityById(id);
            if (!entity) return;
            auto after = Capture(entity, components);
            if (before == after) return;
            auto terrain = components ? entity->GetComponent<Terrain>() : nullptr;
            auto sculpt_after = terrain ? (Ready(id) ? terrain->GetSculptSnapshot() : sculpt_before) : nullptr;
            Record("entity:" + std::to_string(id),
                [id = id, before = before, after, sculpt = sculpt_before] { Restore(id, before, after, sculpt); },
                [id = id, before = before, after, sculpt_after] { Restore(id, after, before, sculpt_after); },
                [id = id] { return Ready(id); });
        }
    };

    // The deletion snapshot is captured at undo time, after creation has finished configuring the entity.
    inline void Created(const std::vector<Entity*>& entities)
    {
        std::vector<uint64_t> ids;
        for (auto entity : entities) if (entity) ids.push_back(entity->GetObjectId());
        if (ids.empty()) return;
        auto snapshots = std::make_shared<std::vector<std::shared_ptr<CommandEntityDelete>>>();
        Record("create", [ids, snapshots] {
            snapshots->clear();
            for (auto id : ids)
                if (auto entity = World::GetEntityById(id))
                    snapshots->push_back(std::make_shared<CommandEntityDelete>(entity));
            for (auto snapshot : *snapshots) snapshot->OnApply();
        }, [snapshots] { for (auto snapshot : *snapshots) snapshot->OnRevert(); }, {}, false);
    }

    inline void Created(Entity* entity) { Created(std::vector<Entity*>{entity}); }

    inline void Deleted(const std::vector<Entity*>& selected)
    {
        std::vector<std::shared_ptr<CommandEntityDelete>> commands;
        for (auto entity : selected)
        {
            if (!entity || !World::EntityExists(entity)) continue;
            bool child = false;
            for (auto parent = entity->GetParent(); parent; parent = parent->GetParent())
                if (std::find(selected.begin(), selected.end(), parent) != selected.end()) { child = true; break; }
            if (!child) commands.push_back(std::make_shared<CommandEntityDelete>(entity));
        }
        if (commands.empty()) return;
        for (auto command : commands) command->OnApply();
        Record("delete", [commands] { for (auto i = commands.rbegin(); i != commands.rend(); ++i) (*i)->OnRevert(); },
            [commands] { for (auto command : commands) command->OnApply(); }, {}, false);
    }

    struct MaterialState
    {
        std::array<float, static_cast<uint32_t>(MaterialProperty::Max)> properties;
        std::array<RHI_Texture*, static_cast<uint32_t>(MaterialTextureType::Max) * Material::slots_per_texture> textures;
        std::vector<std::shared_ptr<IResource>> resources;
        explicit MaterialState(Material* material) : properties(material->GetProperties()), textures(material->GetTextures())
        {
            for (auto texture : textures) if (texture) resources.push_back(texture->shared_from_this());
        }
        bool operator==(const MaterialState& other) const { return properties == other.properties && textures == other.textures; }
        void Restore(Material* material) const
        {
            for (uint32_t i = 0; i < textures.size(); ++i)
                if (textures[i] != material->GetTextures()[i])
                    material->SetTexture(static_cast<MaterialTextureType>(i / Material::slots_per_texture), textures[i], i % Material::slots_per_texture, false);
            for (uint32_t i = 0; i < properties.size(); ++i)
                material->SetProperty(static_cast<MaterialProperty>(i), properties[i]);
        }
    };

    class MaterialScope
    {
        std::shared_ptr<Material> material;
        MaterialState before;
    public:
        explicit MaterialScope(Material* value) : material(std::static_pointer_cast<Material>(value->shared_from_this())), before(value) {}
        ~MaterialScope()
        {
            if (Engine::IsFlagSet(EngineMode::Playing)) return;
            MaterialState after(material.get());
            if (before == after) return;
            // Resource cache and history are both cleared on world shutdown.
            Record("material:" + std::to_string(material->GetObjectId()),
                [material = material, before = before] { before.Restore(material.get()); },
                [material = material, after] { after.Restore(material.get()); });
        }
    };

    inline void Sculpt(Terrain* terrain, TerrainSculptLayer before)
    {
        const uint64_t id = terrain->GetEntity()->GetObjectId();
        auto restore = [id](const TerrainSculptLayer& layer) {
            if (auto entity = World::GetEntityById(id))
                if (auto terrain = entity->GetComponent<Terrain>()) terrain->RestoreSculptLayer(layer);
        };
        Record("sculpt:" + std::to_string(id), [restore, before] { restore(before); },
            [restore, after = terrain->GetSculptLayer()] { restore(after); }, [id] { return Ready(id); }, false);
    }
}
