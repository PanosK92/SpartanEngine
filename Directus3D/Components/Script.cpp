/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ================
#include "Script.h"
#include "../Core/Settings.h"
#include "../IO/Serializer.h"
//===========================

using namespace std;

#define SCRIPT_PATH_INVALID "-1"

Script::Script()
{
}

Script::~Script()
{
	vector<ScriptInstance*>::iterator it;
	for (it = m_scriptInstances.begin(); it < m_scriptInstances.end(); ++it)
	{
		ScriptInstance* scriptInstance = *it;
		delete scriptInstance;
	}
	m_scriptInstances.clear();
}

void Script::Initialize()
{
}

void Script::Update()
{
	if (ENGINE_MODE != Editor_Play)
		return;

	for (auto i = 0; i < m_scriptInstances.size(); i++)
	{
		if (m_scriptInstances[i]->IsInstantiated())
			m_scriptInstances[i]->ExecuteUpdate();
	}
}

void Script::Serialize()
{
	// save script count
	Serializer::SaveInt(static_cast<int>(m_scriptInstances.size()));

	// save script paths
	for (auto i = 0; i < m_scriptInstances.size(); i++)
		Serializer::SaveSTR(m_scriptInstances[i]->GetScriptPath());
}

void Script::Deserialize()
{
	// load script count
	int scriptCount = Serializer::LoadInt();

	// load scripts
	for (int i = 0; i < scriptCount; i++)
	{
		string scriptPath = Serializer::LoadSTR();
		AddScript(scriptPath, i);
	}
}

bool Script::AddScript(string path, int slot)
{
	if (path == SCRIPT_PATH_INVALID)
		return false;

	// Instantiate the script
	ScriptInstance* scriptInstance = new ScriptInstance();
	scriptInstance->Instantiate(path, g_gameObject, g_scriptEngine);

	// If the script didn't instantiate successfully, don't bother with anything
	if (!scriptInstance->IsInstantiated())
	{
		delete scriptInstance;
		return false;
	}

	// Check for an existing script at the same slot
	bool replaced = false;
	for (auto i = 0; i < m_scriptInstances.size(); i++)
	{
		if (i != slot)
			continue;

		ScriptInstance* oldScriptInstance = m_scriptInstances[i];

		// Replace the script at this index with the new one
		m_scriptInstances.at(i) = scriptInstance;
		replaced = true;

		// Delete the old one
		delete oldScriptInstance;
	}

	// If the script didn't replace any existing one, add it
	if (!replaced)
		m_scriptInstances.push_back(scriptInstance);

	// Execute start function
	scriptInstance->ExecuteStart();

	return true;
}

string Script::GetScriptPath(int slot)
{
	for (auto i = 0; i < m_scriptInstances.size(); i++)
	{
		if (slot == i)
			return m_scriptInstances[i]->GetScriptPath();
	}

	return SCRIPT_PATH_INVALID;
}
