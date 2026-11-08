/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include <sol/sol.hpp>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "VisualApi.h"
//==========================

// drop in replacements for the sol2 registration calls, each records the c++ signature for the node graph and then
// registers exactly what sol2 would have
//
//   visual::usertype<Entity>(state, "Entity", "GetName", &Entity::GetObjectName, ...)   instead of state.new_usertype<Entity>(...)
//   visual::enumeration(state, "KeyCode", "W", KeyCode::W, ...)                          instead of state.new_enum(...)
//   visual::table table(state, "World"); table["GetName"] = &World::GetName;            instead of state.create_named_table(...)
//   visual::usertype_extension<Physics> type(state, "Physics"); type["X"] = ...;        instead of sol::usertype<Physics> type = state["Physics"]
namespace spartan::scripting::visual
{
    namespace reflection
    {
        // signatures
        template <typename F>
        struct function_traits
        {
            static constexpr bool known = false;
        };

        template <typename R, typename... A>
        struct function_traits<R (*)(A...)>
        {
            static constexpr bool known  = true;
            static constexpr bool member = false;
            using object                 = void;
            using result                 = R;
            using arguments              = std::tuple<A...>;
        };

        template <typename R, typename... A>
        struct function_traits<R (*)(A...) noexcept> : function_traits<R (*)(A...)>
        {
        };

        template <typename R, typename... A>
        struct function_traits<R(A...)> : function_traits<R (*)(A...)>
        {
        };

        template <typename R, typename C, typename... A>
        struct function_traits<R (C::*)(A...)>
        {
            static constexpr bool known  = true;
            static constexpr bool member = true;
            using object                 = C;
            using result                 = R;
            using arguments              = std::tuple<A...>;
        };

        template <typename R, typename C, typename... A>
        struct function_traits<R (C::*)(A...) const> : function_traits<R (C::*)(A...)>
        {
        };

        template <typename R, typename C, typename... A>
        struct function_traits<R (C::*)(A...) noexcept> : function_traits<R (C::*)(A...)>
        {
        };

        template <typename R, typename C, typename... A>
        struct function_traits<R (C::*)(A...) const noexcept> : function_traits<R (C::*)(A...)>
        {
        };

        // a lambda's call operator is a member of the closure, lua sees a free function
        template <typename F>
        struct call_operator_traits
        {
            static constexpr bool known = false;
        };

        template <typename R, typename L, typename... A>
        struct call_operator_traits<R (L::*)(A...)>
        {
            static constexpr bool known  = true;
            static constexpr bool member = false;
            using object                 = void;
            using result                 = R;
            using arguments              = std::tuple<A...>;
        };

        template <typename R, typename L, typename... A>
        struct call_operator_traits<R (L::*)(A...) const> : call_operator_traits<R (L::*)(A...)>
        {
        };

        template <typename R, typename L, typename... A>
        struct call_operator_traits<R (L::*)(A...) noexcept> : call_operator_traits<R (L::*)(A...)>
        {
        };

        template <typename R, typename L, typename... A>
        struct call_operator_traits<R (L::*)(A...) const noexcept> : call_operator_traits<R (L::*)(A...)>
        {
        };

        template <typename F, typename = void>
        struct callable_traits : function_traits<std::decay_t<F>>
        {
        };

        template <typename F>
        struct callable_traits<F, std::void_t<decltype(&std::decay_t<F>::operator())>> : call_operator_traits<decltype(&std::decay_t<F>::operator())>
        {
        };

        // type classification
        template <typename T, template <typename...> class Template>
        struct is_instance : std::false_type
        {
        };

        template <template <typename...> class Template, typename... A>
        struct is_instance<Template<A...>, Template> : std::true_type
        {
        };

        template <typename T>
        struct is_std_array : std::false_type
        {
        };

        template <typename T, size_t N>
        struct is_std_array<std::array<T, N>> : std::true_type
        {
        };

        template <typename T>
        constexpr bool is_string_like = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char*> || std::is_same_v<T, char*> ||
                                        (std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>);

        template <typename T>
        constexpr bool is_lua_table = std::is_same_v<T, sol::table> || std::is_same_v<T, sol::lua_table> || std::is_same_v<T, sol::global_table> || is_instance<T, std::vector>::value ||
                                      is_instance<T, std::map>::value || is_instance<T, std::unordered_map>::value || is_std_array<T>::value;

