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
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../IO/Log.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "../Pools/MeshPool.h"
#include "../Core/Helper.h"
//====================================================================

//= NAMESPACES =====
using namespace std;
//==================

MeshCollider::MeshCollider()
{
	m_collisionShape = nullptr;
	m_convex = false;
	m_mesh = nullptr;
}

MeshCollider::~MeshCollider()
{

}

//= ICOMPONENT ========================================
void MeshCollider::Initialize()
{
	SetMesh(GetMeshFromAttachedMeshFilter());
	ConstructCollisionShape();
}

void MeshCollider::Start()
{

}

void MeshCollider::Remove()
{
	DeleteCollisionShape();
}

void MeshCollider::Update()
{

}

void MeshCollider::Serialize()
{
	Serializer::WriteBool(m_convex);
	Serializer::WriteSTR(m_mesh ? m_mesh->GetID() : (string)DATA_NOT_ASSIGNED);
}

void MeshCollider::Deserialize()
{
	m_convex = Serializer::ReadBool();
	m_mesh = g_meshPool->GetMeshByID(Serializer::ReadSTR());

	ConstructCollisionShape();
}
//======================================================================================================================

bool MeshCollider::GetConvex() const
{
	return m_convex;
}

void MeshCollider::SetConvex(bool isConvex)
{
	m_convex = isConvex;
	ConstructCollisionShape();
}

shared_ptr<Mesh> MeshCollider::GetMesh() const
{
	return m_mesh;
}

void MeshCollider::SetMesh(shared_ptr<Mesh> mesh)
{
	m_mesh = mesh;
	ConstructCollisionShape();
}

//= HELPER FUNCTIONS ================================================================================================
void MeshCollider::ConstructCollisionShape()
{
	if (!m_mesh)
		return;

	if (m_mesh->GetVertexCount() >= m_vertexLimit)
	{
		LOG_WARNING("No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
		return;
	}

	DeleteCollisionShape();
	//= contruct collider ========================================================================================
	btTriangleMesh* trimesh = new btTriangleMesh();
	vector<Directus::Math::Vector3> vertices;
	for (auto i = 0; i < m_mesh->GetTriangleCount(); i++)
	{
		
		int index0 = m_mesh->GetIndices()[i * 3];
		int index1 = m_mesh->GetIndices()[i * 3 + 1];
		int index2 = m_mesh->GetIndices()[i * 3 + 2];

		vertices.push_back(m_mesh->GetVertices()[index0].position);
		vertices.push_back(m_mesh->GetVertices()[index0].position);
		vertices.push_back(m_mesh->GetVertices()[index0].position);

		btVector3 vertex0 = ToBtVector3(m_mesh->GetVertices()[index0].position);
		btVector3 vertex1 = ToBtVector3(m_mesh->GetVertices()[index1].position);
		btVector3 vertex2 = ToBtVector3(m_mesh->GetVertices()[index2].position);

		trimesh->addTriangle(vertex0, vertex1, vertex2);
	}
	bool useQuantization = true;
	m_collisionShape = make_shared<btBvhTriangleMeshShape>(trimesh, useQuantization);

	//= construct a hull approximation ===========================================================================
	if (m_convex)
	{
		auto shape = new btConvexHullShape((btScalar*)vertices.data(), m_mesh->GetVertexCount(), sizeof(Directus::Math::Vector3));

		// OPTIMIZE
		auto hull = make_shared<btShapeHull>(shape);
		hull->buildHull(shape->getMargin());
		auto convexShape = make_shared<btConvexHullShape>((btScalar*)hull->getVertexPointer(), hull->numVertices(), sizeof(btVector3));
		m_collisionShape = convexShape;
	}

	SetCollisionShapeToRigidBody(m_collisionShape);
}

void MeshCollider::SetCollisionShapeToRigidBody(shared_ptr<btCollisionShape> shape) const
{
	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();
	if (rigidBody)
		rigidBody->SetCollisionShape(shape);
}

shared_ptr<Mesh> MeshCollider::GetMeshFromAttachedMeshFilter() const
{
	MeshFilter* meshFilter = g_gameObject->GetComponent<MeshFilter>();
	return meshFilter ? meshFilter->GetMesh() : nullptr;
}

void MeshCollider::DeleteCollisionShape()
{
	if (m_collisionShape)
		SetCollisionShapeToRigidBody(m_collisionShape);	
}
//======================================================================================================================
