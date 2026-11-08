/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include <cstdint>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>
#include <sol/forward.hpp>
//=====================

// the lua api as the node graph sees it
//
// every binding in source/scripting registers through VisualApiReflection.h, which records the c++ signature next to
// the sol2 call, so the node library is the exposed lua api by construction rather than a list kept in sync by hand.
// types are lua names: a primitive ("boolean", "number", "integer", "string", "table", "function", "any") or the name
// a usertype or enum was registered under ("Vector3", "Entity", "KeyCode")
namespace spartan::scripting::visual
{
    enum class ApiKind : uint8_t
    {
        Function,    // Owner.Name(...), or Name(...) for a global
        Method,      // self:Name(...)
        Constructor, // Owner(...)
        PropertyGet, // self.name
        PropertySet, // self.name = value
        Constant,    // Owner.Name
        Operator     // a usertype metamethod, lhs op rhs or op value
    };

    struct ApiParameter
    {
        std::string name;
        std::string type;
        bool optional = false;
        bool variadic = false;
        bool nullable = false; // a c++ pointer, nil reaches it as nullptr
    };

    struct ApiFunction
    {
        std::string owner;
        std::string name;
        ApiKind kind            = ApiKind::Function;
        std::string op;         // operators only: "+", "-", "*", "/", "==", "<", "<=", "..", "unm"
        uint32_t overload       = 0;
        uint32_t overload_count = 1;
        std::vector<ApiParameter> parameters; // self is not part of them
        std::vector<ApiParameter> returns;
        bool reflected          = true;       // false for a lua function found without a recorded signature

        // stable across runs, the graph files reference functions by it
        std::string id() const;
    };

    struct ApiEnum
    {
        std::string name;
        std::vector<std::pair<std::string, int64_t>> values;
    };

    struct ApiOwner
    {
        std::string name;
        bool is_usertype = false;
        std::vector<std::string> bases;
    };

    struct ApiCoverage
    {
        uint32_t functions_reflected   = 0;
        uint32_t functions_unreflected = 0;
        uint32_t usertypes             = 0;
        uint32_t tables                = 0;
        uint32_t enums                 = 0;
        // owner.name of every lua function that has no recorded signature, they still get untyped nodes
        std::vector<std::string> unreflected;
    };

    const std::vector<ApiFunction>& get_functions();
    const std::vector<ApiEnum>& get_enums();
    const std::vector<ApiOwner>& get_owners();
    const ApiFunction* find_function(const std::string& id);
    const ApiEnum* find_enum(const std::string& name);
    const ApiOwner* find_owner(const std::string& name);
    bool is_usertype(const std::string& type);
    bool is_enum(const std::string& type);
    // true when a value of type can be passed where base is expected through the usertype's sol::bases
    bool derives_from(const std::string& type, const std::string& base);
    const ApiCoverage& get_coverage();
    // bumps whenever the registry changes, caches built from it compare against it
    uint64_t get_revision();

    // registration side, VisualApiReflection.h fills these while the bindings register
    namespace detail
    {
        struct TypeCapture
        {
            const char* primitive = nullptr;
            std::type_index cpp   = typeid(void);
            bool is_enum          = false;
            bool nullable         = false;
        };

        struct ParameterCapture
        {
            TypeCapture type;
            bool optional = false;
            bool variadic = false;
        };

        struct FunctionCapture
        {
            std::string owner;
            std::string name;
            ApiKind kind = ApiKind::Function;
            std::string op;
            std::vector<ParameterCapture> parameters;
            std::vector<ParameterCapture> returns;
            bool reflected = true;
        };

        void add_owner(const std::string& name, bool is_usertype, std::type_index cpp);
        void add_base(const std::string& owner, std::type_index base);
        void add_function(FunctionCapture&& function);
        void add_enum(const std::string& name, std::type_index cpp, std::vector<std::pair<std::string, int64_t>>&& values);
        void add_enum_value(const std::string& name, const std::string& key, int64_t value);

        // forgets everything, the lua state is about to be registered again
        void reset();
        // resolves captured c++ types into lua names and scans the lua state for anything registered around the recorder
        void finalize(sol::state_view state);
    }
}