        template <typename T>
        constexpr bool is_lua_function = std::is_same_v<T, sol::function> || std::is_same_v<T, sol::protected_function> || std::is_same_v<T, sol::unsafe_function>;

        template <typename T>
        constexpr bool is_lua_any = std::is_same_v<T, sol::object> || std::is_same_v<T, sol::reference> || std::is_same_v<T, sol::lua_nil_t> || std::is_same_v<T, sol::userdata>;

        // arguments sol2 fills in itself, lua never passes them
        template <typename T>
        constexpr bool is_injected = std::is_same_v<T, sol::this_state> || std::is_same_v<T, sol::this_main_state> || std::is_same_v<T, sol::this_environment>;

        template <typename T>
        detail::TypeCapture capture_type()
        {
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            detail::TypeCapture capture;

            if constexpr (std::is_same_v<U, bool>)
            {
                capture.primitive = "boolean";
            }
            else if constexpr (std::is_enum_v<U>)
            {
                capture.cpp     = typeid(U);
                capture.is_enum = true;
            }
            else if constexpr (std::is_integral_v<U>)
            {
                capture.primitive = "integer";
            }
            else if constexpr (std::is_floating_point_v<U>)
            {
                capture.primitive = "number";
            }
            else if constexpr (is_string_like<U>)
            {
                capture.primitive = "string";
            }
            else if constexpr (std::is_pointer_v<U>)
            {
                capture          = capture_type<std::remove_pointer_t<U>>();
                capture.nullable = true;
            }
            else if constexpr (is_instance<U, std::shared_ptr>::value || is_instance<U, std::unique_ptr>::value)
            {
                capture          = capture_type<typename U::element_type>();
                capture.nullable = true;
            }
            else if constexpr (is_lua_table<U>)
            {
                capture.primitive = "table";
            }
            else if constexpr (is_lua_function<U>)
            {
                capture.primitive = "function";
            }
            else if constexpr (is_lua_any<U> || std::is_void_v<U>)
            {
                capture.primitive = "any";
            }
            else if constexpr (std::is_class_v<U>)
            {
                capture.cpp = typeid(U);
            }
            else
            {
                capture.primitive = "any";
            }

            return capture;
        }

        template <typename T>
        void capture_parameter(std::vector<detail::ParameterCapture>& out)
        {
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            if constexpr (is_injected<U>)
            {
                return;
            }
            else if constexpr (std::is_same_v<U, sol::variadic_args>)
            {
                detail::ParameterCapture parameter;
                parameter.type.primitive = "any";
                parameter.variadic       = true;
                parameter.optional       = true;
                out.push_back(parameter);
            }
            else if constexpr (is_instance<U, sol::optional>::value || is_instance<U, std::optional>::value)
            {
                detail::ParameterCapture parameter;
                parameter.type     = capture_type<typename U::value_type>();
                parameter.optional = true;
                out.push_back(parameter);
            }
            else
            {
                detail::ParameterCapture parameter;
                parameter.type = capture_type<U>();
                out.push_back(parameter);
            }
        }

        template <typename Tuple, size_t... I>
        void capture_parameters(std::vector<detail::ParameterCapture>& out, std::index_sequence<I...>)
        {
            (capture_parameter<std::tuple_element_t<I, Tuple>>(out), ...);
        }

        template <typename R>
        void capture_returns(std::vector<detail::ParameterCapture>& out)
        {
            using U = std::remove_cv_t<std::remove_reference_t<R>>;
            if constexpr (std::is_void_v<U>)
            {
                return;
            }
            else if constexpr (is_instance<U, std::tuple>::value || is_instance<U, std::pair>::value)
            {
                capture_parameters<U>(out, std::make_index_sequence<std::tuple_size_v<U>>{});
                for (detail::ParameterCapture& value : out)
                {
                    value.optional = false;
                }
            }
            else if constexpr (is_instance<U, sol::optional>::value || is_instance<U, std::optional>::value)
            {
                detail::ParameterCapture value;
                value.type          = capture_type<typename U::value_type>();
                value.type.nullable = true;
                out.push_back(value);
            }
            else
            {
                detail::ParameterCapture value;
                value.type = capture_type<U>();
                out.push_back(value);
            }
        }

