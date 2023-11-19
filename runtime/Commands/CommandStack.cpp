/*
Copyright(c) 2016-2023 Fredrik Svantesson

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

//= INCLUDES ==========================
#include "pch.h"
#include "CommandStack.h"
//=====================================


namespace Spartan
{

    void CommandStack::Undo()
    {
        if (m_undo_buffer.size() == 0)
            return;

        // Fetch the latest applied command, so we know what we are undoing
        std::shared_ptr<Command> undo_command = m_undo_buffer.back();
        m_undo_buffer.pop_back();

        // Actually undo
        undo_command->OnRevert();

        // Push it to the top of the redo stack
        m_redo_buffer.push_back(undo_command);
    }

    void CommandStack::Redo()
    {
        if (m_redo_buffer.size() == 0)
            return;

        // Fetch the latest undoed command, so we know what we are redoing
        std::shared_ptr<Command> redo_command = m_redo_buffer.back();
        m_redo_buffer.pop_back();

        // Actually redo
        redo_command->OnApply();

        // Push it to the top of the undo stack
        m_undo_buffer.push_back(redo_command);
    }

    std::vector<std::shared_ptr<Spartan::Command>> CommandStack::m_undo_buffer;

    std::vector<std::shared_ptr<Spartan::Command>> CommandStack::m_redo_buffer;

}
