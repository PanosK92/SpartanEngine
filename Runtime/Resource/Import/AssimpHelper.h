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

//= INCLUDES =====================
#include <memory>
#include "assimp\scene.h"
#include "..\..\Math\Vector2.h"
#include "..\..\Math\Vector3.h"
#include "..\..\Math\Matrix.h"
#include "..\..\Core\GameObject.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

class AssimpHelper
{
public:
	static Matrix aiMatrix4x4ToMatrix(const aiMatrix4x4& transform)
	{
		return Matrix(
			transform.a1, transform.b1, transform.c1, transform.d1,
			transform.a2, transform.b2, transform.c2, transform.d2,
			transform.a3, transform.b3, transform.c3, transform.d3,
			transform.a4, transform.b4, transform.c4, transform.d4
		);
	}

	static void SetGameObjectTransform(weak_ptr<GameObject> gameObject, aiNode* node)
	{
		if (gameObject.expired())
			return;

		aiMatrix4x4 mAssimp = node->mTransformation;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;

		// Decompose the transformation matrix
		Matrix mEngine = aiMatrix4x4ToMatrix(mAssimp);
		mEngine.Decompose(scale, rotation, position);

		// Apply position, rotation and scale
		gameObject._Get()->GetTransform()->SetPositionLocal(position);
		gameObject._Get()->GetTransform()->SetRotationLocal(rotation);
		gameObject._Get()->GetTransform()->SetScaleLocal(scale);
	}

	static Vector4 ToVector4(const aiColor4D& aiColor)
	{
		return Vector4(aiColor.r, aiColor.g, aiColor.b, aiColor.a);
	}

	static Vector3 ToVector3(const aiVector3D& aiVector)
	{
		return Vector3(aiVector.x, aiVector.y, aiVector.z);
	}

	static Vector2 ToVector2(const aiVector2D& aiVector)
	{
		return Vector2(aiVector.x, aiVector.y);
	}

	static Quaternion ToQuaternion(const aiQuaternion& aiQuaternion)
	{
		return Quaternion(aiQuaternion.x, aiQuaternion.y, aiQuaternion.z, aiQuaternion.w);
	}
};