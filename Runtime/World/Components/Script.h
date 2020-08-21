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

//= INCLUDES ==========
#include "IComponent.h"
//=====================

namespace Spartan
{
    // FORWARD DECLERATIONS =
    class Scripting;
    struct ScriptInstance;
    //=======================

    class SPARTAN_CLASS Script : public IComponent
    {
    public:
        Script(Context* context, Entity* entity, uint32_t id = 0);
        ~Script() = default;

        //= ICOMPONENT ===============================
        void OnStart() override;
        void OnTick(float delta_time) override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        bool SetScript(const std::string& file_path);
        std::string GetScriptPath() const;
        std::string GetName() const;

    private:
        std::string m_name;
        std::string m_file_path;
        Scripting* m_scripting              = nullptr;
        ScriptInstance* m_script_instance   = nullptr;
        uint32_t m_script_instance_id       = 0;
    };
}
