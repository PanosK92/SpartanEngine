/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "TreeSpecies.h"
#include "../mcp/McpJson.h"
#include "../file_system/FileSystem.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
//================================

using namespace std;
using namespace spartan::math;

namespace spartan::trees
{
    namespace
    {
        template<typename T>
        struct enum_name
        {
            T value;
            const char* name;
        };

        const enum_name<crown_shape> crown_shape_names[] =
        {
            { crown_shape::conical,             "conical" },
            { crown_shape::spherical,           "spherical" },
            { crown_shape::hemispherical,       "hemispherical" },
            { crown_shape::cylindrical,         "cylindrical" },
            { crown_shape::tapered_cylindrical, "tapered_cylindrical" },
            { crown_shape::flame,               "flame" },
            { crown_shape::inverse_conical,     "inverse_conical" },
            { crown_shape::tend_flame,          "tend_flame" },
            { crown_shape::umbrella,            "umbrella" },
            { crown_shape::vase,                "vase" }
        };

        const enum_name<leaf_shape> leaf_shape_names[] =
        {
            { leaf_shape::elliptic,   "elliptic" },
            { leaf_shape::lanceolate, "lanceolate" },
            { leaf_shape::ovate,      "ovate" },
            { leaf_shape::round,      "round" },
            { leaf_shape::heart,      "heart" },
            { leaf_shape::palmate,    "palmate" },
            { leaf_shape::lobed,      "lobed" },
            { leaf_shape::needle,     "needle" },
            { leaf_shape::scale,      "scale" },
            { leaf_shape::pinnate,    "pinnate" },
            { leaf_shape::blade,      "blade" }
        };

        const enum_name<bark_style> bark_style_names[] =
        {
            { bark_style::furrowed, "furrowed" },
            { bark_style::plated,   "plated" },
            { bark_style::smooth,   "smooth" },
            { bark_style::birch,    "birch" },
            { bark_style::peeling,  "peeling" },
            { bark_style::fibrous,  "fibrous" },
            { bark_style::twisted,  "twisted" },
            { bark_style::ringed,   "ringed" }
        };

        const enum_name<tree_class> tree_class_names[] =
        {
            { tree_class::shrub,     "shrub" },
            { tree_class::broadleaf, "broadleaf" },
            { tree_class::conifer,   "conifer" },
            { tree_class::palm,      "palm" },
            { tree_class::hero,      "hero" }
        };

        template<typename T, size_t N>
        T enum_from(const string& text, const enum_name<T> (&names)[N], const T fallback)
        {
            for (const enum_name<T>& entry : names)
            {
                if (text == entry.name)
                {
                    return entry.value;
                }
            }
            return fallback;
        }

        template<typename T, size_t N>
        const char* enum_to(const T value, const enum_name<T> (&names)[N])
        {
            for (const enum_name<T>& entry : names)
            {
                if (value == entry.value)
                {
                    return entry.name;
                }
            }
            return names[0].name;
        }

        float read(const mcp_json::value& node, const char* key, const float fallback)
        {
            return static_cast<float>(node.member_number(key, fallback));
        }

        uint32_t read_uint(const mcp_json::value& node, const char* key, const uint32_t fallback)
        {
            return static_cast<uint32_t>(max(0.0, node.member_number(key, static_cast<double>(fallback))));
        }

        // colours are written as [r, g, b] in linear 0 to 1, a hex string reads too so a species can be typed from a photo picker
        Vector3 read_color(const mcp_json::value& node, const char* key, const Vector3& fallback)
        {
            const mcp_json::value* item = node.find(key);
            if (!item)
            {
                return fallback;
            }

            if (item->is_array() && item->array_items.size() >= 3)
            {
                return Vector3(
                    static_cast<float>(item->array_items[0].number_or(fallback.x)),
                    static_cast<float>(item->array_items[1].number_or(fallback.y)),
                    static_cast<float>(item->array_items[2].number_or(fallback.z))
                );
            }

            string text = item->string_or("");
            if (!text.empty() && text[0] == '#')
            {
                text = text.substr(1);
            }
            if (text.size() == 6)
            {
                auto channel = [&text](const size_t offset)
                {
                    const float srgb = static_cast<float>(stoi(text.substr(offset, 2), nullptr, 16)) / 255.0f;
                    return srgb <= 0.04045f ? srgb / 12.92f : pow((srgb + 0.055f) / 1.055f, 2.4f);
                };
                return Vector3(channel(0), channel(2), channel(4));
            }
            return fallback;
        }

