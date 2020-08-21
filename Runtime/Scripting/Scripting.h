/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==================
#include <vector>
#include <string>
#include "ScriptInstance.h"
#include "../Core/ISubsystem.h"
//=============================

//= FORWARD DECLARATIONS =
struct _MonoDomain;
//========================

namespace Spartan
{
    //= FORWARD DECLARATIONS =
    class Script;
    //========================

    static const uint32_t SCRIPT_NOT_LOADED = 0;

    class Scripting : public ISubsystem
    {
    public:
        Scripting(Context* context);
        ~Scripting();

        //= Subsystem =============
        bool Initialize() override;
        //=========================

        uint32_t Load(const std::string& file_path, Script* script_component);
        ScriptInstance* GetScript(const uint32_t id);
        bool CallScriptFunction_Start(const ScriptInstance* script_instance);
        bool CallScriptFunction_Update(const ScriptInstance* script_instance, float delta_time);
        void Clear();

    private:
        bool CompileApiAssembly();

        MonoDomain* m_domain = nullptr;
        std::unordered_map<uint32_t, ScriptInstance> m_scripts;
        uint32_t m_script_id = SCRIPT_NOT_LOADED;
        bool m_api_assembly_compiled = false;
    };
}
