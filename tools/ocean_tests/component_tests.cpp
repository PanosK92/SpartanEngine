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

// Exercise the production component without starting the renderer.
#include <new>
#include "pch.h"
#include "world/components/Water.h"
#include "io/pugixml.hpp"
#include <stdexcept>
#include <Windows.h>

static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    printf("PASS %s\n", message);
}

// The bundled Lua amalgamation exports main; use a separate CRT entry point.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    using spartan::Water;
    Water water(nullptr);
    check(water.GetAttributes().size() == 3, "only three editable water settings");

    pugi::xml_document legacy;
    legacy.load_string("<component><water amplitude='0.5' displacement_scale='2' choppiness='4' normal_strength='0' cascade_count='1' turbidity='2' sea_level='7'/></component>");
    auto node = legacy.child("component");
    water.Load(node);
    check(water.GetWaveSize() == 1.0f && water.GetSeaLevel() == 7.0f, "legacy height multipliers and sea level migrate");
    check(water.GetClarity() == 0.5f, "legacy particle density migrates");
    check(water.GetNormalStrength() == 1.0f && water.GetDisplacementScale() == 1.0f && water.GetCascadeCount() == 4,
        "legacy lighting and detail overrides become automatic");

    water.SetWaveSize(0.0f);
    water.SetClarity(1.0f);
    pugi::xml_document saved;
    auto saved_node = saved.append_child("component");
    water.Save(saved_node);
    Water restored(nullptr);
    restored.Load(saved_node);
    check(restored.GetWaveSize() == 0.0f && restored.GetClarity() == 1.0f && restored.GetSeaLevel() == 7.0f,
        "new settings round-trip including flat and clear endpoints");
    check(!saved_node.child("water").attribute("amplitude"), "saves contain the simplified schema");

    water.SetWaveSize(2.5f);
    water.SetClarity(0.25f);
    restored.SetAttributes(water.GetAttributes());
    check(restored.GetWaveSize() == 2.5f && restored.GetClarity() == 0.25f && restored.GetSeaLevel() == 7.0f,
        "component cloning preserves all three controls");

    water.SetWaveSize(-2.0f);
    water.SetClarity(2.0f);
    check(water.GetWaveSize() == 0.0f && water.GetClarity() == 1.0f, "out-of-range controls clamp");
    water.SetWaveSize(std::numeric_limits<float>::quiet_NaN());
    water.SetSeaLevel(std::numeric_limits<float>::infinity());
    water.SetClarity(std::numeric_limits<float>::quiet_NaN());
    check(water.GetWaveSize() == 1.0f && water.GetSeaLevel() == 0.0f && water.GetClarity() == 0.75f,
        "non-finite controls restore safe defaults");

    pugi::xml_document invalid;
    invalid.load_string("<component><water wave_size='nan' clarity='inf' sea_level='nan'/></component>");
    node = invalid.child("component");
    water.Load(node);
    check(std::isfinite(water.GetWaveSize()) && std::isfinite(water.GetClarity()) && std::isfinite(water.GetSeaLevel()),
        "malformed scene numbers cannot reach the GPU");

    pugi::xml_document scene;
    check(scene.load_file("worlds/dreamcore.world"), "Dreamcore scene parses");
    for (auto entity : scene.child("World").child("Entities").children("Entity"))
    {
        auto component = entity.child("water");
        if (!component) continue;
        water.Load(component);
        check(water.GetWaveSize() == 1.0f && water.GetClarity() == 0.75f && water.GetSeaLevel() == 0.0f,
            "existing Dreamcore water loads unchanged in size, clarity and level");
        return 0;
    }
    check(false, "Dreamcore contains water");
    return 1;
}
