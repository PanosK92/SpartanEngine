/*
Copyright(c) 2015-2025 Panos Karabelas, George Mavroeidis

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

//= INCLUDES =================================
#include "Renderer_Definitions.h"
//============================================

namespace spartan
{
    template<typename E>
    constexpr auto enum_range()
    {
        return std::pair{E{0}, E::MAX};
    }

    template <typename T>
    struct hash
    {
        std::enable_if_t<std::is_enum_v<T>, std::size_t>
        operator()(T t) const noexcept
        {
            return static_cast<std::size_t>(t);
        }
    };

    using RenderOptionType = std::variant<bool, int, float, uint32_t>;

    /*
     * A class that encompasses all the rendering options settings.
     * Option settings can be a mix of different global rendering properties
     * or component related data, which include post-process, camera, world,
     * weather, output, debugging and more.
     * This is a scalable system that can work for different parameter collection
     * methods.
     */
    class RenderOptions
    {
    public:
        RenderOptions();
        RenderOptions(const std::map<Renderer_Option, RenderOptionType>& options);
        RenderOptions(RenderOptions& other); // Move version
        ~RenderOptions() = default;

        // Options map
        std::map<Renderer_Option, RenderOptionType> GetOptions() const { return m_options;}
        void SetOptions(const std::map<Renderer_Option, RenderOptionType>& options) { m_options = options; }

        template<typename T>
        T GetOption(Renderer_Option option)
        {
            auto it = GetOptions().find(option);
            return (it != GetOptions().end() && std::holds_alternative<T>(it->second)) ? std::get<T>(it->second) : T{};
        }
        void SetOption(Renderer_Option option, const RenderOptionType& value);

        bool operator!=(const RenderOptions& other) const;

        static std::string EnumToString(Renderer_Option option); // used most likely for editor-related applications
        static Renderer_Option StringToEnum(const std::string& name);
    private:
        std::map<Renderer_Option, RenderOptionType> m_options;
    };
}
