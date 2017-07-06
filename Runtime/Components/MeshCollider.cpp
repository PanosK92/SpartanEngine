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

//= INCLUDES ========================================================
#include "MeshCollider.h"
#include "MeshFilter.h"
#include "RigidBody.h"
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include "../IO/Serializer.h"
#include "../Core/GameObject.h"
#include "../Logging/Log.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
#include "../Graphics/Model.h"
//===================================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshCollider::MeshCollider()
	{
		Register();
		m_collisionShape = nullptr;
		m_isConvex = false;
	}

	MeshCollider::~MeshCollider()
	{

	}

	//= ICOMPONENT ========================================
	void MeshCollider::Reset()
	{
		SetMesh(GetMeshFromAttachedMeshFilter());
		Build();
	}

	void MeshCollider::Start()
	{

	}

	void MeshCollider::OnDisable()
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
		Serializer::WriteBool(m_isConvex);
		Serializer::WriteSTR(!m_mesh.expired() ? m_mesh.lock()->GetID() : (string)DATA_NOT_ASSIGNED);
	}

	void MeshCollider::Deserialize()
	{
		m_isConvex = Serializer::ReadBool();
		string meshID = Serializer::ReadSTR();

		auto models = g_context->GetSubsystem<ResourceManager>()->GetResourcesByType<Model>();
		for (const auto& model : models)
		{
			auto mesh = model.lock()->GetMeshByID(meshID);
			if (!mesh.expired())
			{
				m_mesh = mesh;
				break;
			}
		}

		Build();
	}
	//======================================================================================================================
	void MeshCollider::Build()
	{
		if (m_mesh.expired())
			return;

		if (m_mesh.lock()->GetVertexCount() >= m_vertexLimit)
		{
			LOG_WARNING("No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
			return;
		}

		DeleteCollisionShape();
		//= contruct collider ========================================================================================
		btTriangleMesh* trimesh = new btTriangleMesh();
		vector<Vector3> vertices;
		for (unsigned int i = 0; i < m_mesh.lock()->GetTriangleCount(); i++)
		{

			int index0 = m_mesh.lock()->GetIndices()[i * 3];
			int index1 = m_mesh.lock()->GetIndices()[i * 3 + 1];
			int index2 = m_mesh.lock()->GetIndices()[i * 3 + 2];

			vertices.push_back(m_mesh.lock()->GetVertices()[index0].position);
			vertices.push_back(m_mesh.lock()->GetVertices()[index0].position);
			vertices.push_back(m_mesh.lock()->GetVertices()[index0].position);

			btVector3 vertex0 = ToBtVector3(m_mesh.lock()->GetVertices()[index0].position);
			btVector3 vertex1 = ToBtVector3(m_mesh.lock()->GetVertices()[index1].position);
			btVector3 vertex2 = ToBtVector3(m_mesh.lock()->GetVertices()[index2].position);

			trimesh->addTriangle(vertex0, vertex1, vertex2);
		}
		bool useQuantization = true;
		m_collisionShape = make_shared<btBvhTriangleMeshShape>(trimesh, useQuantization);

		//= construct a hull approximation ===========================================================================
		if (m_isConvex)
		{
			auto shape = new btConvexHullShape((btScalar*)vertices.data(), m_mesh.lock()->GetVertexCount(), sizeof(Vector3));

			// OPTIMIZE
			auto hull = make_shared<btShapeHull>(shape);
			hull->buildHull(shape->getMargin());
			auto convexShape = make_shared<btConvexHullShape>((btScalar*)hull->getVertexPointer(), hull->numVertices(), sizeof(btVector3));
			m_collisionShape = convexShape;
		}

		SetCollisionShapeToRigidBody(m_collisionShape);
	}

	//= HELPER FUNCTIONS ================================================================================================
	void MeshCollider::SetCollisionShapeToRigidBody(weak_ptr<btCollisionShape> shape)
	{
		if (g_gameObject.expired())
		{
			return;
		}

		RigidBody* rigidBody = g_gameObject.lock()->GetComponent<RigidBody>();
		if (rigidBody)
		{
			rigidBody->SetCollisionShape(shape);
		}
	}

	weak_ptr<Mesh> MeshCollider::GetMeshFromAttachedMeshFilter()
	{
		if (g_gameObject.expired())
		{
			return weak_ptr<Mesh>();
		}

		MeshFilter* meshFilter = g_gameObject.lock()->GetComponent<MeshFilter>();
		return meshFilter ? meshFilter->GetMesh() : weak_ptr<Mesh>();
	}

	void MeshCollider::DeleteCollisionShape()
	{
		SetCollisionShapeToRigidBody(weak_ptr<btCollisionShape>());
	}
	//======================================================================================================================
}