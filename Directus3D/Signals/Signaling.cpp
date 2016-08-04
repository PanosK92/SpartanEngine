//= INCLUDES =========
#include "Signaling.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

vector<Slot> Signaling::m_slots;

void Signaling::Connect(int signalID, std::function<void()> function)
{
	Slot slot;
	slot.signalID = signalID;
	slot.function = function;

	m_slots.push_back(slot);
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