        // a free callable whose first argument is the owner is a method, lua calls both with self first
        template <typename Owner, typename Tuple>
        constexpr bool first_argument_is_owner()
        {
            if constexpr (std::tuple_size_v<Tuple> == 0 || std::is_void_v<Owner>)
            {
                return false;
            }
            else
            {
                using First = std::remove_cv_t<std::remove_pointer_t<std::remove_cv_t<std::remove_reference_t<std::tuple_element_t<0, Tuple>>>>>;
                return std::is_same_v<First, Owner> || std::is_base_of_v<First, Owner>;
            }
        }

        template <typename Tuple, size_t Offset, size_t... I>
        void capture_parameters_from(std::vector<detail::ParameterCapture>& out, std::index_sequence<I...>)
        {
            (capture_parameter<std::tuple_element_t<I + Offset, Tuple>>(out), ...);
        }

        // Owner is the usertype (void for tables and globals)
        template <typename Owner, typename F>
        void record_callable(const std::string& owner, const std::string& name, ApiKind static_kind = ApiKind::Function)
        {
            using traits = callable_traits<F>;
            detail::FunctionCapture function;
            function.owner = owner;
            function.name  = name;

            if constexpr (!traits::known)
            {
                // generic lambdas and functors with overloaded call operators carry no single signature
                function.kind      = std::is_void_v<Owner> ? static_kind : ApiKind::Method;
                function.reflected = false;
                detail::ParameterCapture any;
                any.type.primitive = "any";
                any.variadic       = true;
                any.optional       = true;
                function.parameters.push_back(any);
                function.returns.push_back(any);
            }
            else
            {
                using arguments = typename traits::arguments;
                capture_returns<typename traits::result>(function.returns);

                if constexpr (traits::member)
                {
                    function.kind = ApiKind::Method;
                    capture_parameters<arguments>(function.parameters, std::make_index_sequence<std::tuple_size_v<arguments>>{});
                }
                else if constexpr (first_argument_is_owner<Owner, arguments>())
                {
                    function.kind = ApiKind::Method;
                    capture_parameters_from<arguments, 1>(function.parameters, std::make_index_sequence<std::tuple_size_v<arguments> - 1>{});
                }
                else
                {
                    function.kind = static_kind;
                    capture_parameters<arguments>(function.parameters, std::make_index_sequence<std::tuple_size_v<arguments>>{});
                }
            }

            detail::add_function(std::move(function));
        }

        template <typename Owner, typename... A>
        void record_constructor_types(const std::string& owner, sol::types<A...>);

        // a constructor signature from sol::constructors, Vector3(float, float, float)
        template <typename Owner, typename Signature>
        void record_constructor(const std::string& owner)
        {
            if constexpr (std::is_function_v<Signature>)
            {
                using traits    = function_traits<Signature>;
                using arguments = typename traits::arguments;
                detail::FunctionCapture function;
                function.owner = owner;
                function.name  = owner;
                function.kind  = ApiKind::Constructor;
                capture_parameters<arguments>(function.parameters, std::make_index_sequence<std::tuple_size_v<arguments>>{});
                capture_returns<Owner>(function.returns);
                detail::add_function(std::move(function));
            }
            else if constexpr (is_instance<Signature, sol::types>::value)
            {
                record_constructor_types<Owner>(owner, Signature{});
            }
        }

        template <typename Owner, typename... A>
        void record_constructor_types(const std::string& owner, sol::types<A...>)
        {
            record_constructor<Owner, Owner(A...)>(owner);
        }

        template <typename Owner, typename... Signatures>
        void record_constructors(const std::string& owner, const sol::constructor_list<Signatures...>&)
        {
            (record_constructor<Owner, Signatures>(owner), ...);
        }

        inline const char* operator_symbol(sol::meta_function function)
        {
            switch (function)
            {
                case sol::meta_function::addition:              return "+";
                case sol::meta_function::subtraction:           return "-";
                case sol::meta_function::multiplication:        return "*";
                case sol::meta_function::division:              return "/";
                case sol::meta_function::modulus:               return "%";
                case sol::meta_function::power_of:              return "^";
                case sol::meta_function::concatenation:         return "..";
                case sol::meta_function::equal_to:              return "==";
                case sol::meta_function::less_than:             return "<";
                case sol::meta_function::less_than_or_equal_to: return "<=";
                case sol::meta_function::unary_minus:           return "unm";
                default:                                        return nullptr;
            }
        }

