/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
//=================

// the world's sky, clock, place, weather and climate, they belong to the world rather than to any entity
class WorldEnvironment : public Widget
{
public:
    WorldEnvironment(Editor* editor);

    void OnPreBegin() override;
    void OnVisible() override;
    void OnTickVisible() override;

    // shows the panel and brings its tab to the front
    void Focus();

private:
    bool m_appeared_once = false;
    bool m_focus_pending = false;
    bool m_dock_checked  = false;
    bool m_dock_pending  = false;
    bool m_joins_world   = false;
};