        string write_color(const Vector3& color)
        {
            ostringstream stream;
            stream << "[" << color.x << ", " << color.y << ", " << color.z << "]";
            return stream.str();
        }

        string json_escape(const string& text)
        {
            string result;
            for (const char character : text)
            {
                switch (character)
                {
                    case '"':  result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n"; break;
                    default:   result += character; break;
                }
            }
            return result;
        }

        void hash_combine(uint64_t& hash, const uint64_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        }

        void hash_float(uint64_t& hash, const float value)
        {
            uint32_t bits = 0;
            memcpy(&bits, &value, sizeof(bits));
            hash_combine(hash, bits);
        }

        void hash_color(uint64_t& hash, const Vector3& value)
        {
            hash_float(hash, value.x);
            hash_float(hash, value.y);
            hash_float(hash, value.z);
        }

        bool is_species_root(const string& directory)
        {
            return FileSystem::IsDirectory(directory);
        }
    }

    const char* to_string(const season value)
    {
        switch (value)
        {
            case season::spring: return "spring";
            case season::autumn: return "autumn";
            case season::bare:   return "bare";
            default:             return "summer";
        }
    }

    const char* to_string(const quality value)
    {
        switch (value)
        {
            case quality::mobile: return "mobile";
            case quality::hero:   return "hero";
            default:              return "standard";
        }
    }

    bool season_from_string(const string& text, season& value)
    {
        if (text == "spring") { value = season::spring; return true; }
        if (text == "summer") { value = season::summer; return true; }
        if (text == "autumn" || text == "fall") { value = season::autumn; return true; }
        if (text == "bare" || text == "winter") { value = season::bare; return true; }
        return false;
    }

    bool quality_from_string(const string& text, quality& value)
    {
        if (text == "mobile" || text == "low") { value = quality::mobile; return true; }
        if (text == "standard" || text == "medium") { value = quality::standard; return true; }
        if (text == "hero" || text == "high") { value = quality::hero; return true; }
        return false;
    }

    string species_directory()
    {
        // walk up from the executable to the repository so the json in data/ is read live, a packaged build has it next to the exe
        string current = filesystem::path(FileSystem::GetExecutableDirectory()).lexically_normal().generic_string();
        for (uint32_t level = 0; level < 8; level++)
        {
            const string candidate = current + "/data/trees/species";
            if (FileSystem::IsDirectory(current + "/source/core") && is_species_root(candidate))
            {
                return candidate;
            }

            const string parent = FileSystem::GetParentDirectory(current);
            if (parent == current)
            {
                break;
            }
            current = parent;
        }
        return "data/trees/species";
    }

    vector<string> region_order()
    {
        return
        {
            "Greece",
            "Italy",
            "Central Europe",
            "Scandinavia",
            "Japan",
            "North America",
            "Middle East",
            "Africa",
            "Australia",
            "South America",
            "Asia"
        };
    }

