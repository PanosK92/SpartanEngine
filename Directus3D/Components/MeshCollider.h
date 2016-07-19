/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ==========
#include "IComponent.h"
//=====================

class RigidBody;
class MeshFilter;
class btCollisionShape;

class __declspec(dllexport) MeshCollider : public IComponent
{
public:
	MeshCollider();
	~MeshCollider();

	/*------------------------------------------------------------------------------
									[INTERFACE]
	------------------------------------------------------------------------------*/
	virtual void Initialize();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();

	bool GetConvex();
	void SetConvex(bool isConvex);

private:
	MeshFilter* m_mesh;
	int m_vertexLimit = 1024;
	btCollisionShape* m_collider;
	bool m_convex;
	bool m_isDirty;
	RigidBody* m_rigidBody;

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void ConstructFromVertexCloud();
	bool ComponentCheck();
};
