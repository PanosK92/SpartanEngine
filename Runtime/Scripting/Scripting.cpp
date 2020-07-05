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

//= INCLUDES =================================
#include "Spartan.h"
#include "Scripting.h"
#include <scriptstdstring/scriptstdstring.cpp>
#include "ScriptInterface.h"
//===========================================

namespace Spartan
{
	Scripting::Scripting(Context* context) : ISubsystem(context)
	{
		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EventType::WorldUnload, EVENT_HANDLER(Clear));
	}

	Scripting::~Scripting()
	{
		Clear();

		if (m_scriptEngine)
		{
			m_scriptEngine->ShutDownAndRelease();
			m_scriptEngine = nullptr;
		}
	}

    bool Scripting::Initialize()
    {
        m_scriptEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
        if (!m_scriptEngine)
        {
            LOG_ERROR("Failed to create AngelScript engine");
            return false;
        }

        // Register the string type
        RegisterStdString(m_scriptEngine);

        // Register engine script interface
        auto scriptInterface = make_shared<ScriptInterface>();
        scriptInterface->Register(m_scriptEngine, m_context);

        // Set the message callback to print the human readable messages that the engine gives in case of errors
        m_scriptEngine->SetMessageCallback(asMETHOD(Scripting, message_callback), this, asCALL_THISCALL);

        m_scriptEngine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);

        // Get version
        const string major = to_string(ANGELSCRIPT_VERSION).erase(1, 4);
        const string minor = to_string(ANGELSCRIPT_VERSION).erase(0, 1).erase(2, 2);
        const string rev = to_string(ANGELSCRIPT_VERSION).erase(0, 3);
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("AngelScript", major + "." + minor + "." + rev, "https://www.angelcode.com/angelscript/downloads.html");

        return true;
    }

    void Scripting::Clear()
	{
		for (auto& context : m_contexts)
		{
			context->Release();
		}

		m_contexts.clear();
		m_contexts.shrink_to_fit();
	}

	asIScriptEngine* Scripting::GetAsIScriptEngine() const
    {
		return m_scriptEngine;
	}

	/*------------------------------------------------------------------------------
									[CONTEXT]
	------------------------------------------------------------------------------*/
	// Contexts is what you use to call AngelScript functions and methods.
	// They say you must pool them to avoid overhead. So I do as they say.
	asIScriptContext* Scripting::RequestContext()
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
	void Scripting::ReturnContext(asIScriptContext* context)
	{
		if (!context)
		{
			LOG_ERROR("Scripting::ReturnContext: Context is null");
			return;
		}
		m_contexts.push_back(context);
		context->Unprepare();
	}

	/*------------------------------------------------------------------------------
								[CALLS]
	------------------------------------------------------------------------------*/
	bool Scripting::ExecuteCall(asIScriptFunction* scriptFunc, asIScriptObject* obj, float delta_time /*=-1.0f*/)
	{
		asIScriptContext* ctx = RequestContext();

		ctx->Prepare(scriptFunc); // prepare the context for calling the method

        // Instance data and function parameters
		ctx->SetObject(obj); // set the object pointer
        if (delta_time != -1.0f) ctx->SetArgFloat(0, delta_time);

        const int r = ctx->Execute(); // execute the call

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
	void Scripting::DiscardModule(const string& moduleName) const
    {
		m_scriptEngine->DiscardModule(moduleName.c_str());
	}

	/*------------------------------------------------------------------------------
									[PRIVATE]
	------------------------------------------------------------------------------*/
	// This is used for script exception messages
	void Scripting::LogExceptionInfo(asIScriptContext* ctx) const
    {
        const string exceptionDescription = ctx->GetExceptionString(); // get the exception that occurred
		const asIScriptFunction* function = ctx->GetExceptionFunction(); // get the function where the exception occurred

        const string functionDecleration = function->GetDeclaration();
		string moduleName = function->GetModuleName();
        const string scriptPath = function->GetScriptSectionName();
        const string scriptFile = FileSystem::GetFileNameFromFilePath(scriptPath);
        const string exceptionLine = to_string(ctx->GetExceptionLineNumber());

		LOG_ERROR("%s, at line %s, in function %s, in script %s", exceptionDescription.c_str(), exceptionLine.c_str(), functionDecleration.c_str(), scriptFile.c_str());
	}

	// This is used for AngelScript error messages
	void Scripting::message_callback(const asSMessageInfo& msg) const
    {
        const string filename = FileSystem::GetFileNameFromFilePath(msg.section);
        const string message = msg.message;

		string final_message;
		if (filename != "")
			final_message = filename + " " + message;
		else
			final_message = message;

		LOG_ERROR(final_message);
	}
}