    bool parse_species(const string& json, species& output, string& error)
    {
        mcp_json::value root;
        if (!mcp_json::parse(json, root, error))
        {
            return false;
        }
        if (!root.is_object())
        {
            error = "a species file must be a json object";
            return false;
        }

        species& s = output;
        s.id          = root.member_string("id", s.id);
        s.name        = root.member_string("name", s.name);
        s.scientific  = root.member_string("scientific", s.scientific);
        s.region      = root.member_string("region", s.region);
        s.description = root.member_string("description", s.description);
        s.type        = enum_from(root.member_string("class", enum_to(s.type, tree_class_names)), tree_class_names, s.type);
        s.evergreen   = root.member_boolean("evergreen", s.evergreen);
        s.budget      = read_uint(root, "budget", s.budget);

        s.height     = max(0.3f, read(root, "height", s.height));
        s.height_var = read(root, "height_var", s.height_var);

        if (const mcp_json::value* trunk = root.find("trunk"))
        {
            s.trunk_radius   = read(*trunk, "radius", s.trunk_radius);
            s.trunk_taper    = read(*trunk, "taper", s.trunk_taper);
            s.trunk_flare    = read(*trunk, "flare", s.trunk_flare);
            s.trunk_lean     = read(*trunk, "lean", s.trunk_lean);
            s.trunk_gnarl    = read(*trunk, "gnarl", s.trunk_gnarl);
            s.trunk_twist    = read(*trunk, "twist", s.trunk_twist);
            s.trunk_segments = max(3u, read_uint(*trunk, "segments", s.trunk_segments));
            s.stems          = max(1u, read_uint(*trunk, "stems", s.stems));
            s.split_height   = read(*trunk, "split_height", s.split_height);
            s.split_angle    = read(*trunk, "split_angle", s.split_angle);
            s.stem_spread    = read(*trunk, "stem_spread", s.stem_spread);
        }

        if (const mcp_json::value* crown = root.find("crown"))
        {
            s.shape       = enum_from(crown->member_string("shape", enum_to(s.shape, crown_shape_names)), crown_shape_names, s.shape);
            s.crown_base  = read(*crown, "base", s.crown_base);
            s.crown_width = read(*crown, "width", s.crown_width);
        }

        if (const mcp_json::value* levels = root.find("levels"); levels && levels->is_array())
        {
            s.levels.clear();
            for (const mcp_json::value& node : levels->array_items)
            {
                branch_level level;
                level.count          = read(node, "count", level.count);
                level.start          = read(node, "start", level.start);
                level.end            = read(node, "end", level.end);
                level.length         = read(node, "length", level.length);
                level.length_var     = read(node, "length_var", level.length_var);
                level.down_angle     = read(node, "down_angle", level.down_angle);
                level.down_angle_tip = read(node, "down_angle_tip", level.down_angle);
                level.down_angle_var = read(node, "down_angle_var", level.down_angle_var);
                level.rotate         = read(node, "rotate", level.rotate);
                level.whorl          = max(1u, read_uint(node, "whorl", level.whorl));
                level.planar         = node.member_boolean("planar", level.planar);
                level.curve          = read(node, "curve", level.curve);
                level.curve_var      = read(node, "curve_var", level.curve_var);
                level.gravity        = read(node, "gravity", level.gravity);
                level.radius_ratio   = read(node, "radius_ratio", level.radius_ratio);
                level.segments       = max(2u, read_uint(node, "segments", level.segments));
                s.levels.push_back(level);
                if (s.levels.size() == 4)
                {
                    break;
                }
            }
        }

        if (const mcp_json::value* leaves = root.find("leaves"))
        {
            s.leaf            = enum_from(leaves->member_string("shape", enum_to(s.leaf, leaf_shape_names)), leaf_shape_names, s.leaf);
            s.leaf_size       = read(*leaves, "size", s.leaf_size);
            s.card_size       = max(0.05f, read(*leaves, "card_size", s.card_size));
            s.card_density    = read(*leaves, "density", s.card_density);
            s.leaf_start      = read(*leaves, "start", s.leaf_start);
            s.leaf_tilt       = read(*leaves, "tilt", s.leaf_tilt);
            s.leaves_per_card = max(1u, read_uint(*leaves, "per_card", s.leaves_per_card));
            s.leaf_color      = read_color(*leaves, "color", s.leaf_color);
            s.leaf_color_back = read_color(*leaves, "color_back", s.leaf_color_back);
            s.spring_color    = read_color(*leaves, "spring_color", s.spring_color);
            s.autumn_color    = read_color(*leaves, "autumn_color", s.autumn_color);
            s.autumn_color_b  = read_color(*leaves, "autumn_color_b", s.autumn_color_b);
            s.leaf_color_var  = read(*leaves, "color_var", s.leaf_color_var);
            s.flowers         = leaves->member_boolean("flowers", s.flowers);
            s.flower_color    = read_color(*leaves, "flower_color", s.flower_color);
            s.translucency    = read(*leaves, "translucency", s.translucency);
        }

        if (const mcp_json::value* palm = root.find("palm"))
        {
            s.fronds       = read_uint(*palm, "fronds", s.fronds);
            s.frond_length = read(*palm, "frond_length", s.frond_length);
            s.frond_droop  = read(*palm, "droop", s.frond_droop);
        }

        if (const mcp_json::value* bark = root.find("bark"))
        {
            s.bark            = enum_from(bark->member_string("style", enum_to(s.bark, bark_style_names)), bark_style_names, s.bark);
            s.bark_color      = read_color(*bark, "color", s.bark_color);
            s.bark_color_dark = read_color(*bark, "color_dark", s.bark_color_dark);
            s.bark_scale      = max(0.1f, read(*bark, "scale", s.bark_scale));
            s.bark_roughness  = read(*bark, "roughness", s.bark_roughness);
        }

        if (s.levels.empty() && s.fronds == 0 && s.type != tree_class::palm)
        {
            s.levels.push_back(branch_level());
        }
        if (s.name.empty())
        {
            s.name = s.id;
        }
        return true;
    }

