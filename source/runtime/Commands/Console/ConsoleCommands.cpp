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

//= INCLUDES ========================
#include "pch.h"
#include "ConsoleCommands.h"
//===================================


namespace spartan
{
    namespace
    {
        template<typename T>
        requires(std::is_same_v<T, int32_t>)
        std::optional<T> ParseValue(std::string_view value)
        {
            char* End = nullptr;
            long long Value = std::strtoll(value.data(), &End, 10);
            if (End != value.data() && Value >= std::numeric_limits<T>::min() && Value <= std::numeric_limits<T>::max())
            {
                return static_cast<T>(Value);
            }
            return std::nullopt;
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        std::optional<T> ParseValue(std::string_view value)
        {
            char* End = nullptr;
            T var_value = static_cast<T>(std::strtod(value.data(), &End));
            if (End != value.data())
            {
                return var_value;
            }
            return std::nullopt;
        }

        template<typename T>
        requires(std::is_same_v<T, bool>)
        std::optional<T> ParseValue(std::string_view value)
        {
            if (value == "true" || value == "1" || value == "True" || value == "TRUE")
            {
                return true;
            }
            if (value == "false" || value == "0" || value == "False" || value == "FALSE")
            {
                return false;
            }

            return std::nullopt;
        }

        template<typename T>
        requires(std::is_same_v<T, std::string> || std::is_convertible_v<T, std::string>)
        std::optional<T> ParseValue(std::string_view value)
        {
            return std::string(value.begin(), value.end());
        }
    }

    ConsoleRegistry& ConsoleRegistry::Get()
    {
        static ConsoleRegistry s_registry;
        return s_registry;
    }

    void ConsoleRegistry::Register(const ConsoleVariable& var)
    {
        std::string_view VarName = var.m_name;

        assert(!m_console_variables.contains(VarName));

        m_console_variables.emplace(VarName, var);
    }

    ConsoleVariable* ConsoleRegistry::Find(std::string_view name)
    {
        auto It = m_console_variables.find(name);
        return It != m_console_variables.end() ? &It->second : nullptr;
    }

    const ConsoleRegistry::ConsoleContainer& ConsoleRegistry::GetAll() const
    {
        return m_console_variables;
    }

    bool ConsoleRegistry::SetValueFromString(std::string_view target_name, std::string_view string_value)
    {
        ConsoleVariable* ConsoleVar = Find(target_name);
        if (ConsoleVar == nullptr)
        {
            return false;
        }

        bool bSuccess = std::visit([&]<typename T0>(T0&&) -> bool
        {
            using T = std::decay_t<T0>;

            std::optional<T> parsed_value = ParseValue<T>(string_value);
            if (parsed_value.has_value())
            {
                *(ConsoleVar->m_value_ptr) = *parsed_value;
                return true;
            }

            return false;
        }, *(ConsoleVar->m_value_ptr));

        if (bSuccess && ConsoleVar->m_on_change)
        {
            ConsoleVar->m_on_change(*(ConsoleVar->m_value_ptr));
        }

        return bSuccess;
    }

    std::optional<std::string> ConsoleRegistry::GetValueAsString(std::string_view variable_name)
    {
        ConsoleVariable* console_variable = Find(variable_name);
        if (console_variable == nullptr)
        {
            return std::nullopt;
        }

        std::string Result;

        bool bSuccess = std::visit([&]<typename T0>(T0&& Value) -> bool
        {
            using T = std::decay_t<T0>;

            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
            {
                Result = std::to_string(std::forward<T0>(Value));
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                Result = Value ? "true" : "false";
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                Result = Value;
            }
            else
            {
                return false;
            }

            return true;
        }, *(console_variable->m_value_ptr));

        if (!bSuccess)
        {
            return std::nullopt;
        }

        return Result;
    }
}