        template <typename Owner, typename F>
        void record_operator(const std::string& owner, const char* symbol)
        {
            using traits = callable_traits<F>;
            if constexpr (traits::known)
            {
                using arguments = typename traits::arguments;
                detail::FunctionCapture function;
                function.owner = owner;
                function.name  = symbol;
                function.kind  = ApiKind::Operator;
                function.op    = symbol;
                capture_parameters<arguments>(function.parameters, std::make_index_sequence<std::tuple_size_v<arguments>>{});
                capture_returns<typename traits::result>(function.returns);
                detail::add_function(std::move(function));
            }
        }

        template <typename Owner, typename... F>
        void record_operator_overloads(const std::string& owner, const char* symbol, const sol::overload_set<F...>&)
        {
            (record_operator<Owner, F>(owner, symbol), ...);
        }

        template <typename Owner, typename V>
        void record_meta(const std::string& owner, sol::meta_function key, const V& value)
        {
            const char* symbol = operator_symbol(key);
            if (!symbol)
            {
                // index, new_index, to_string and length are how lua reads the type, not calls a graph makes
                return;
            }

            if constexpr (is_instance<V, sol::overload_set>::value)
            {
                record_operator_overloads<Owner>(owner, symbol, value);
            }
            else
            {
                record_operator<Owner, V>(owner, symbol);
            }
        }

        template <typename Owner, typename... F>
        void record_overloads(const std::string& owner, const std::string& name, const sol::overload_set<F...>&)
        {
            (record_callable<Owner, F>(owner, name), ...);
        }

        template <typename Owner, typename... F>
        void record_factories(const std::string& owner, const std::string& name, const sol::factory_wrapper<F...>&)
        {
            (record_callable<void, F>(owner, name, ApiKind::Function), ...);
        }

        // one name = value pair of a usertype or table
        template <typename Owner, typename V>
        void record_named(const std::string& owner, const std::string& name, const V& value)
        {
            using U = std::decay_t<V>;
            if constexpr (std::is_member_object_pointer_v<U>)
            {
                using Field = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Owner&>().*value)>>;

                detail::FunctionCapture get;
                get.owner = owner;
                get.name  = name;
                get.kind  = ApiKind::PropertyGet;
                capture_returns<Field>(get.returns);
                detail::add_function(std::move(get));

                detail::FunctionCapture set;
                set.owner = owner;
                set.name  = name;
                set.kind  = ApiKind::PropertySet;
                capture_parameter<Field>(set.parameters);
                detail::add_function(std::move(set));
            }
            else if constexpr (is_instance<U, sol::var_wrapper>::value)
            {
                detail::FunctionCapture constant;
                constant.owner = owner;
                constant.name  = name;
                constant.kind  = ApiKind::Constant;
                capture_returns<decltype(value.value())>(constant.returns);
                detail::add_function(std::move(constant));
            }
            else if constexpr (is_instance<U, sol::overload_set>::value)
            {
                record_overloads<Owner>(owner, name, value);
            }
            else if constexpr (is_instance<U, sol::factory_wrapper>::value)
            {
                record_factories<Owner>(owner, name, value);
            }
            else if constexpr (is_instance<U, sol::constructor_list>::value)
            {
                record_constructors<Owner>(owner, value);
            }
            else if constexpr (std::is_enum_v<U> || std::is_arithmetic_v<U> || is_string_like<U>)
            {
                detail::FunctionCapture constant;
                constant.owner = owner;
                constant.name  = name;
                constant.kind  = ApiKind::Constant;
                capture_returns<U>(constant.returns);
                detail::add_function(std::move(constant));
            }
            else
            {
                record_callable<Owner, U>(owner, name);
            }
        }

        template <typename... B>
        void record_bases(const std::string& owner, const sol::bases<B...>&)
        {
            (detail::add_base(owner, typeid(B)), ...);
        }

        template <typename T>
        void record_bases(const std::string&, const T&)
        {
        }

        template <typename Owner>
        void record_entries(const std::string&)
        {
        }

