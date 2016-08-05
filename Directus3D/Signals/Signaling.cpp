//= INCLUDES =========
#include "Signaling.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

vector<Slot> Signaling::m_slots;

void Signaling::Connect(int signalID, function<void()> function)
{
	Slot slot;
	slot.signalID = signalID;
	slot.function = function;

	m_slots.push_back(slot);
}

template<typename FunctionType, typename... ARGS>
size_t getAddress(function<FunctionType(ARGS...)> function)
{
	typedef FunctionType(fnType)(ARGS...);
	fnType** fptr = function.template target<fnType*>();
	return size_t(*fptr);
}

void Signaling::Disconnect(int signalID, function<void()> function)
{
	for (auto it = m_slots.begin(); it != m_slots.end();)
	{
		Slot slot = *it;
		if (slot.signalID == signalID && getAddress(slot.function) == getAddress(function))
		{
			it = m_slots.erase(it);
			return;
		}
		++it;
	}
}

void Signaling::EmitSignal(int signalID)
{
	for (int i = 0; i < m_slots.size(); i++)
	{
		if (m_slots[i].signalID == signalID)
			m_slots[i].function();
	}
}

void Signaling::DeleteAll()
{
	m_slots.clear();
	m_slots.shrink_to_fit();
}
