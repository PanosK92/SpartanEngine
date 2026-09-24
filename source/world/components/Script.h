/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"
SP_WARNINGS_OFF
#include "sol/sol.hpp"
SP_WARNINGS_ON

namespace spartan
{
    class Script : public Component
    {
    public:

        Script(Entity* Entity);

        sol::reference AsLua(sol::state_view state) override;

        void LoadScriptFile(std::string_view path);
        void Initialize() override;
        void Start() override;
        void Stop() override;
        void Remove() override;
        void PreTick() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;


        std::string file_path;
        sol::table  script;

    private:
        // loads the script file, applies serialized properties and runs the lua initialize and load hooks
        void LoadInternal(pugi::xml_node& node);
    };
}
