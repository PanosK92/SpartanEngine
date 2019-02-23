/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===========================
#include "Script.h"			
#include "../../Core/Context.h"
#include "../../IO/FileStream.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Script::Script(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{

	}

	Script::~Script()
	{

	}

	//= ICOMPONENT ==================================================================
	void Script::OnStart()
	{
		if (!m_scriptInstance)
			return;

		if (!m_scriptInstance->IsInstantiated())
			return;

		m_scriptInstance->ExecuteStart();
	}

	void Script::OnTick()
	{
		if (!m_scriptInstance)
			return;

		if (!m_scriptInstance->IsInstantiated())
			return;

		m_scriptInstance->ExecuteUpdate();
	}

	void Script::Serialize(FileStream* stream)
	{
		stream->Write(m_scriptInstance ? m_scriptInstance->GetScriptPath() : NOT_ASSIGNED);
	}

	void Script::Deserialize(FileStream* stream)
	{
		string scriptPath = NOT_ASSIGNED;
		stream->Read(&scriptPath);

		if (scriptPath != NOT_ASSIGNED)
		{
			SetScript(scriptPath);
		}
	}
	//====================================================================================

	bool Script::SetScript(const string& filePath)
	{
		// Instantiate the script
		m_scriptInstance = make_shared<ScriptInstance>();
		m_scriptInstance->Instantiate(filePath, GetEntity_PtrWeak(), GetContext()->GetSubsystem<Scripting>());

		// Check if the script has been instantiated successfully.
		if (!m_scriptInstance->IsInstantiated())
			return false;

		m_scriptInstance->ExecuteStart();
		return true;
	}

	string Script::GetScriptPath()
	{
		return m_scriptInstance ? m_scriptInstance->GetScriptPath() : NOT_ASSIGNED;
	}

	string Script::GetName()
	{
		return m_scriptInstance ? FileSystem::GetFileNameNoExtensionFromFilePath(GetScriptPath()) : NOT_ASSIGNED;
	}
}
