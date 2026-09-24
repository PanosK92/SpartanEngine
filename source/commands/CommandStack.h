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
#include "../commands/CircularStack.h"
//==============================

namespace spartan {
// @todo make editor setting instead of compile time constant expression
constexpr uint64_t max_undo_steps = 128;

class CommandStack {
public:
    template<typename CommandType, typename... Args>
    static void Add(Args&&... args) {
        // @todo copies the whole buffer when full, a preallocated array with an undo cursor would avoid that

        std::shared_ptr<Command> new_command = std::make_shared<CommandType>(std::forward<Args>(args)...);
        m_undo_buffer.Push(new_command);

        // Make sure to clear the redo buffer if you apply a new command, to preserve the time continuum.
        m_redo_buffer.Clear();
        ++m_revision;
    }

    // push an already-created command to the undo stack
    static void Push(std::shared_ptr<Command> command) {
        m_undo_buffer.Push(command);
        m_redo_buffer.Clear();
        ++m_revision;
    }

    static uint64_t Revision() { return m_revision; }
    static uint64_t Epoch() { return m_epoch; }
    static void Clear() { m_undo_buffer.Clear(); m_redo_buffer.Clear(); ++m_revision; ++m_epoch; }

    /** Undoes the latest applied command */
    static void Undo();

    /** Redoes the latest undone command */
    static void Redo();

protected:
    inline static uint64_t m_revision = 0;
    inline static uint64_t m_epoch = 0;
    static spartan::CircularStack<std::shared_ptr<Command>> m_undo_buffer;
    static spartan::CircularStack<std::shared_ptr<Command>> m_redo_buffer;
};
}
