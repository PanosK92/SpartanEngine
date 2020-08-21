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

//= INCLUDES =========================
#include "Spartan.h"
#include "Script.h"
#include "../Entity.h"
#include "../../IO/FileStream.h"
#include "../../Scripting/Scripting.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Script::Script(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_scripting = context->GetSubsystem<Scripting>();
    }

    void Script::OnStart()
    {
        if (m_script_instance)
        {
            m_scripting->CallScriptFunction_Start(m_script_instance);
        }
    }

    void Script::OnTick(float delta_time)
    {
        // Don't run any scripts if we are not in game mode
        if (!m_context->m_engine->EngineMode_IsSet(Engine_Game))
            return;

        if (m_script_instance)
        {
            m_scripting->CallScriptFunction_Update(m_script_instance, delta_time);
        }
    }

    void Script::Serialize(FileStream* stream)
    {
        stream->Write(m_file_path);
    }

    void Script::Deserialize(FileStream* stream)
    {
        stream->Read(&m_file_path);
        SetScript(m_file_path);
    }

    bool Script::SetScript(const string& file_path)
    {
        // Load script
        const uint32_t id = m_scripting->Load(file_path, this);
        if (id == SCRIPT_NOT_LOADED)
        {
            LOG_ERROR("Failed to load script");
            return false;
        }

        // Initialise
        m_script_instance   = m_scripting->GetScript(id);
        m_file_path         = file_path;
        m_name              = FileSystem::GetFileNameNoExtensionFromFilePath(file_path);

        return true;
    }

    string Script::GetScriptPath() const
    {
        return m_file_path;
    }

    string Script::GetName() const
    {
        return m_name;
    }
}
