/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==========================
#include "pch.h"
#include "EditorMcpCommands.h"
#include "Editor.h"
#include "widgets/TreeCreator.h"
#include "file_system/FileSystem.h"
#include "world/World.h"
//=====================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace editor_mcp
{
    namespace
    {
        const char* class_name(const trees::tree_class type)
        {
            switch (type)
            {
                case trees::tree_class::shrub:   return "shrub";
                case trees::tree_class::conifer: return "conifer";
                case trees::tree_class::palm:    return "palm";
                case trees::tree_class::hero:    return "hero";
                default:                         return "broadleaf";
            }
        }

        string number(const float value)
        {
            char text[32];
            snprintf(text, sizeof(text), "%.3f", value);
            return text;
        }

        string stats_json(const TreeCreator::stats& stats)
        {
            string lods = "[";
            for (size_t i = 0; i < stats.lod_triangles.size(); i++)
            {
                lods += (i ? "," : "") + to_string(stats.lod_triangles[i]);
            }
            lods += "]";
            return
                "{\"triangles\":" + to_string(stats.triangles) +
                ",\"vertices\":" + to_string(stats.vertices) +
                ",\"bark_triangles\":" + to_string(stats.bark_triangles) +
                ",\"foliage_triangles\":" + to_string(stats.foliage_triangles) +
                ",\"budget\":" + to_string(stats.budget) +
                ",\"stems\":" + to_string(stats.stems) +
                ",\"cards\":" + to_string(stats.cards) +
                ",\"lod_triangles\":" + lods +
                ",\"height\":" + number(stats.height) +
                ",\"crown_width\":" + number(stats.crown_width) +
                ",\"generate_ms\":" + number(stats.generate_ms) +
                "}";
        }

        string controls_json(const trees::controls& c)
        {
            return
                "{\"age\":" + number(c.age) +
                ",\"leafiness\":" + number(c.leafiness) +
                ",\"branchiness\":" + number(c.branchiness) +
                ",\"wildness\":" + number(c.wildness) +
                ",\"season\":" + quote(trees::to_string(c.time)) +
                ",\"seed\":" + to_string(c.seed) +
                ",\"quality\":" + quote(trees::to_string(c.detail)) +
                "}";
        }

        string state_json(TreeCreator* creator)
        {
            const trees::species* selected = creator->GetSelectedSpecies();
            return
                "{\"ok\":true,\"visible\":" + string(boolean(creator->GetVisible())) +
                ",\"species\":" + quote(selected ? selected->id : "") +
                ",\"name\":" + quote(selected ? selected->name : "") +
                ",\"controls\":" + controls_json(creator->GetControls()) +
                ",\"stats\":" + stats_json(creator->GetStats()) +
                ",\"preview\":{\"lod\":" + to_string(creator->GetPreviewLod()) +
                ",\"yaw\":" + number(creator->GetPreviewYaw() * math::rad_to_deg) +
                ",\"pitch\":" + number(creator->GetPreviewPitch() * math::rad_to_deg) +
                ",\"zoom\":" + number(creator->GetPreviewZoom()) +
                "}}";
        }

        // every control is optional, what is not named keeps its current value
        string apply_controls(const McpRequest& request, trees::controls& controls)
        {
            const char* fraction = "age, leafiness, branchiness and wildness are numbers from 0 to 1";
            struct slider
            {
                const char* name;
                float* value;
            };
            const slider sliders[] =
            {
                { "age",         &controls.age },
                { "leafiness",   &controls.leafiness },
                { "branchiness", &controls.branchiness },
                { "wildness",    &controls.wildness }
            };
            for (const slider& entry : sliders)
            {
                if (const string* value = find(request, entry.name))
                {
                    const optional<float> parsed = as_float(value);
                    if (!parsed)
                    {
                        return fraction;
                    }
                    *entry.value = clamp(*parsed, 0.0f, 1.0f);
                }
            }
            if (const string* value = find(request, "season"))
            {
                string text = to_lower(*value);
                if (text == "winter")
                {
                    text = "bare";
                }
                if (!trees::season_from_string(text, controls.time))
                {
                    return "season must be spring, summer, autumn or winter";
                }
            }
            if (const string* value = find(request, "seed"))
            {
                const optional<uint64_t> parsed = as_uint(value);
                if (!parsed)
                {
                    return "seed must be a positive integer";
                }
                controls.seed = static_cast<uint32_t>(*parsed);
            }
            if (const string* value = find(request, "quality"))
            {
                if (!trees::quality_from_string(to_lower(*value), controls.detail))
                {
                    return "quality must be mobile, standard or hero";
                }
            }
            return "";
        }

        string apply_view(const McpRequest& request, TreeCreator* creator)
        {
            // the bridge speaks degrees, the widget radians
            float yaw   = creator->GetPreviewYaw() * math::rad_to_deg;
            float pitch = creator->GetPreviewPitch() * math::rad_to_deg;
            float zoom  = creator->GetPreviewZoom();
            bool view   = false;
            for (const pair<const char*, float*>& entry : { pair<const char*, float*>{ "yaw", &yaw }, { "pitch", &pitch }, { "zoom", &zoom } })
            {
                if (const string* value = find(request, entry.first))
                {
                    const optional<float> parsed = as_float(value);
                    if (!parsed)
                    {
                        return "yaw, pitch and zoom must be numbers";
                    }
                    *entry.second = *parsed;
                    view          = true;
                }
            }
            if (view)
            {
                creator->SetPreviewView(yaw * math::deg_to_rad, pitch * math::deg_to_rad, zoom);
            }
            if (const string* value = find(request, "lod"))
            {
                const optional<uint64_t> parsed = as_uint(value);
                if (!parsed)
                {
                    return "lod must be a level index";
                }
                creator->SetPreviewLod(static_cast<int>(*parsed));
            }
            return "";
        }

        TreeCreator* open(Editor* editor)
        {
            TreeCreator* creator = editor->GetWidget<TreeCreator>();
            if (creator && creator->GetSpecies().empty())
            {
                creator->ReloadSpecies();
            }
            return creator;
        }
    }

    void register_tree_creator(Editor* editor)
    {
        add(
            "tree_creator_open",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                creator->SetPanelVisible(as_bool(find(request, "visible")).value_or(true));
                return state_json(creator);
            }
        );

        add(
            "tree_species_list",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                string errors;
                if (as_bool(find(request, "reload")).value_or(false))
                {
                    errors = creator->ReloadSpecies();
                }

                const string* region_filter = find(request, "region");
                const string region         = region_filter ? to_lower(*region_filter) : "";
                vector<string> regions;
                string species = "[";
                bool first     = true;
                for (const trees::species& recipe : creator->GetSpecies())
                {
                    if (find(regions.begin(), regions.end(), recipe.region) == regions.end())
                    {
                        regions.push_back(recipe.region);
                    }
                    if (!region.empty() && to_lower(recipe.region) != region)
                    {
                        continue;
                    }
                    species += (first ? "" : ",");
                    species +=
                        "{\"id\":" + quote(recipe.id) +
                        ",\"name\":" + quote(recipe.name) +
                        ",\"scientific\":" + quote(recipe.scientific) +
                        ",\"region\":" + quote(recipe.region) +
                        ",\"class\":" + quote(class_name(recipe.type)) +
                        ",\"evergreen\":" + boolean(recipe.evergreen) +
                        ",\"height\":" + number(recipe.height) +
                        ",\"budget\":" + to_string(recipe.budget) +
                        ",\"file\":" + quote(recipe.file_path) +
                        ",\"description\":" + quote(recipe.description) +
                        "}";
                    first = false;
                }
                species += "]";

                string region_list = "[";
                for (size_t i = 0; i < regions.size(); i++)
                {
                    region_list += (i ? "," : "") + quote(regions[i]);
                }
                region_list += "]";

                const trees::species* selected = creator->GetSelectedSpecies();
                return
                    "{\"ok\":true,\"directory\":" + quote(trees::species_directory()) +
                    ",\"regions\":" + region_list +
                    ",\"species\":" + species +
                    ",\"selected\":" + quote(selected ? selected->id : "") +
                    ",\"errors\":" + quote(errors) +
                    "}";
            }
        );

        add(
            "tree_generate",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                if (const string* id = find_any(request, { "species", "species_id", "id" }))
                {
                    if (!creator->SelectSpecies(*id))
                    {
                        return failure("no species with id " + *id + ", see tree_species_list");
                    }
                }

                trees::controls controls = creator->GetControls();
                if (as_bool(find(request, "reset")).value_or(false))
                {
                    controls = trees::controls();
                }
                const string control_error = apply_controls(request, controls);
                if (!control_error.empty())
                {
                    return failure(control_error);
                }
                creator->SetControls(controls);

                string error;
                if (!creator->GenerateNow(error))
                {
                    return failure(error);
                }
                const string view_error = apply_view(request, creator);
                if (!view_error.empty())
                {
                    return failure(view_error);
                }
                return state_json(creator);
            }
        );

        add(
            "tree_preview_capture",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                const string view_error = apply_view(request, creator);
                if (!view_error.empty())
                {
                    return failure(view_error);
                }

                const string* requested = find_any(request, { "path", "filename", "file", "name" });
                string name = requested ? FileSystem::GetFileNameFromFilePath(*requested) : "tree_creator.png";
                if (FileSystem::GetExtensionFromFilePath(name) != ".png")
                {
                    name += ".png";
                }
                const string path = World::GetLibraryResourceDirectory() + "thumbnails/" + name;

                uint32_t width  = 1024;
                uint32_t height = 1024;
                if (const optional<uint64_t> value = as_uint(find(request, "width")))
                {
                    width = static_cast<uint32_t>(*value);
                }
                if (const optional<uint64_t> value = as_uint(find(request, "height")))
                {
                    height = static_cast<uint32_t>(*value);
                }

                string error;
                if (!creator->CapturePreview(path, width, height, error))
                {
                    return failure(error);
                }
                return "{\"ok\":true,\"ready\":false,\"async\":true,\"path\":" + quote(path) + ",\"width\":" + to_string(width) + ",\"height\":" + to_string(height) + "}";
            }
        );

        add(
            "tree_export",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                if (const string* id = find_any(request, { "species", "species_id", "id" }))
                {
                    if (!creator->SelectSpecies(*id))
                    {
                        return failure("no species with id " + *id + ", see tree_species_list");
                    }
                }
                trees::controls controls = creator->GetControls();
                const string control_error = apply_controls(request, controls);
                if (!control_error.empty())
                {
                    return failure(control_error);
                }
                creator->SetControls(controls);

                const string* name        = find(request, "name");
                const uint32_t variations = static_cast<uint32_t>(as_uint(find(request, "variations")).value_or(1));
                vector<TreeCreator::export_result> results;
                string error;
                if (!creator->Export(name ? *name : "", variations, results, error))
                {
                    return failure(error);
                }

                string exports = "[";
                for (size_t i = 0; i < results.size(); i++)
                {
                    const TreeCreator::export_result& result = results[i];
                    exports += (i ? "," : "");
                    exports +=
                        "{\"prefab\":" + quote(result.prefab) +
                        ",\"mesh\":" + quote(result.mesh) +
                        ",\"directory\":" + quote(result.directory) +
                        ",\"seed\":" + to_string(result.seed) +
                        ",\"impostor\":" + boolean(result.impostor) +
                        ",\"stats\":" + stats_json(result.numbers) +
                        "}";
                }
                exports += "]";
                return "{\"ok\":true,\"exports\":" + exports + "}";
            }
        );

        add(
            "tree_species_save",
            [editor](const McpRequest& request) -> string
            {
                TreeCreator* creator = open(editor);
                if (!creator)
                {
                    return failure("the tree creator is not available");
                }
                const string* json = find_any(request, { "json", "species" });
                if (!json || json->empty())
                {
                    return failure("pass the species as json, the schema is the one in data/trees/species");
                }

                string path;
                string error;
                if (!creator->SaveSpecies(*json, path, error))
                {
                    return failure(error);
                }
                return "{\"ok\":true,\"path\":" + quote(path) + ",\"state\":" + state_json(creator) + "}";
            }
        );
    }
}
