/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==========================
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
// Hide warnings which belong to Bullet
#pragma warning(push, 0) 
#include "LinearMath/btQuaternion.h"
#pragma warning(pop)
//=====================================

inline Directus::Math::Vector3 ToVector3(const btVector3& vector)
{
	return Directus::Math::Vector3(vector.getX(), vector.getY(), vector.getZ());
}

inline Directus::Math::Vector4 ToVector4(const btVector3& vector)
{
	return Directus::Math::Vector4(vector.getX(), vector.getY(), vector.getZ(), 1.0f);
}

inline btVector3 ToBtVector3(Directus::Math::Vector3 vector)
{
	return btVector3(vector.x, vector.y, vector.z);
}

inline btQuaternion ToBtQuaternion(Directus::Math::Quaternion quaternion)
{
	return btQuaternion(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
}

inline Directus::Math::Quaternion ToQuaternion(const btQuaternion& quaternion)
{
	return Directus::Math::Quaternion(quaternion.getX(), quaternion.getY(), quaternion.getZ(), quaternion.getW());
}
