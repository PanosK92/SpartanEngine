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
#include "../IO/FileSystem.h"
#include "../Core/Helper.h"
#include "../Core/Settings.h"
#include "../IO/Log.h"
//===========================

//= NAMESPACES =====
using namespace std;
//==================

Script::Script()
{
	m_scriptInstance = nullptr;
}

Script::~Script()
{
	SafeDelete(m_scriptInstance);
}

//= ICOMPONENT ==================================================================
void Script::Initialize()
{

}

void Script::Start()
{
	if (!m_scriptInstance)
		return;

	if (m_scriptInstance->IsInstantiated())
		m_scriptInstance->ExecuteStart();
}

void Script::Remove()
{

}

void Script::Update()
{
	if (!m_scriptInstance)
		return;

	if (GET_ENGINE_MODE == Editor_Idle || GET_ENGINE_MODE == Editor_Paused)
		return;

	if (m_scriptInstance->IsInstantiated())
		m_scriptInstance->ExecuteUpdate();
}

void Script::Serialize()
{
	Serializer::WriteSTR(m_scriptInstance ? m_scriptInstance->GetScriptPath() : PATH_NOT_ASSIGNED);
}

void Script::Deserialize()
{
	string scriptPath = Serializer::ReadSTR();

	if (scriptPath != PATH_NOT_ASSIGNED)
		AddScript(scriptPath);
}
//====================================================================================

bool Script::AddScript(const string& filePath)
{
	// Instantiate the script
	m_scriptInstance = new ScriptInstance();
	m_scriptInstance->Instantiate(filePath, g_gameObject, g_scriptEngine);

	// Check if the script has been instantiated successfully.
	if (!m_scriptInstance->IsInstantiated())
	{
		SafeDelete(m_scriptInstance);
		return false;
	}

	m_scriptInstance->ExecuteStart();
	return true;
}

string Script::GetScriptPath()
{
	return m_scriptInstance ? m_scriptInstance->GetScriptPath() : PATH_NOT_ASSIGNED;
}

string Script::GetName()
{
	return m_scriptInstance ? FileSystem::GetFileNameNoExtensionFromPath(GetScriptPath()) : "N/A";
}
