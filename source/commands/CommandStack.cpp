/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============
#include "pch.h"
#include "CommandStack.h"
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    spartan::CircularStack<shared_ptr<Command>> CommandStack::m_undo_buffer = spartan::CircularStack<shared_ptr<Command>>(spartan::max_undo_steps);
    spartan::CircularStack<shared_ptr<Command>> CommandStack::m_redo_buffer = spartan::CircularStack<shared_ptr<Command>>(spartan::max_undo_steps);

    void CommandStack::Undo()
    {
        // fetch
        optional<shared_ptr<Command>> optional_undo_command = m_undo_buffer.Pop();
        if (!optional_undo_command.has_value()) return;
        shared_ptr<Command> undo_command = optional_undo_command.value();

        if (!undo_command->CanExecute()) { m_undo_buffer.Push(undo_command); SP_LOG_WARNING("Undo is waiting for terrain work to finish; retry when generation completes."); return; }
        ++m_revision;
        // undo
        undo_command->OnRevert();

        // push it to the top of the redo stack
        m_redo_buffer.Push(undo_command);
    }

    void CommandStack::Redo()
    {
        // fetch
        optional<shared_ptr<Command>> optional_redo_command = m_redo_buffer.Pop();
        if (!optional_redo_command.has_value()) return;
        shared_ptr<Command> redo_command = optional_redo_command.value();

        if (!redo_command->CanExecute()) { m_redo_buffer.Push(redo_command); SP_LOG_WARNING("Redo is waiting for terrain work to finish; retry when generation completes."); return; }
        ++m_revision;
        // redo
        redo_command->OnApply();

        // push it to the top of the undo stack
        m_undo_buffer.Push(redo_command);
    }

}
