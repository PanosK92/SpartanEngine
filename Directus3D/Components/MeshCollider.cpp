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

//= INCLUDES ==========================================================
#include "MeshCollider.h"
#include "MeshFilter.h"
#include "RigidBody.h"
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btConvexTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../IO/Log.h"
//====================================================================

//= NAMESPACES =====
using namespace std;
//==================

MeshCollider::MeshCollider()
{
	m_collider = nullptr;
	m_convex = false;
	m_rigidBody = nullptr;
	m_meshFilter = nullptr;

	m_isDirty = true;
}

MeshCollider::~MeshCollider()
{
	delete m_collider;
	m_collider = nullptr;
}

/*------------------------------------------------------------------------------
							[INTERFACE]
------------------------------------------------------------------------------*/
void MeshCollider::Initialize()
{
	if (!g_gameObject->HasComponent<RigidBody>())
		g_gameObject->AddComponent<RigidBody>();

	m_rigidBody = g_gameObject->GetComponent<RigidBody>();
}

void MeshCollider::Remove()
{
	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();

	if (rigidBody)
		rigidBody->SetCollisionShape(m_collider);
}

void MeshCollider::Update()
{
	if (!ComponentCheck())
		return;

	if (!m_isDirty)
		return;

	ConstructFromVertexCloud();

	m_isDirty = false;
}

void MeshCollider::Serialize()
{
	Serializer::SaveBool(m_convex);
}

void MeshCollider::Deserialize()
{
	m_convex = Serializer::LoadBool();
	m_isDirty = true;
}

bool MeshCollider::GetConvex()
{
	return m_convex;
}

void MeshCollider::SetConvex(bool isConvex)
{
	m_convex = isConvex;
	m_isDirty = true;
}

Mesh* MeshCollider::GetMesh()
{
	if (!m_meshFilter)
		return nullptr;

	return m_meshFilter->GetMesh();
}

void MeshCollider::SetMesh(Mesh* mesh)
{
	// needs to be implemented
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void MeshCollider::ConstructFromVertexCloud()
{
	if (m_meshFilter->GetVertexCount() > m_vertexLimit)
	{
		LOG("No user defined collider with more than " + std::to_string(m_vertexLimit) + " vertices is allowed.", Log::Warning);
		return;
	}

	// vertices & indices
	vector<VertexPositionTextureNormalTangent> vertices = m_meshFilter->GetVertices();
	vector<unsigned int> indices = m_meshFilter->GetIndices();

	//= contruct collider ========================================================================================
	btTriangleMesh* trimesh = new btTriangleMesh();
	for (auto i = 0; i < m_meshFilter->GetFaceCount(); i++)
	{
		int index0 = indices[i * 3];
		int index1 = indices[i * 3 + 1];
		int index2 = indices[i * 3 + 2];

		btVector3 vertex0(vertices[index0].position.x, vertices[index0].position.y, vertices[index0].position.z);
		btVector3 vertex1(vertices[index1].position.x, vertices[index1].position.y, vertices[index1].position.z);
		btVector3 vertex2(vertices[index2].position.x, vertices[index2].position.y, vertices[index2].position.z);

		trimesh->addTriangle(vertex0, vertex1, vertex2);
	}
	bool useQuantization = true;
	m_collider = new btBvhTriangleMeshShape(trimesh, useQuantization);

	//= construct a hull approximation ===========================================================================
	if (m_convex)
	{
		btConvexShape* tmpConvexShape = new btConvexTriangleMeshShape(trimesh);
		btShapeHull* hull = new btShapeHull(tmpConvexShape);
		btScalar margin = tmpConvexShape->getMargin();
		hull->buildHull(margin);
		tmpConvexShape->setUserPointer(hull);

		bool updateLocalAabb = false;
		btConvexHullShape* convexShape = new btConvexHullShape();
		for (int i = 0; i < hull->numVertices(); i++)
		{
			convexShape->addPoint(hull->getVertexPointer()[i], updateLocalAabb);
		}
		convexShape->recalcLocalAabb();

		m_collider = convexShape;

		delete tmpConvexShape;
		delete hull;
	}

	//= set rigidbody's collision shape ===========================================================================
	m_rigidBody->SetCollisionShape(m_collider);
}

bool MeshCollider::ComponentCheck()
{
	if (!m_rigidBody)
		m_rigidBody = g_gameObject->GetComponent<RigidBody>();

	if (!m_meshFilter)
		m_meshFilter = g_gameObject->GetComponent<MeshFilter>();

	if (m_rigidBody && m_meshFilter)
		return true;

	return false;
}