    bool load_species(const string& file_path, species& output, string& error)
    {
        ifstream file(file_path, ios::binary);
        if (!file)
        {
            error = "cannot open " + file_path;
            return false;
        }
        stringstream buffer;
        buffer << file.rdbuf();

        output    = species();
        output.id = filesystem::path(file_path).stem().string();
        if (!parse_species(buffer.str(), output, error))
        {
            error = file_path + ": " + error;
            return false;
        }
        output.file_path = filesystem::path(file_path).generic_string();
        if (output.region.empty())
        {
            output.region = filesystem::path(file_path).parent_path().filename().string();
        }
        return true;
    }

    vector<species> load_all_species(string* errors)
    {
        vector<species> result;
        const string directory = species_directory();
        error_code ec;
        if (!filesystem::is_directory(directory, ec))
        {
            if (errors)
            {
                *errors = "species directory not found: " + directory;
            }
            return result;
        }

        for (const filesystem::directory_entry& entry : filesystem::recursive_directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
            {
                continue;
            }

            species loaded;
            string error;
            if (load_species(entry.path().generic_string(), loaded, error))
            {
                result.push_back(move(loaded));
            }
            else if (errors)
            {
                *errors += error + "\n";
            }
        }

        const vector<string> order = region_order();
        auto region_rank = [&order](const string& region)
        {
            const auto it = find(order.begin(), order.end(), region);
            return it == order.end() ? order.size() : static_cast<size_t>(it - order.begin());
        };
        sort(result.begin(), result.end(), [&region_rank](const species& a, const species& b)
        {
            const size_t rank_a = region_rank(a.region);
            const size_t rank_b = region_rank(b.region);
            if (rank_a != rank_b)
            {
                return rank_a < rank_b;
            }
            if (a.region != b.region)
            {
                return a.region < b.region;
            }
            return a.name < b.name;
        });
        return result;
    }

