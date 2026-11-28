/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"
#include <memory>
SP_WARNINGS_OFF
#include <sol/forward.hpp>
SP_WARNINGS_ON

namespace spartan
{
    class Script : public Component
    {
    public:
        bool StartsEarly() const override { return true; }

        Script(Entity* Entity);
        ~Script() override;

        void LoadScriptFile(std::string_view path);
        void Initialize() override;
        void Start() override;
        void Stop() override;
        void Remove() override;
        void PreTick() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        struct Property
        {
            enum class Type : uint8_t { Bool, Integer, Number, String };
            std::string key;
            Type type           = Type::String;
            bool bool_value     = false;
            int int_value       = 0;
            double double_value = 0.0;
            std::string string_value;
        };

        bool IsScriptLoaded() const;
        sol::table& GetScriptTable();
        void GetProperties(std::vector<Property>& out_properties, std::vector<std::string>& out_code_members) const;
        void SetPropertyBool(const std::string& key, bool value);
        void SetPropertyInt(const std::string& key, int value);
        void SetPropertyDouble(const std::string& key, double value);
        void SetPropertyString(const std::string& key, const std::string& value);

        std::string file_path;

    private:
        struct ScriptInstance;
        // loads the script file, applies serialized properties and runs the lua initialize and load hooks
        void LoadInternal(pugi::xml_node& node);

        std::unique_ptr<ScriptInstance> m_instance;
        uint64_t m_serial = 0;
    };
}
