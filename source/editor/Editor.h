/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ==============
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "widgets/Widget.h"
//=========================

struct ImFont;

class Editor
{
public:
    Editor(const std::vector<std::string>& args);
    ~Editor();

    void Tick();

    template<typename T>
    T* GetWidget() const
    {
        const auto it = m_widget_lookup.find(std::type_index(typeid(T)));
        if (it == m_widget_lookup.end())
        {
            return nullptr;
        }

        return static_cast<T*>(it->second);
    }

    template<typename TFn>
    void ForEachWidget(TFn&& fn) const
    {
        for (const auto& widget : m_widgets)
        {
            fn(widget.get());
        }
    }

    inline static ImFont* font_normal = nullptr;
    inline static ImFont* font_bold   = nullptr;

private:
    template<typename T>
    T* AddWidget()
    {
        std::unique_ptr<T> widget = std::make_unique<T>(this);
        T* raw = widget.get();
        m_widget_lookup[std::type_index(typeid(T))] = raw;
        m_widgets.push_back(std::move(widget));
        return raw;
    }

    void RegisterWidgets();

    std::vector<std::unique_ptr<Widget>> m_widgets;
    std::unordered_map<std::type_index, Widget*> m_widget_lookup;
};
