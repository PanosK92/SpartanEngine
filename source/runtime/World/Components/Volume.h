/*
Copyright(c) 2015-2026 Panos Karabelas

permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "software"), to deal
in the software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the software, and to permit persons to whom the software is furnished
to do so, subject to the following conditions :

the above copyright notice and this permission notice shall be included in
all copies or substantial portions of the software.

the software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and noninfringement. in no event shall the authors or
copyright holders be liable for any claim, damages or other liability, whether
in an action of contract, tort or otherwise, arising from, out of or in
connection with the software or the use or other dealings in the software.
*/

#pragma once

//= includes ===================================
#include "Component.h"
#include "../../Math/BoundingBox.h"
#include <unordered_map>
#include <string>
//==============================================

namespace spartan
{
    class Entity;

    class Volume : public Component
    {
    public:
        Volume(Entity* entity);
        ~Volume() = default;

        //= COMPONENT =====================
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        //=================================

        // box
        const math::BoundingBox& GetBoundingBox() const   { return m_bounding_box; }
        void SetBoundingBox(const math::BoundingBox& box) { m_bounding_box = box; }

        // options (use cvar names like "r.bloom", "r.fog" as keys)
        void SetOption(const char* name, float value);
        void RemoveOption(const char* name);
        float GetOption(const char* name) const;
        const std::unordered_map<std::string, float>& GetOptions() const { return m_options; }

    private:
        // the shape of the volume
        math::BoundingBox m_bounding_box;

        // the user defined overrides
        std::unordered_map<std::string, float> m_options;
    };
}
