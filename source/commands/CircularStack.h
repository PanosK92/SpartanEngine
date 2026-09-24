/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include <optional>
//==============================

namespace spartan
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
        if (items_count == 0) return std::nullopt;

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
