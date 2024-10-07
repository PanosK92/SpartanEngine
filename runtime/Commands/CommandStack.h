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

#pragma once

//= INCLUDES ===================
#include "Definitions.h"
#include "../Commands/Command.h"
#include "../Commands/CircularStack.h"
//==============================

namespace Spartan {
// @todo make editor setting instead of compile time constant expression
constexpr uint64_t max_undo_steps = 128;

class SP_CLASS CommandStack {
public:
    template<typename CommandType, typename... Args>
    static void Add(Args&&... args) {
        // @todo this is garbage for performance, as it has to copy the entire buffer when it's full
        // could be solved by using linked lists instead of dynamic arrays (vectors)
        // optimal solution may be to preallocate an array instead, and use a cursor to manage undo/redo <-- probably do this
        // luckily we only store pointers so should be decent performance for now (as long as max_undo_steps doesn't grow too large)
        //
        // CircularStack author: not sure I fully made optimal solution
        // I suppose we can store it all in single stack, I will look into it

        std::shared_ptr<Command> new_command = std::make_shared<CommandType>(std::forward<Args>(args)...);
        m_undo_buffer.Push(new_command);

        // Make sure to clear the redo buffer if you apply a new command, to preserve the time continuum.
        m_redo_buffer.Clear();
    }

    /** Undoes the latest applied command */
    static void Undo();

    /** Redoes the latest undone command */
    static void Redo();

protected:
    static Spartan::CircularStack<std::shared_ptr<Command>> m_undo_buffer;
    static Spartan::CircularStack<std::shared_ptr<Command>> m_redo_buffer;
};
}