        template <typename Owner, typename K, typename V, typename... Rest>
        void record_entries(const std::string& owner, const K& key, const V& value, const Rest&... rest)
        {
            using KU = std::decay_t<K>;
            using VU = std::decay_t<V>;
            if constexpr (std::is_same_v<KU, sol::meta_function>)
            {
                record_meta<Owner>(owner, key, value);
            }
            else if constexpr (std::is_same_v<KU, sol::call_construction>)
            {
                if constexpr (is_instance<VU, sol::constructor_list>::value)
                {
                    record_constructors<Owner>(owner, value);
                }
                else if constexpr (is_instance<VU, sol::factory_wrapper>::value)
                {
                    record_factories<Owner>(owner, owner, value);
                }
            }
            else if constexpr (std::is_same_v<KU, sol::base_classes_tag>)
            {
                record_bases(owner, value);
            }
            else if constexpr (is_string_like<KU> || std::is_same_v<KU, const char*>)
            {
                record_named<Owner>(owner, std::string(key), value);
            }

            record_entries<Owner>(owner, rest...);
        }

        template <typename E>
        int64_t enum_value(const E& value)
        {
            if constexpr (std::is_enum_v<E>)
            {
                return static_cast<int64_t>(static_cast<std::underlying_type_t<E>>(value));
            }
            else
            {
                return static_cast<int64_t>(value);
            }
        }

        inline void collect_enum(std::vector<std::pair<std::string, int64_t>>&, std::type_index&)
        {
        }

        template <typename K, typename E, typename... Rest>
        void collect_enum(std::vector<std::pair<std::string, int64_t>>& values, std::type_index& cpp, const K& key, const E& value, const Rest&... rest)
        {
            if constexpr (std::is_enum_v<std::decay_t<E>>)
            {
                cpp = typeid(std::decay_t<E>);
            }
            values.emplace_back(std::string(key), enum_value(value));
            collect_enum(values, cpp, rest...);
        }
    }

    template <typename T, typename... Args>
    sol::usertype<T> usertype(sol::state_view state, const char* name, Args&&... args)
    {
        static_assert(sizeof...(Args) % 2 == 0, "usertype entries come in name, value pairs");
        detail::add_owner(name, true, typeid(T));
        reflection::record_entries<T>(name, args...);
        return state.new_usertype<T>(name, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void enumeration(sol::state_view state, const char* name, Args&&... args)
    {
        static_assert(sizeof...(Args) % 2 == 0, "enum entries come in name, value pairs");
        std::vector<std::pair<std::string, int64_t>> values;
        std::type_index cpp = typeid(void);
        reflection::collect_enum(values, cpp, args...);
        detail::add_enum(name, cpp, std::move(values));
        state.new_enum(name, std::forward<Args>(args)...);
    }

    // a named lua table whose functions are recorded as they are assigned
    class table
    {
    public:
        table(sol::state_view state, const char* name) : m_name(name), m_table(state.create_named_table(name))
        {
            detail::add_owner(m_name, false, typeid(void));
        }

        class entry
        {
        public:
            entry(table& owner, const char* key) : m_owner(owner), m_key(key)
            {
            }

            template <typename V>
            entry& operator=(V&& value)
            {
                reflection::record_named<void>(m_owner.m_name, m_key, value);
                m_owner.m_table[m_key] = std::forward<V>(value);
                return *this;
            }

        private:
            table& m_owner;
            const char* m_key;
        };

        entry operator[](const char* key)
        {
            return entry(*this, key);
        }

        sol::table& get()
        {
            return m_table;
        }

    private:
        std::string m_name;
        sol::table m_table;
    };

    // adds to a usertype that was registered elsewhere
    template <typename T>
    class usertype_extension
    {
    public:
        usertype_extension(sol::state_view state, const char* name) : m_name(name), m_type(make_type(state, name))
        {
        }

        class entry
        {
        public:
            entry(usertype_extension& owner, const char* key) : m_owner(owner), m_key(key)
            {
            }

            template <typename V>
            entry& operator=(V&& value)
            {
                reflection::record_named<T>(m_owner.m_name, m_key, value);
                m_owner.m_type[m_key] = std::forward<V>(value);
                return *this;
            }

        private:
            usertype_extension& m_owner;
            const char* m_key;
        };

        entry operator[](const char* key)
        {
            return entry(*this, key);
        }

    private:
        static sol::usertype<T> make_type(sol::state_view state, const char* name)
        {
            sol::usertype<T> type = state[name];
            return type;
        }

        std::string m_name;
        sol::usertype<T> m_type;
    };

    // a global function, print and friends
    template <typename F>
    void global_function(sol::state_view state, const char* name, F&& function)
    {
        reflection::record_callable<void, std::decay_t<F>>("", name);
        state.set_function(name, std::forward<F>(function));
    }
}
