/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =======
#include "Variant.h"
//==================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Variant& Variant::operator=(const Variant& rhs)
	{
		m_type = rhs.GetType();

		switch (m_type)
		{
		case var_none:
				break;
		case var_int:
			m_value.int_ = rhs.GetInt();
			break;
		case var_bool:
			m_value.bool_ = rhs.GetBool();
			break;
		case var_float:
			m_value.float_ = rhs.GetFloat();
			break;
		case var_double:
			m_value.double_ = rhs.GetDouble();
			break;
		case var_vector_ref:
			m_value.vectorRef_ = rhs.GetVectorRef();
			break;
		case var_vector2:
			m_value.vector2_ = rhs.GetVector2();
			break;
		case var_vector3:
			m_value.vector3_ = rhs.GetVector3();
			break;
		case var_vector4:
			m_value.vector4_ = rhs.GetVector4();
			break;
		case var_quaternion:
			m_value.quaternion_ = rhs.GetQuaternion();
			break;
		case var_matrix:
			*m_value.matrix_ = rhs.GetMatrix();
			break;
		case var_variant_vector:
			m_value.variantVector_ = rhs.GetVariantVector();
			break;
		case var_buffer:
			m_value.buffer_ = rhs.GetBuffer();
			break;
		case var_voidptr:
			m_value.voidPtr_ = rhs.GetVoidPtr();
			break;
		}

		return *this;
	}
}
