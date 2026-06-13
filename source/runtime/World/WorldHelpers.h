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

#pragma once

//= INCLUDES ===============
#include <sol/forward.hpp>
//==========================

namespace spartan
{
    class Entity;

    // engine side helpers exposed to the lua world builder scripts
    // these wrap the heavy multithreaded construction that used to live in game.cpp
    class WorldHelpers
    {
    public:
        // registers material, resource cache, renderer grass, geometry buffer and forest helpers with the lua state
        static void RegisterForScripting(sol::state_view state);

        // builds the procedural forest world, terrain, water, props and gpu grass
        static void BuildForest(Entity* builder_entity);

        // releases the long lived meshes and materials owned by the builders, called from world shutdown
        static void Clear();
    };
}
