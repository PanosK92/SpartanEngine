/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include "Definitions.h"
#include "../commands/Command.h"
#include <string>
#include "../world/Entity.h"
//==============================

namespace spartan
{
    class Entity;

    class CommandEntityDelete : public Command
    {
    public:
        CommandEntityDelete(Entity* entity);

        virtual void OnApply() override;
        virtual void OnRevert() override;

    private:
        uint64_t m_entity_id    = 0;
        uint64_t m_parent_id    = 0;
        std::string m_entity_xml;
        TerrainSculptSnapshots m_sculpt_snapshots;
    };
}
