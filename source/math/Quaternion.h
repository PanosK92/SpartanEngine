/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace spartan::math
{
    class Quaternion
    {
    public:
        // Constructs an identity quaternion
        Quaternion()
        {
            x = 0;
            y = 0;
            z = 0;
            w = 1;
        }

        // Constructs a new quaternion with the given components
        Quaternion(float x, float y, float z, float w)
        {
            this->x = x;
            this->y = y;
            this->z = z;
            this->w = w;
        }

        explicit Quaternion(const __m128 v)
        {
            simd::store4(&x, v);
        }

        ~Quaternion() = default;

        [[nodiscard]] __m128 Simd() const { return simd::load4(&x); }

        // Creates a new Quaternion from the specified axis and angle.
        // The angle in radians.
        // The axis of rotation.
        static Quaternion FromAxisAngle(const Vector3& axis, float angle)
        {
            __m128 sin, cos;
            simd::sin_cos(_mm_set1_ps(angle * 0.5f), sin, cos);

            return Quaternion(_mm_blend_ps(_mm_mul_ps(axis.Simd(), sin), cos, 0x8));
        }

        void FromAxes(const Vector3& axis_x, const Vector3& axis_y, const Vector3& axis_z);

        // rotation matrix (no scale) to quaternion, row i is (mi0, mi1, mi2)
        // canonical form (w >= 0) to prevent sign flipping between equivalent quaternions
        static Quaternion FromRotationRows(__m128 row0, __m128 row1, __m128 row2);

        // Creates a new Quaternion from the specified yaw, pitch and roll angles.
        // Yaw around the y axis in radians.
        // Pitch around the x axis in radians.
        // Roll around the z axis in radians.
        static inline Quaternion FromYawPitchRoll(float yaw, float pitch, float roll)
        {
            // lanes are (roll, pitch, yaw)
            __m128 s, c;
            simd::sin_cos(_mm_mul_ps(_mm_setr_ps(roll, pitch, yaw, 0.0f), _mm_set1_ps(0.5f)), s, c);

            // x = cy * sp * cr + sy * cp * sr
            // y = sy * cp * cr - cy * sp * sr
            // z = cy * cp * sr - sy * sp * cr
            // w = cy * cp * cr + sy * sp * sr
            const __m128 sr = simd::splat<0>(s), cr = simd::splat<0>(c);
            const __m128 sp = simd::splat<1>(s), cp = simd::splat<1>(c);
            const __m128 sy = simd::splat<2>(s), cy = simd::splat<2>(c);
            const __m128 a  = _mm_mul_ps(_mm_mul_ps(_mm_blend_ps(cy, sy, 0x2), _mm_blend_ps(cp, sp, 0x1)), _mm_blend_ps(cr, sr, 0x4));
            const __m128 b  = _mm_mul_ps(_mm_mul_ps(_mm_blend_ps(sy, cy, 0x2), _mm_blend_ps(sp, cp, 0x1)), _mm_blend_ps(sr, cr, 0x4));

            return Quaternion(_mm_fmadd_ps(b, _mm_setr_ps(1.0f, -1.0f, -1.0f, 1.0f), a));
        }

        static Quaternion FromRotation(const Vector3& start, const Vector3& end)
        {
            const Vector3 start_normalized = start.Normalized();
            const Vector3 end_normalized   = end.Normalized();
            const float dot                = std::clamp(start_normalized.Dot(end_normalized), -1.0f, 1.0f);

            // 1 + dot cancels catastrophically near antiparallel, bail out well before float epsilon
            if (dot > -1.0f + 1.0e-6f)
            {
                const __m128 cross = simd::cross3(start_normalized.Simd(), end_normalized.Simd());
                const float scale  = sqrtf((1.0f + dot) * 2.0f);

                return Quaternion(_mm_insert_ps(_mm_mul_ps(cross, _mm_set1_ps(1.0f / scale)), _mm_set_ss(0.5f * scale), 0x30));
            }
            else
            {
                // the vectors are opposite, any perpendicular axis gives a valid 180 degree turn
                Vector3 axis = Vector3::Right.Cross(start_normalized);
                if (axis.LengthSquared() < 1.0e-6f)
                {
                    axis = Vector3::Up.Cross(start_normalized);
                }
                // from axis angle assumes a unit axis, a cross of unit vectors is not one
                axis.Normalize();

                return FromAxisAngle(axis, 180.0f * deg_to_rad);
            }
        }

        static Quaternion FromLookRotation(const Vector3& direction, const Vector3& up_direction = Vector3::Up)
        {
            Quaternion result;
            const Vector3 forward = direction.Normalized();

            Vector3 v = forward.Cross(up_direction);
            if (v.LengthSquared() >= std::numeric_limits<float>::min())
            {
                v.Normalize();
                const Vector3 up    = v.Cross(forward);
                const Vector3 right = up.Cross(forward);
                result.FromAxes(right, up, forward);
            }
            else
            {
                result = Quaternion::FromRotation(Vector3::Forward, forward);
            }

            return result;
        }

        static Quaternion Lerp(const Quaternion& a, const Quaternion& b, const float t)
        {
            const __m128 va = a.Simd();
            const __m128 vb = b.Simd();

            // take the short way around, q and -q are the same rotation
            const float t_b = simd::dot4(va, vb) >= 0.0f ? t : -t;

            return Quaternion(_mm_fmadd_ps(vb, _mm_set1_ps(t_b), _mm_mul_ps(va, _mm_set1_ps(1.0f - t)))).Normalized();
        }

        // hamilton product, the vector parts contribute a cross product and the scalar parts a dot
        // r = aw * (bx,  by,  bz,  bw)
        //   + ax * (bw, -bz,  by, -bx)
        //   + ay * (bz,  bw, -bx, -by)
        //   + az * (-by, bx,  bw, -bz)
        static Quaternion Multiply(const Quaternion& a, const Quaternion& b)
        {
            const __m128 va = a.Simd();
            const __m128 vb = b.Simd();

            const __m128 b_wzyx = _mm_xor_ps(_mm_permute_ps(vb, _MM_SHUFFLE(0, 1, 2, 3)), _mm_setr_ps(0.0f, -0.0f, 0.0f, -0.0f));
            const __m128 b_zwxy = _mm_xor_ps(_mm_permute_ps(vb, _MM_SHUFFLE(1, 0, 3, 2)), _mm_setr_ps(0.0f, 0.0f, -0.0f, -0.0f));
            const __m128 b_yxwz = _mm_xor_ps(_mm_permute_ps(vb, _MM_SHUFFLE(2, 3, 0, 1)), _mm_setr_ps(-0.0f, 0.0f, 0.0f, -0.0f));

            // summed as a tree to keep the dependency chain short
            const __m128 low  = _mm_fmadd_ps(simd::splat<0>(va), b_wzyx, _mm_mul_ps(simd::splat<3>(va), vb));
            const __m128 high = _mm_fmadd_ps(simd::splat<2>(va), b_yxwz, _mm_mul_ps(simd::splat<1>(va), b_zwxy));

            return Quaternion(_mm_add_ps(low, high));
        }

        auto Conjugate() const      { return Quaternion(_mm_xor_ps(Simd(), _mm_setr_ps(-0.0f, -0.0f, -0.0f, 0.0f))); }
        float LengthSquared() const { const __m128 v = Simd(); return simd::dot4(v, v); }

        // Normalizes the quaternion
        void Normalize()
        {
            const __m128 v              = Simd();
            const __m128 length_squared = simd::dot4_splat(v, v);
            const float length_squared_ = _mm_cvtss_f32(length_squared);
            if (!approximate_equals(length_squared_, 1.0f) && length_squared_ > 0.0f)
            {
                simd::store4(&x, _mm_div_ps(v, _mm_sqrt_ps(length_squared)));
            }
        }

        Quaternion Normalized() const
        {
            Quaternion result = *this;
            result.Normalize();
            return result;
        }

        Quaternion Inverse() const 
        {
            const float length_squared = LengthSquared();
            if (length_squared == 1.0f)
            {
                return Conjugate();
            }
            else if (length_squared >= std::numeric_limits<float>::min())
            {
                return Quaternion(_mm_div_ps(Conjugate().Simd(), _mm_set1_ps(length_squared)));
            }
            else
            {
                return Identity;
            }
        }

        Vector3 ToEulerAngles() const
        {
            // Derivation from http://www.geometrictools.com/Documentation/EulerAngles.pdf
            // Order of rotations: Z first, then X, then Y
            const float check = 2.0f * (-y * z + w * x);

            if (check < -0.995f)
            {
                return Vector3
                (
                    -90.0f,
                    0.0f,
                    -atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * rad_to_deg
                );
            }

            if (check > 0.995f)
            {
                return Vector3
                (
                    90.0f,
                    0.0f,
                    atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * rad_to_deg
                );
            }

            // (x * z + w * y, x * x + y * y, x * y + w * z, x * x + z * z), then 2 * p for the numerators and 1 - 2 * p for the denominators
            const __m128 q = Simd();
            const __m128 p = _mm_fmadd_ps(
                simd::splat<0>(q), _mm_permute_ps(q, _MM_SHUFFLE(0, 1, 0, 2)),
                _mm_mul_ps(_mm_permute_ps(q, _MM_SHUFFLE(2, 3, 1, 3)), _mm_permute_ps(q, _MM_SHUFFLE(2, 2, 1, 1))));
            const __m128 terms = _mm_fmadd_ps(p, _mm_setr_ps(2.0f, -2.0f, 2.0f, -2.0f), _mm_setr_ps(0.0f, 1.0f, 0.0f, 1.0f));

            alignas(16) float t[4];
            _mm_store_ps(t, terms);

            return Vector3
            (
                asinf(check) * rad_to_deg,
                atan2f(t[0], t[1]) * rad_to_deg,
                atan2f(t[2], t[3]) * rad_to_deg
            );
        }

        // euler angles to quaternion (input in degrees)
        static Quaternion FromEulerAngles(const Vector3& rotation)                           { return FromYawPitchRoll(rotation.y * deg_to_rad, rotation.x * deg_to_rad, rotation.z * deg_to_rad); }
        static Quaternion FromEulerAngles(float rotation_x, float rotation_y, float rotation_z) { return FromYawPitchRoll(rotation_y * deg_to_rad,  rotation_x * deg_to_rad,  rotation_z * deg_to_rad); }

        // Returns yaw in degrees
        float Yaw() const   { return ToEulerAngles().y; }

        // Calculate dot product.
        float Dot(const Quaternion& rhs)                            const { return simd::dot4(Simd(), rhs.Simd()); }
        static inline float Dot(const Quaternion& a, const Quaternion& b) { return simd::dot4(a.Simd(), b.Simd()); }

        // Operators
        Quaternion& operator=(const Quaternion& rhs) = default;
        Quaternion operator+(const Quaternion& rhs) const { return Quaternion(_mm_add_ps(Simd(), rhs.Simd())); }
        Quaternion operator-(const Quaternion& rhs) const { return Quaternion(_mm_sub_ps(Simd(), rhs.Simd())); }
        Quaternion operator-()                      const { return Quaternion(simd::negate(Simd())); }
        Quaternion operator*(const Quaternion& rhs) const { return Multiply(*this, rhs); }
        void operator*=(const Quaternion& rhs)            { *this = Multiply(*this, rhs); }

        // v' = v + 2 * (w * (q x v) + q x (q x v))
        Vector3 operator*(const Vector3& rhs) const
        {
            const __m128 q      = _mm_blend_ps(Simd(), _mm_setzero_ps(), 0x8);
            const __m128 v      = rhs.Simd();
            const __m128 cross1 = simd::cross3(q, v);
            const __m128 cross2 = simd::cross3(q, cross1);
            const __m128 sum    = _mm_fmadd_ps(cross1, _mm_set1_ps(w), cross2);

            return Vector3(_mm_fmadd_ps(sum, _mm_set1_ps(2.0f), v));
        }

        Quaternion& operator*=(float rhs)
        {
            simd::store4(&x, _mm_mul_ps(Simd(), _mm_set1_ps(rhs)));
            return *this;
        }

        Quaternion operator*(float rhs) const { return Quaternion(_mm_mul_ps(Simd(), _mm_set1_ps(rhs))); }

        // Equality
        bool operator==(const Quaternion& rhs) const { return simd::equal_mask(Simd(), rhs.Simd()) == 0xF; }
        bool operator!=(const Quaternion& rhs) const { return !(*this == rhs); }

        std::string ToString() const;

        bool IsNaN()    const { return simd::nan_mask(Simd()) != 0; }
        bool IsFinite() const { return simd::finite_mask(Simd()) == 0xF; }

        float x, y, z, w;
        static const Quaternion Identity;
    };

    // Reverse order operators
    inline  Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
    inline  Quaternion operator*(float lhs, const Quaternion& rhs)       { return rhs * lhs; }
}
