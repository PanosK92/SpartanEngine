/*
Copyright(c) 2024 Roman Koshchei

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
#include <optional>
//==============================

namespace Spartan
{
    template <typename T>
    class CircularStack {
    private:
        uint64_t top_item_index;
        uint64_t items_count;

        uint64_t buffer_capacity;
        T* buffer_start;

    public:
        CircularStack(uint64_t capacity);
        ~CircularStack();

        void Push(T item);
        std::optional<T> Pop();
        void Clear();
    };

    template <typename T>
    CircularStack<T>::CircularStack(uint64_t capacity): buffer_capacity(capacity), items_count(0) {
        this->buffer_start = new T[capacity];
        this->top_item_index = capacity - 1;
    }

    template <typename T>
    CircularStack<T>::~CircularStack() {
        delete[] buffer_start;
    }

    template <typename T>
    void CircularStack<T>::Push(T item) {
        top_item_index += 1;
        if (top_item_index == buffer_capacity) {
            top_item_index = 0;
        }

        buffer_start[top_item_index] = item;

        if (items_count < buffer_capacity) {
            items_count += 1;
        }
    }

    template <typename T>
    std::optional<T> CircularStack<T>::Pop() {
        if (items_count == 0) {
            return std::nullopt;
        }

        T item = buffer_start[top_item_index];

        if (top_item_index == 0) {
            top_item_index = buffer_capacity - 1;
        }
        else {
            top_item_index -= 1;
        }

        items_count -= 1;

        return item;
    }

    template <typename T>
    void CircularStack<T>::Clear() {
        this->items_count = 0;
        this->top_item_index = this->buffer_capacity - 1;
    }
};
