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
//===================

struct Slot
{
	int signalID;
	std::function<void(void)> function;
};

class Signaling
{
public:
	static void Connect(int signalID, std::function<void(void)> function);
	static void Disconnect(int signalID, std::function<void(void)> function);
	static void EmitSignal(int signalID);
	static void DeleteAll();

	static std::vector<Slot> m_slots;
};

#define CONNECT_TO_SIGNAL(signalID, function)		Signaling::Connect(signalID, function)
#define DISCONNECT_FROM_SIGNAL(signalID, function)	Signaling::Disconnect(signalID, function)
#define EMIT_SIGNAL(signalID)						Signaling::EmitSignal(signalID)

/*
HOW TO USE
==========
CONNECT_TO_SIGNAL		(SIGNAL_FRAME_START, std::bind(&Class::Function, this));
DISCONNECT_FROM_SIGNAL	(SIGNAL_FRAME_START, std::bind(&Class::Function, this));
EMIT_SIGNAL				(SIGNAL_FRAME_START);
*/