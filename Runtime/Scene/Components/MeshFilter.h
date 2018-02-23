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

//= INCLUDES ======================
#include "IComponent.h"
#include <vector>
#include <memory>
#include "../../Math/BoundingBox.h"
//=================================

namespace Directus
{
	class Mesh;
	struct LoadVertices;

	namespace Math
	{
		class Vector3;
		class BoundingBox;
	}

	enum MeshType
	{
		MeshType_Imported,
		MeshType_Cube,
		MeshType_Quad,
		MeshType_Sphere,
		MeshType_Cylinder,
		MeshType_Cone
	};

	class ENGINE_CLASS MeshFilter : public IComponent
	{
	public:
		MeshFilter(Context* context, GameObject* gameObject, Transform* transform);
		~MeshFilter();

		//= ICOMPONENT ===============================
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		// Sets a mesh from memory
		void SetMesh(const std::weak_ptr<Mesh>& mesh, bool autoCache = true);

		// Sets a default mesh (cube, quad)
		void UseStandardMesh(MeshType type);

		// Set vertex and index buffers (must be called before rendering)
		bool SetBuffers();

		//= BOUNDING BOX ===============================
		const Math::BoundingBox& GetBoundingBox() const;
		Math::BoundingBox GetBoundingBoxTransformed();
		//==============================================

		//= PROPERTIES ========================================
		MeshType GetMeshType() { return m_meshType; }
		std::string GetMeshName();
		const std::weak_ptr<Mesh>& GetMesh() { return m_mesh; }
		bool HasMesh() { return !m_mesh.expired(); }
		//=====================================================

	private:
		// A weak reference to the mesh
		std::weak_ptr<Mesh> m_mesh;
		// Type of mesh
		MeshType m_meshType;
	};
}