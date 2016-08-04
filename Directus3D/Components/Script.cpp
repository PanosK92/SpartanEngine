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
#include "../IO/Serializer.h"
#include "../IO/FileHelper.h"
#include "../Core/Globals.h"
#include "../Core/Settings.h"
//===========================

//= NAMESPACES =====
using namespace std;
//==================

#define SCRIPT_PATH_INVALID "-1"
#define SCRIPT_NAME_INVALID "-1"

Script::Script()
{
	m_scriptInstance = nullptr;
}

Script::~Script()
{
	SafeDelete(m_scriptInstance);
}

void Script::Initialize()
{

}

void Script::Start()
{

}

void Script::Remove()
{

}

void Script::Update()
{
	if (GET_ENGINE_MODE == Editor_Stop || GET_ENGINE_MODE == Editor_Pause)
		return;

	if (m_scriptInstance->IsInstantiated())
		m_scriptInstance->ExecuteUpdate();
}

void Script::Serialize()
{
	Serializer::SaveSTR(m_scriptInstance->GetScriptPath());
}

void Script::Deserialize()
{
	string scriptPath = Serializer::LoadSTR();
	AddScript(scriptPath);
}

bool Script::AddScript(string path)
{
	if (path == SCRIPT_PATH_INVALID)
		return false;

	// Instantiate the script
	m_scriptInstance = new ScriptInstance();
	m_scriptInstance->Instantiate(path, g_gameObject, g_scriptEngine);

	// If the script didn't instantiate successfully, don't bother with anything
	if (!m_scriptInstance->IsInstantiated())
	{
		delete m_scriptInstance;
		m_scriptInstance = nullptr;
		return false;
	}

	// Execute start function
	m_scriptInstance->ExecuteStart();

	return true;
}

string Script::GetScriptPath()
{
	return m_scriptInstance ? m_scriptInstance->GetScriptPath() : SCRIPT_PATH_INVALID;
}

string Script::GetName()
{
	return m_scriptInstance ? FileHelper::GetFileNameNoExtensionFromPath(GetScriptPath()) : SCRIPT_NAME_INVALID;
}