    string serialize_species(const species& s)
    {
        ostringstream o;
        o << "{\n";
        o << "  \"id\": \"" << json_escape(s.id) << "\",\n";
        o << "  \"name\": \"" << json_escape(s.name) << "\",\n";
        o << "  \"scientific\": \"" << json_escape(s.scientific) << "\",\n";
        o << "  \"region\": \"" << json_escape(s.region) << "\",\n";
        o << "  \"description\": \"" << json_escape(s.description) << "\",\n";
        o << "  \"class\": \"" << enum_to(s.type, tree_class_names) << "\",\n";
        o << "  \"evergreen\": " << (s.evergreen ? "true" : "false") << ",\n";
        o << "  \"budget\": " << s.budget << ",\n";
        o << "  \"height\": " << s.height << ",\n";
        o << "  \"height_var\": " << s.height_var << ",\n";
        o << "  \"trunk\": { \"radius\": " << s.trunk_radius << ", \"taper\": " << s.trunk_taper << ", \"flare\": " << s.trunk_flare
          << ", \"lean\": " << s.trunk_lean << ", \"gnarl\": " << s.trunk_gnarl << ", \"twist\": " << s.trunk_twist
          << ", \"segments\": " << s.trunk_segments << ", \"stems\": " << s.stems << ", \"split_height\": " << s.split_height
          << ", \"split_angle\": " << s.split_angle << ", \"stem_spread\": " << s.stem_spread << " },\n";
        o << "  \"crown\": { \"shape\": \"" << enum_to(s.shape, crown_shape_names) << "\", \"base\": " << s.crown_base << ", \"width\": " << s.crown_width << " },\n";
        o << "  \"levels\": [\n";
        for (size_t i = 0; i < s.levels.size(); i++)
        {
            const branch_level& l = s.levels[i];
            o << "    { \"count\": " << l.count << ", \"start\": " << l.start << ", \"end\": " << l.end << ", \"length\": " << l.length
              << ", \"length_var\": " << l.length_var << ", \"down_angle\": " << l.down_angle << ", \"down_angle_tip\": " << l.down_angle_tip
              << ", \"down_angle_var\": " << l.down_angle_var << ", \"rotate\": " << l.rotate << ", \"whorl\": " << l.whorl
              << ", \"planar\": " << (l.planar ? "true" : "false") << ", \"curve\": " << l.curve << ", \"curve_var\": " << l.curve_var
              << ", \"gravity\": " << l.gravity << ", \"radius_ratio\": " << l.radius_ratio << ", \"segments\": " << l.segments << " }"
              << (i + 1 < s.levels.size() ? ",\n" : "\n");
        }
        o << "  ],\n";
        o << "  \"leaves\": { \"shape\": \"" << enum_to(s.leaf, leaf_shape_names) << "\", \"size\": " << s.leaf_size << ", \"card_size\": " << s.card_size
          << ", \"density\": " << s.card_density << ", \"start\": " << s.leaf_start << ", \"tilt\": " << s.leaf_tilt << ", \"per_card\": " << s.leaves_per_card
          << ",\n    \"color\": " << write_color(s.leaf_color) << ", \"color_back\": " << write_color(s.leaf_color_back)
          << ", \"spring_color\": " << write_color(s.spring_color) << ",\n    \"autumn_color\": " << write_color(s.autumn_color)
          << ", \"autumn_color_b\": " << write_color(s.autumn_color_b) << ", \"color_var\": " << s.leaf_color_var
          << ",\n    \"flowers\": " << (s.flowers ? "true" : "false") << ", \"flower_color\": " << write_color(s.flower_color)
          << ", \"translucency\": " << s.translucency << " },\n";
        o << "  \"palm\": { \"fronds\": " << s.fronds << ", \"frond_length\": " << s.frond_length << ", \"droop\": " << s.frond_droop << " },\n";
        o << "  \"bark\": { \"style\": \"" << enum_to(s.bark, bark_style_names) << "\", \"color\": " << write_color(s.bark_color)
          << ", \"color_dark\": " << write_color(s.bark_color_dark) << ", \"scale\": " << s.bark_scale << ", \"roughness\": " << s.bark_roughness << " }\n";
        o << "}\n";
        return o.str();
    }

    uint64_t texture_hash(const species& s, const season time)
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        hash_combine(hash, 8); // texture generator version, bump it whenever the painters change
        hash_combine(hash, static_cast<uint64_t>(time));
        hash_combine(hash, static_cast<uint64_t>(s.leaf));
        hash_combine(hash, static_cast<uint64_t>(s.bark));
        hash_combine(hash, s.leaves_per_card);
        hash_combine(hash, s.flowers ? 1 : 0);
        hash_combine(hash, s.evergreen ? 1 : 0);
        hash_combine(hash, s.fronds);
        hash_float(hash, s.leaf_size);
        hash_float(hash, s.card_size);
        hash_float(hash, s.leaf_color_var);
        hash_color(hash, s.leaf_color);
        hash_color(hash, s.leaf_color_back);
        hash_color(hash, s.spring_color);
        hash_color(hash, s.autumn_color);
        hash_color(hash, s.autumn_color_b);
        hash_color(hash, s.flower_color);
        hash_color(hash, s.bark_color);
        hash_color(hash, s.bark_color_dark);
        return hash;
    }
}
