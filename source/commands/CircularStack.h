/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace spartan
{
    template <typename T>
    class CircularStack
    {
    public:
        explicit CircularStack(size_t capacity) : buffer(capacity) { assert(capacity > 0); }
        CircularStack(const CircularStack&) = delete;
        CircularStack& operator=(const CircularStack&) = delete;

        void Push(T item)
        {
            assert(!buffer.empty());
            buffer[next] = std::move(item);
            next = (next + 1) % buffer.size();
            count = std::min(count + 1, buffer.size());
        }

        std::optional<T> Pop()
        {
            if (count == 0) return std::nullopt;
            next = (next + buffer.size() - 1) % buffer.size();
            T item = std::move(buffer[next]);
            buffer[next] = T{};
            --count;
            return item;
        }

        void Clear()
        {
            // Release commands and their resources before the graphics device shuts down.
            for (T& item : buffer) item = T{};
            next = count = 0;
        }

    private:
        std::vector<T> buffer;
        size_t next = 0;
        size_t count = 0;
    };
}
