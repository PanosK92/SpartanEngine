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

//= INCLUDES ========
#include "Signals.h"
#include <vector>
#include <functional>
#include <memory>
//===================

#define CONNECT_TO_SIGNAL(signalID, function)		Signaling::Connect(signalID, function)
#define DISCONNECT_FROM_SIGNAL(signalID, function)	Signaling::Disconnect(signalID, function)
#define EMIT_SIGNAL(signalID)						Signaling::Emit(signalID)

/*
HOW TO USE
==========
CONNECT_TO_SIGNAL		(SIGNAL_FRAME_START, std::bind(&Class::Function, arguments));
CONNECT_TO_SIGNAL		(SIGNAL_FRAME_START, std::bind(&Class::Function, this, arguments));
DISCONNECT_FROM_SIGNAL	(SIGNAL_FRAME_START, std::bind(&Class::Function, this));
EMIT_SIGNAL				(SIGNAL_FRAME_START);
*/

//= HELPER FUNCTION ==========================================
template<typename FunctionType, typename... ARGS>
size_t getAddress(std::function<FunctionType(ARGS...)> function)
{
	typedef FunctionType(fnType)(ARGS...);
	fnType** fptr = function.template target<fnType*>();
	return size_t(*fptr);
}
//============================================================

//= SIGNAL ===================================================
class __declspec(dllexport) Signal
{
public:
	using functionType = std::function<void()>;

	Signal(int signalID, functionType&& arguments)
	{
		m_signalID = signalID;
		m_function = std::forward<functionType>(arguments);
	}
	int GetSignalID() { return m_signalID; }
	size_t GetAddress() { return getAddress(m_function); }
	void Emit() { m_function(); }

private:
	int m_signalID;
	functionType m_function;
};
//============================================================

//= SIGNALING ================================================
class __declspec(dllexport) Signaling
{
public:
	template <typename Function>
	static void Connect(int signalID, Function&& function);

	template <typename Function>
	static void Disconnect(int signalID, Function&& function);

	static void Emit(int signalID);
	static void Clear();

private:
	static std::vector<std::shared_ptr<Signal>> m_signals;
};
//============================================================

template <typename Function>
void Signaling::Connect(int signalID, Function&& function)
{
	m_signals.push_back(std::make_shared<Signal>(signalID, std::bind(std::forward<Function>(function))));
}

template <typename Function>
void Signaling::Disconnect(int signalID, Function&& function)
{
	for (auto it = m_signals.begin(); it != m_signals.end();)
	{
		auto slot = *it;
		if (slot->GetSignalID() == signalID && slot->GetAddress() == getAddress(function))
		{
			it = m_signals.erase(it);
			return;
		}
		++it;
	}
}