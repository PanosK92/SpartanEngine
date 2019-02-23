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

//= INCLUDES =============================
#include "Module.h"
#include <scriptbuilder/scriptbuilder.cpp>
#include "Scripting.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
//========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Module::Module(const string& moduleName, weak_ptr<Scripting> scriptEngine)
	{
		m_moduleName	= moduleName;
		m_scriptEngine	= scriptEngine;
	}

	Module::~Module()
	{
		if (auto scriptEngine = m_scriptEngine.lock())
		{
			scriptEngine->DiscardModule(m_moduleName);
		}
	}

	bool Module::LoadScript(const string& filePath)
	{
		auto scriptEngine = m_scriptEngine.lock();
		if (!scriptEngine)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// start new module
		m_scriptBuilder = make_unique<CScriptBuilder>();
		int result = m_scriptBuilder->StartNewModule(scriptEngine->GetAsIScriptEngine(), m_moduleName.c_str());
		if (result < 0)
		{
			LOG_ERROR("Failed to start new module, make sure there is enough memory for it to be allocated.");
			return false;
		}

		// load the script
		result = m_scriptBuilder->AddSectionFromFile(filePath.c_str());
		if (result < 0)
		{
			LOGF_ERROR("Failed to load script \"%s\".", filePath);
			return false;
		}

		// build the script
		result = m_scriptBuilder->BuildModule();
		if (result < 0)
		{
			LOGF_ERROR("Failed to compile script \"%s\". Correct any errors and try again.", FileSystem::GetFileNameFromFilePath(filePath));
			return false;
		}

		return true;
	}

	asIScriptModule* Module::GetAsIScriptModule()
	{
		if (!m_scriptBuilder)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		return m_scriptBuilder->GetModule();
	}
}