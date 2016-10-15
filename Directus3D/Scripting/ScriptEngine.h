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

#pragma once

//= INCLUDES ====
#include <vector>
#include <memory>
//===============

class asIScriptObject;
class asIScriptFunction;
class GameObject;
class asIScriptEngine;
class asIScriptContext;
class asIScriptModule;
class CScriptBuilder;
class Timer;
class Input;
struct asSFuncPtr;
struct asSMessageInfo;
class Module;

class ScriptEngine
{
public:
	ScriptEngine(std::shared_ptr<Timer> timer, std::shared_ptr<Input> input);
	~ScriptEngine();

	bool Initialize();
	void Reset();
	asIScriptEngine* GetAsIScriptEngine();
	std::shared_ptr<Timer> GetTimer();
	std::shared_ptr<Input> GetInput();

	/*------------------------------------------------------------------------------
									[CONTEXT]
	------------------------------------------------------------------------------*/
	asIScriptContext* RequestContext();
	void ReturnContext(asIScriptContext* ctx);

	/*------------------------------------------------------------------------------
									[CALLS]
	------------------------------------------------------------------------------*/
	bool ExecuteCall(asIScriptFunction* scriptFunc, asIScriptObject* obj);

	/*------------------------------------------------------------------------------
								[MODULE]
	------------------------------------------------------------------------------*/
	void DiscardModule(std::string moduleName);

private:
	asIScriptEngine* m_scriptEngine;
	std::vector<asIScriptContext*> m_contexts;
	std::shared_ptr<Timer> m_timer;
	std::shared_ptr<Input> m_input;

	/*------------------------------------------------------------------------------
									[PRIVATE]
	------------------------------------------------------------------------------*/
	void LogExceptionInfo(asIScriptContext* ctx);
	void message_callback(const asSMessageInfo& msg);
};
