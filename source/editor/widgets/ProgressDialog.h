/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
#include "core/ProgressTracker.h"
//=================

class ProgressDialog : public Widget
{
public:
    ProgressDialog(Editor* editor);
    ~ProgressDialog() = default;

    void OnTick() override;
    void OnTickVisible() override;
    void OnPreBegin() override;

private:
    spartan::ProgressDisplay m_display;
    double m_visible_since = 0.0;
    struct BarState { uint64_t id = 0; uint64_t step_id = 0; float fraction = 0.0f; };
    std::array<BarState, 2> m_bars;

};
