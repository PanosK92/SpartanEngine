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

//= INCLUDES ================================
#include "ScriptEngine.h"
#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <scriptstdstring/scriptstdstring.cpp>
#include "ScriptInterface.h"
#include "Module.h"
#include "../IO/Log.h"
#include "../IO/FileSystem.h"
#include "../Core/Context.h"
//==========================================

#define AS_USE_STLNAMES = 1

ScriptEngine::ScriptEngine(Context* context) : Object(context)
{
	m_scriptEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
	if (!m_scriptEngine)
	{
		LOG_ERROR("Failed to create AngelScript engine.");
		return;
	}

	// Register the string type
	RegisterStdString(m_scriptEngine);

	// Register engine script interface
	auto scriptInterface = make_shared<ScriptInterface>();
	scriptInterface->Register(m_scriptEngine, g_context);

	// Set the message callback to print the human readable messages that the engine gives in case of errors
	m_scriptEngine->SetMessageCallback(asMETHOD(ScriptEngine, message_callback), this, asCALL_THISCALL);

	m_scriptEngine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);
}

ScriptEngine::~ScriptEngine()
{
	Reset();

	if (m_scriptEngine)
	{
		m_scriptEngine->ShutDownAndRelease();
		m_scriptEngine = nullptr;
	}
}

void ScriptEngine::Reset()
{
	for (auto n = 0; n < m_contexts.size(); n++)
		m_contexts[n]->Release();

	m_contexts.clear();
	m_contexts.shrink_to_fit();
}

asIScriptEngine* ScriptEngine::GetAsIScriptEngine()
{
	return m_scriptEngine;
}

/*------------------------------------------------------------------------------
								[CONTEXT]
------------------------------------------------------------------------------*/
// Contexts is what you use to call AngelScript functions and methods.
// They say you must pool them to avoid overhead. So I do as they say.
asIScriptContext* ScriptEngine::RequestContext()
{
	asIScriptContext* context = nullptr;
	if (m_contexts.size())
	{
		context = *m_contexts.rbegin();
		m_contexts.pop_back();
	}
	else
	{
		context = m_scriptEngine->CreateContext();
	}

	return context;
}

// A context should be returned after calling an AngelScript function, 
// it will be inserted back in the pool for re-use
void ScriptEngine::ReturnContext(asIScriptContext* context)
{
	m_contexts.push_back(context);
	context->Unprepare();
}

/*------------------------------------------------------------------------------
							[CALLS]
------------------------------------------------------------------------------*/
bool ScriptEngine::ExecuteCall(asIScriptFunction* scriptFunc, asIScriptObject* obj)
{
	asIScriptContext* ctx = RequestContext();

	ctx->Prepare(scriptFunc); // prepare the context for calling the method
	ctx->SetObject(obj); // set the object pointer
	int r = ctx->Execute(); // execute the call

	// output any exceptions
	if (r == asEXECUTION_EXCEPTION)
	{
		LogExceptionInfo(ctx);
		ReturnContext(ctx);
		return false;
	}
	ReturnContext(ctx);

	return true;
}

/*------------------------------------------------------------------------------
									[MODULE]
------------------------------------------------------------------------------*/
void ScriptEngine::DiscardModule(string moduleName)
{
	m_scriptEngine->DiscardModule(moduleName.c_str());
}

/*------------------------------------------------------------------------------
								[PRIVATE]
------------------------------------------------------------------------------*/
// This is used for script exception messages
void ScriptEngine::LogExceptionInfo(asIScriptContext* ctx)
{
	string exceptionDescription = ctx->GetExceptionString(); // get the exception that occurred
	const asIScriptFunction* function = ctx->GetExceptionFunction(); // get the function where the exception occured

	string functionDecleration = function->GetDeclaration();
	string moduleName = function->GetModuleName();
	string scriptPath = function->GetScriptSectionName();
	string scriptFile = FileSystem::GetFileNameFromPath(scriptPath);
	string exceptionLine = to_string(ctx->GetExceptionLineNumber());

	LOG_ERROR(exceptionDescription + ", at line " + exceptionLine + ", in function " + functionDecleration + ", in script " + scriptFile);
}

// This is used for AngelScript error messages
void ScriptEngine::message_callback(const asSMessageInfo& msg)
{
	string filename = FileSystem::GetFileNameFromPath(msg.section);
	string message = msg.message;

	string finalMessage;
	if (filename != "")
		finalMessage = filename + " " + message;
	else
		finalMessage = message;

	LOG_ERROR(finalMessage);
}
