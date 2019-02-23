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

//= INCLUDES ========================
#include "ScriptInstance.h"
#include <angelscript.h>
#include "Module.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../World/Entity.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	ScriptInstance::ScriptInstance()
	{

	}

	ScriptInstance::~ScriptInstance()
	{
		if (m_scriptObject)
		{
			m_scriptObject->Release();
			m_scriptObject = nullptr;
		}

		m_constructorFunction	= nullptr;
		m_startFunction			= nullptr;
		m_updateFunction		= nullptr;
		m_scriptEngine			= nullptr;
		m_isInstantiated		= false;
	}

	bool ScriptInstance::Instantiate(const string& path, std::weak_ptr<Entity> entity, shared_ptr<Scripting> scriptEngine)
	{
		if (entity.expired())
			return false;

		m_scriptEngine = scriptEngine;

		// Extract properties from path
		m_scriptPath				= path;
		m_entity					= entity;
		m_className					= FileSystem::GetFileNameNoExtensionFromFilePath(m_scriptPath);
		m_moduleName				= m_className + to_string(m_entity.lock()->GetID());
		m_constructorDeclaration	= m_className + " @" + m_className + "(Entity @)";

		// Instantiate the script
		m_isInstantiated = CreateScriptObject();

		return m_isInstantiated;
	}

	void ScriptInstance::ExecuteStart()
	{
		if (!m_scriptEngine)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		m_scriptEngine->ExecuteCall(m_startFunction, m_scriptObject);
	}

	void ScriptInstance::ExecuteUpdate()
	{
		if (!m_scriptEngine)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		m_scriptEngine->ExecuteCall(m_updateFunction, m_scriptObject);
	}

	bool ScriptInstance::CreateScriptObject()
	{
		if (!m_scriptEngine)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Create module
		m_module	= make_shared<Module>(m_moduleName, m_scriptEngine);
		bool result = m_module->LoadScript(m_scriptPath);
		if (!result)
			return false;

		// Get type
		auto type_id		= m_module->GetAsIScriptModule()->GetTypeIdByDecl(m_className.c_str());
		asITypeInfo* type	= m_scriptEngine->GetAsIScriptEngine()->GetTypeInfoById(type_id);
		if (!type)
			return false;

		// Get functions in the script
		m_startFunction			= type->GetMethodByDecl("void Start()"); // Get the Start function from the script
		m_updateFunction		= type->GetMethodByDecl("void Update()"); // Get the Update function from the script
		m_constructorFunction	= type->GetFactoryByDecl(m_constructorDeclaration.c_str()); // Get the constructor function from the script
		if (!m_constructorFunction)
		{
			LOG_ERROR("Couldn't find the appropriate factory for the type '" + m_className + "'");
			return false;
		}

		asIScriptContext* context = m_scriptEngine->RequestContext(); // request a context
		int r = context->Prepare(m_constructorFunction); // prepare the context to call the factory function
		if (r < 0) return false;

		r = context->SetArgObject(0, m_entity.lock().get()); // Pass the entity as the constructor's parameter
		if (r < 0) return false;

		r = context->Execute(); // execute the call
		if (r < 0) return false;

		// get the object that was created
		m_scriptObject = *static_cast<asIScriptObject**>(context->GetAddressOfReturnValue());

		// if you're going to store the object you must increase the reference,
		// otherwise it will be destroyed when the context is reused or destroyed.
		m_scriptObject->AddRef();

		// return context
		m_scriptEngine->ReturnContext(context);

		return true;
	}
}