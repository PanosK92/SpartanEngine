/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_MEDIUM
#define SPARTAN_RESTIR_MEDIUM

// Match the reference's default two-slot InteriorList. Priority zero denotes
// the highest priority at the API boundary; zero slots denote vacuum internally.
// Equal priorities are ordered by material identity, not by traversal order.
static const uint RESTIR_NO_MEDIUM = 0xffffffffu;
static const uint RESTIR_MEDIUM_MATERIAL_MASK = 0x0fffffffu;

struct RestirMediumState
{
    uint2 slots;
};

uint restir_medium_material(uint slot)
{
    return slot == 0u ? RESTIR_NO_MEDIUM : slot & RESTIR_MEDIUM_MATERIAL_MASK;
}

bool restir_medium_true_intersection(RestirMediumState state, uint priority)
{
    return priority == 0u || priority >= (state.slots.x >> 28u);
}

// Query before updating the list. When leaving its top material, the next
// material is outside; entering a volume uses the current top material.
uint restir_medium_outside_material(RestirMediumState state, uint material, bool entering)
{
    uint top = restir_medium_material(state.slots.x);
    return !entering && top == material ? restir_medium_material(state.slots.y) : top;
}

// Call only for transmission or an ignored lower-priority boundary. Reflection
// leaves the list intact. As in the reference, a full list ignores insertion.
void restir_medium_cross(inout RestirMediumState state, uint material, uint priority, bool entering)
{
    uint encoded = ((priority == 0u ? 15u : priority) << 28u) |
        (material & RESTIR_MEDIUM_MATERIAL_MASK);
    if (entering)
    {
        if (state.slots.x == 0u) state.slots.x = encoded;
        else if (state.slots.y == 0u) state.slots.y = encoded;
    }
    else
    {
        if (restir_medium_material(state.slots.x) == material) state.slots.x = 0u;
        else if (restir_medium_material(state.slots.y) == material) state.slots.y = 0u;
    }
    state.slots = uint2(max(state.slots.x, state.slots.y), min(state.slots.x, state.slots.y));
}

#endif
