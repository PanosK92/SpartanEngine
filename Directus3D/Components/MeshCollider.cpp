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
	m_collisionShape = nullptr;
	m_convex = false;
	m_meshFilter = nullptr;
}

MeshCollider::~MeshCollider()
{
	delete m_collisionShape;
	m_collisionShape = nullptr;
}

//= ICOMPONENT ========================================
void MeshCollider::Initialize()
{
	// Initialize with the mesh filter that might
	// be attached to this GameObject
	m_meshFilter = g_gameObject->GetComponent<MeshFilter>();
}

void MeshCollider::Start()
{

}

void MeshCollider::Remove()
{
	SetCollisionShapeToRigidBody(nullptr);
}

void MeshCollider::Update()
{

}

void MeshCollider::Serialize()
{
	Serializer::SaveBool(m_convex);
}

void MeshCollider::Deserialize()
{
	m_convex = Serializer::LoadBool();
	ConstructFromVertexCloud();
}

bool MeshCollider::GetConvex() const
{
	return m_convex;
}

void MeshCollider::SetConvex(bool isConvex)
{
	m_convex = isConvex;
	ConstructFromVertexCloud();
}

Mesh* MeshCollider::GetMesh() const
{
	return m_meshFilter ? m_meshFilter->GetMesh() : nullptr;
}

void MeshCollider::SetMesh(Mesh* mesh)
{
	m_meshFilter = m_meshFilter;
}
//======================================================================================================================

//= HELPER FUNCTIONS ===================================================================================================
void MeshCollider::ConstructFromVertexCloud()
{
	if (!m_meshFilter)
		return;

	if (m_meshFilter->GetVertexCount() >= m_vertexLimit)
	{
		LOG_WARNING("No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
		return;
	}

	// vertices & indices
	vector<VertexPositionTextureNormalTangent> vertices = m_meshFilter->GetVertices();
	vector<unsigned int> indices = m_meshFilter->GetIndices();

	//= contruct collider ========================================================================================
	btTriangleMesh* trimesh = new btTriangleMesh();
	for (auto i = 0; i < m_meshFilter->GetTriangleCount(); i++)
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
	m_collisionShape = new btBvhTriangleMeshShape(trimesh, useQuantization);

	vertices.clear();
	indices.clear();

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

		m_collisionShape = convexShape;

		delete tmpConvexShape;
		delete hull;
	}

	SetCollisionShapeToRigidBody(m_collisionShape);
}

void MeshCollider::SetCollisionShapeToRigidBody(btCollisionShape* collisionShape) const
{
	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();
	if (rigidBody)
		rigidBody->SetCollisionShape(m_collisionShape);
}
//======================================================================================================================