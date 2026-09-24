/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "Widget.h"
#include <string>
#include <vector>
#include "memory/GpuMemory.h"
//================================

class MemoryViewer : public Widget
{
public:
    MemoryViewer(Editor* editor);
    void OnTickVisible() override;

private:
    bool m_show_gpu = true;
    bool m_frozen   = false;
    std::vector<spartan::GpuMemoryBlock> m_frozen_blocks;
    void* m_selected_resource = nullptr;
    std::string m_export_path;
};
