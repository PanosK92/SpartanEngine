/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ====================
#include <variant>
#include <cstdint>
#include <string_view>
#include <utility>
#include <optional>
#include <unordered_map>
//===============================


namespace spartan
{
    // Allowed types of console variables, there really should never need to be any more than these.
    using CVarVariant = std::variant<int32_t, float, bool, std::string>;

    template<typename T, typename Variant>
    struct IsVariantMember;

    template<typename T, typename... Types>
    struct IsVariantMember<T, std::variant<Types...>>  : std::disjunction<std::is_same<T, Types>...> {};

    namespace Concept
    {
        template<typename T>
        concept TIsConsoleVar = IsVariantMember<T, CVarVariant>::value;
    }

    /**
     * Represents a console variable to be used by the console system.
     */
    struct ConsoleVariable
    {
        /** Name of the variable for searching */
        std::string_view    m_name;

        /** A display hint */
        std::string_view    m_hint;

        /** Internal value of this variable */
        CVarVariant*      m_value_ptr;

        /** Default value of this variable */
        CVarVariant       m_default_value;

        /** Callback for when this variable has been changed from it's previous value */
        void (*m_on_change)(const CVarVariant&);

        constexpr ConsoleVariable(std::string_view name, std::string_view hint, CVarVariant* value_ptr,
                                  CVarVariant _default, void (*callback)(const CVarVariant&) = nullptr)
            : m_name(name)
            , m_hint(hint)
            , m_value_ptr(value_ptr)
            , m_default_value(std::move(_default))
            , m_on_change(callback)
        {}
    };

    /**
     * Holds all registered console variables and includes utilities for setting and getting by string-value.
     * */
    class ConsoleRegistry
    {
    public:

        using ConsoleContainer = std::unordered_map<std::string_view, ConsoleVariable>;

        [[nodiscard]] static ConsoleRegistry& Get();

        void Register(const ConsoleVariable& var);

        ConsoleVariable* Find(std::string_view name);

        // Will assert of the requested variable is not of type T.
        template<Concept::TIsConsoleVar T>
        [[nodiscard]] const T& GetAs(std::string_view name)
        {
            return std::get<T>(*Find(name)->m_value_ptr);
        }

        // Try to return the console variable as the found type.
        template<Concept::TIsConsoleVar T>
        [[nodiscard]] const T* TryGetAs(std::string_view name)
        {
            auto It = Find(name)->m_value_ptr;
            if (It == nullptr)
            {
                return nullptr;
            }

            if (auto* Ptr = std::get_if<T>(It))
            {
                return Ptr;
            }

            return nullptr;
        }

        [[nodiscard]] const ConsoleContainer& GetAll() const;

        bool SetValueFromString(std::string_view target_name, std::string_view string_value);
        [[nodiscard]] std::optional<std::string> GetValueAsString(std::string_view variable_name);

    private:

        ConsoleContainer m_console_variables;
    };

    /**
     * An auto-registration helper for console variables, all console variables must be registered in one translation unit (a CPP file), in order to be auto-detected
     * for registration. Please see examples at the bottom of this file.
     * @tparam T Type of console variable to deduce.
     */
    template<Concept::TIsConsoleVar T>
    class TConsoleVar
    {
    public:

        // A default placeholder callback
        static void DefaultCallback(const CVarVariant&) { /** Intentionally empty */ }

        constexpr TConsoleVar(std::string_view name, T default_value, std::string_view hint, void(*callback)(const CVarVariant&) = DefaultCallback)
            : m_storage(default_value)
        {
            ConsoleVariable Var(name, hint, &m_storage, m_storage, callback);
            ConsoleRegistry::Get().Register(Var);
        }

        /** Returns the expected value of the type, will assert if not correct type */
        T GetValue() const
        {
            return std::get<T>(m_storage);
        }

        /** Tries to return the expected value from the type, will return nullptr if type is not found. */
        T* GetValuePtr() const
        {
            return std::get_if<T>(&m_storage);
        }

        explicit operator bool() const
        {
            return GetValue();
        }

    private:

        /** Internal storage of this type */
        CVarVariant m_storage;
    };
}


/*
 * Example console variables that will automatically register with the system when placed in a CPP file.
 *
    namespace tests
    {
        static TConsoleVar CVarTestConsoleVar_Int("console.test.int", 12, "int test console var");
        static TConsoleVar CVarTestConsoleVar_Bool("console.test.bool", false, "bool test console var");
        static TConsoleVar CVarTestConsoleVar_Float("console.test.float", 12.0f, "float test console var");
        static TConsoleVar<std::string> CVarTestConsoleVar_String("console.test.string", "SpartanIsCool!", "string test console var");
    }
**/


