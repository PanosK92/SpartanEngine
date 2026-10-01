/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
// Finite application component list; names are the serialized identifiers.
#define SP_GAME_COMPONENT_LIST \
    X(SkidMarks, skid_marks) \
    X(Traffic, traffic) \
    X(Pedestrians, pedestrians) \
    X(CarReset, car_reset) \
    X(RaceDriver, race_driver) \
    X(RouteDriver, route_driver)
