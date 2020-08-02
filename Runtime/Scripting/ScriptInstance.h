/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Logging/Log.h"
//=========================

namespace Spartan
{
    struct ScriptInstance
    {
        MonoAssembly* assembly      = nullptr;
        MonoImage* image            = nullptr;
        MonoClass* klass            = nullptr;
        MonoObject* object          = nullptr;       
        MonoMethod* method_start    = nullptr;
        MonoMethod* method_update   = nullptr;

        template<class T>
        bool SetValue(T* value, const std::string& name)
        {
            if (MonoClassField* field = mono_class_get_field_from_name(klass, name.c_str()))
            {
                mono_field_set_value(object, field, value);
                return true;
            }

            LOG_ERROR("Failed to set value for field %s", name.c_str());
            return false;
        }
    };
}
