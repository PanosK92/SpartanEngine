/*
Copyright(c) 2023 Fredrik Svantesson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ============
#include "pch.h"
#include "CommandStack.h"
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Spartan::CircularStack<shared_ptr<Command>> CommandStack::m_undo_buffer = Spartan::CircularStack<shared_ptr<Command>>(Spartan::max_undo_steps);
    Spartan::CircularStack<shared_ptr<Command>> CommandStack::m_redo_buffer = Spartan::CircularStack<shared_ptr<Command>>(Spartan::max_undo_steps);

    void CommandStack::Undo()
    {
        // fetch
        optional<shared_ptr<Command>> optional_undo_command = m_undo_buffer.Pop();
        if (!optional_undo_command.has_value()) {
            return;
        }
        shared_ptr<Command> undo_command = optional_undo_command.value();

        // undo
        undo_command->OnRevert();

        // push it to the top of the redo stack
        m_redo_buffer.Push(undo_command);
    }

    void CommandStack::Redo()
    {
        // fetch
        optional<shared_ptr<Command>> optional_redo_command = m_redo_buffer.Pop();
        if (!optional_redo_command.has_value()) {
            return;
        }
        shared_ptr<Command> redo_command = optional_redo_command.value();

        // redo
        redo_command->OnApply();

        // push it to the top of the undo stack
        m_undo_buffer.Push(redo_command);
    }

}
