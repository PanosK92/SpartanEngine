/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ==================
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
#include "../Math/Matrix.h"
#include <vector>
#include "EngineDefs.h"
//=============================

namespace Directus
{
	class Variant;

	struct VectorRef
	{
		void* ptr = nullptr;
		int length = 0;
	};

	// Supported types
	enum VariantType
	{
		var_none = 0,
		var_int,
		var_bool,
		var_float,
		var_double,
		var_vector_ref,
		var_vector2,
		var_vector3,
		var_vector4,
		var_quaternion,
		var_matrix,
		var_variant_vector,
		var_voidptr,
		var_buffer
	};

	// Size of variant value. 16 bytes on 32-bit platform, 32 bytes on 64-bit platform.
	static const unsigned VARIANT_VALUE_SIZE = sizeof(void*) * 4;

	// Possible variant values. Objects exceeding the VARIANT_VALUE_SIZE are allocated on the heap.
	union VariantValue
	{
		unsigned char storage_[VARIANT_VALUE_SIZE];
		bool bool_;
		int int_;
		float float_;
		double double_;
		VectorRef vectorRef_;
		Math::Vector2 vector2_;
		Math::Vector3 vector3_;
		Math::Vector4 vector4_;
		Math::Quaternion quaternion_;
		Math::Matrix* matrix_;	
		std::vector<Variant> variantVector_;
		std::vector<unsigned char> buffer_;
		void* voidPtr_;

		VariantValue() { }
		VariantValue(const VariantValue& value) = delete; // Non-copyable
		~VariantValue() { }
	};

	static_assert(sizeof(VariantValue) == VARIANT_VALUE_SIZE, "Unexpected size of VariantValue");

	class ENGINE_CLASS Variant
	{
	public:
		Variant() { }

		Variant(const Variant& var)
		{
			m_type = var.GetType();

			switch (m_type)
			{
			case var_none:
				break;
			case var_int:
				m_value.int_ = var.GetInt();
				break;
			case var_bool:
				m_value.bool_ = var.GetBool();
				break;
			case var_float:
				m_value.float_ = var.GetFloat();
				break;
			case var_double:
				m_value.double_ = var.GetDouble();
				break;
			case var_vector_ref:
				m_value.vectorRef_ = var.GetVectorRef();
				break;
			case var_vector2:
				m_value.vector2_ = var.GetVector2();
				break;
			case var_vector3:
				m_value.vector3_ = var.GetVector3();
				break;
			case var_vector4:
				m_value.vector4_ = var.GetVector4();
				break;
			case var_quaternion:
				m_value.quaternion_ = var.GetQuaternion();
				break;
			case var_matrix:
				*m_value.matrix_ = var.GetMatrix();
				break;
			case var_variant_vector:
				m_value.variantVector_ = var.GetVariantVector();
				break;
			case var_buffer:
				m_value.buffer_ = var.GetBuffer();
				break;
			case var_voidptr:
				m_value.voidPtr_ = var.GetVoidPtr();
				break;
			}
		}

		~Variant() { m_type = var_none; }

		//= Copy Constructors =============================================
		Variant(bool value) { *this = value; }
		Variant(int value) { *this = value; }
		Variant(float value) { *this = value; }
		Variant(double value) { *this = value; }
		Variant(VectorRef value) { *this = value; }
		Variant(const Math::Vector2& value) { *this = value; }
		Variant(const Math::Vector3& value) { *this = value; }
		Variant(const Math::Vector4& value) { *this = value; }
		Variant(const Math::Quaternion& value) { *this = value; }
		Variant(const Math::Matrix& value) { *this = value; }
		Variant(const std::vector<Variant>& value) { *this = value; }
		Variant(const std::vector<unsigned char>& value) { *this = value; }
		Variant(void* value) { *this = value; }
		//=================================================================

		//= ASSIGNMENT OPERATORS ============================
		Variant& operator =(const Variant& rhs);

		Variant& operator =(bool rhs)
		{
			m_type = var_bool;
			m_value.bool_ = rhs;
			return *this;
		}

		Variant& operator =(int rhs)
		{
			m_type = var_int;
			m_value.int_ = rhs;
			return *this;
		}

		Variant& operator =(float rhs)
		{
			m_type = var_float;
			m_value.float_ = rhs;
			return *this;
		}

		Variant& operator =(double rhs)
		{
			m_type = var_double;
			m_value.double_ = rhs;
			return *this;
		}

		Variant& operator =(const VectorRef rhs)
		{
			m_type = var_vector_ref;
			m_value.vectorRef_ = rhs;
			return *this;
		}

		Variant& operator =(const Math::Vector2& rhs)
		{
			m_type = var_vector2;
			m_value.vector2_ = rhs;
			return *this;
		}

