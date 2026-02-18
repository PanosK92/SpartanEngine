#pragma once

#include "Component.h"
#include "sol/sol.hpp"

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

    };
}