		Variant& operator =(const Math::Vector3& rhs)
		{
			m_type = var_vector3;
			m_value.vector3_ = rhs;
			return *this;
		}

		Variant& operator =(const Math::Vector4& rhs)
		{
			m_type = var_vector4;
			m_value.vector4_ = rhs;
			return *this;
		}

		Variant& operator =(const Math::Quaternion& rhs)
		{
			m_type = var_quaternion;
			m_value.quaternion_ = rhs;
			return *this;
		}

		Variant& operator =(const Math::Matrix& rhs)
		{
			m_type = var_matrix;
			*m_value.matrix_ = rhs;
			return *this;
		}

		Variant& operator =(const std::vector<Variant>& rhs)
		{
			m_type = var_variant_vector;
			m_value.variantVector_ = rhs;
			return *this;
		}

		Variant& operator =(const std::vector<unsigned char>& rhs)
		{
			m_type = var_buffer;
			m_value.buffer_ = rhs;
			return *this;
		}

		Variant& operator =(void* rhs)
		{
			m_type = var_voidptr;
			m_value.voidPtr_ = rhs;
			return *this;
		}
		//==================================================

		// Returns bool or false on type mismatch.
		bool GetBool() const { return m_type == var_bool ? m_value.bool_ : false; }

		// Returns int or zero on type mismatch. Floats and doubles are converted.
		int GetInt() const
		{
			if (m_type == var_int) return m_value.int_;
			if (m_type == var_float) return static_cast<int>(m_value.float_);
			if (m_type == var_double) return static_cast<int>(m_value.double_);

			return 0;
		}

		// Returns float or zero on type mismatch. Doubles and integers are converted.
		float GetFloat() const
		{
			if (m_type == var_float) return m_value.float_;
			if (m_type == var_double) return static_cast<float>(m_value.double_);
			if (m_type == var_int) return static_cast<float>(m_value.float_);

			return 0.0f;
		}

		// Returns int or zero on type mismatch. Floats and integers are converted.
		double GetDouble() const
		{
			if (m_type == var_double) return m_value.double_;
			if (m_type == var_float) return static_cast<double>(m_value.float_);
			if (m_type == var_int) return static_cast<double>(m_value.int_);

			return 0.0;
		}

		// Returns VectorRef or an empty VectorRef on type mismatch.
		VectorRef GetVectorRef() const { return m_type == var_vector_ref ? m_value.vectorRef_ : VectorRef(); }

		// Returns Vector2 or zero on type mismatch.
		const Math::Vector2& GetVector2() const { return m_type == var_vector2 ? m_value.vector2_ : Math::Vector2::Zero; }

		// Returns Vector3 or zero on type mismatch.
		const Math::Vector3& GetVector3() const { return m_type == var_vector3 ? m_value.vector3_ : Math::Vector3::Zero; }

		// Returns Vector4 or zero on type mismatch.
		const Math::Vector4& GetVector4() const { return m_type == var_vector4 ? m_value.vector4_ : Math::Vector4::Zero; }

		// Returns Quaternion or an identity quaternion on type mismatch.
		const Math::Quaternion& GetQuaternion() const { return m_type == var_quaternion ? m_value.quaternion_ : Math::Quaternion::Identity; }

		// Returns Quaternion or an identity matrix on type mismatch.
		const Math::Matrix& GetMatrix() const { return m_type == var_matrix ? *m_value.matrix_ : Math::Matrix::Identity; }

		// Returns a variant vector or an empty vector on type mismatch.
		const std::vector<Variant>& GetVariantVector() const { return m_type == var_variant_vector ? m_value.variantVector_ : std::vector<Variant>(); }

		// Returns an unsigned char vector or an empty vector on type mismatch.
		const std::vector<unsigned char>& GetBuffer() const { return m_type == var_buffer ? m_value.buffer_ : std::vector<unsigned char>(); }

		// Returns void pointer or nullptr on type mismatch.
		void* GetVoidPtr() const { return m_type == var_voidptr ? m_value.voidPtr_ : nullptr; }

		VariantType GetType() const { return m_type; }
		bool IsEmpty() const { return m_type == var_none; }

	private:
		VariantType m_type = var_none;
		VariantValue m_value;
	};

	//= HELPER FUNCTIONS ================================================
	template <class T>
	VectorRef VectorToVariant(const std::vector<T>& vector)
	{
		VectorRef varArray;
		varArray.ptr = (void*)vector.data();
		varArray.length = (int)vector.size();

		return varArray;
	}

	template <class T>
	std::vector<T> VariantToVector(Variant variant)
	{
		T* ptr = (T*)variant.GetVectorRef().ptr;
		int length = variant.GetVectorRef().length;

		return vector<T>(ptr, ptr + length);
	}
}
