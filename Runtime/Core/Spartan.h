/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= DISABLED WARNINGS ==============================================================================================================================
// identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'
#pragma warning(disable: 4251) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// non – DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'
#pragma warning(disable: 4275) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// no definition for inline function 'function'
#pragma warning(disable: 4506) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4506?view=vs-2017
//==================================================================================================================================================

// Platform 
//#define API_GRAPHICS_D3D11    -> Defined by solution generation script
//#define API_GRAPHICS_D3D12    -> Defined by solution generation script
//#define API_GRAPHICS_VULKAN   -> Defined by solution generation script
#define API_INPUT_WINDOWS //    -> Explicitly defined for now

//= WINDOWS ===============
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
//=========================

//= STD ==============
#include <string>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <fstream>
#include <sstream>
#include <limits>
#include <assert.h>
#include <cstdint>
#include <array>
#include <atomic>
#include <map>
//====================

//= THIRD-PARTY LIBS ===========================================================
#pragma warning(push, 0) // Hide warnings which don't belong to us
// Assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/version.h>
// Bullet
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletSoftBody/btSoftBody.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>
#include <BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h>
#include <BulletSoftBody/btSoftBodyHelpers.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btSliderConstraint.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
// FreeType
#include "freetype/ftstroke.h"
//pugixml
#include "pugixml.hpp"
// Mono
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/environment.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/exception.h>
#include <mono/metadata/appdomain.h>
#include <mono/utils/mono-publib.h>
#pragma warning(pop)
//==============================================================================

//= RUNTIME ====================
// Core
#include "Engine.h"
#include "EventSystem.h"
#include "Settings.h"
#include "Context.h"
#include "Timer.h"
#include "FileSystem.h"
#include "Stopwatch.h"
// Logging
#include "../Logging/Log.h"
// Math
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Ray.h"
#include "../Math/RayHit.h"
#include "../Math/Rectangle.h"
#include "../Math/BoundingBox.h"
#include "../Math/Matrix.h"
#include "../Math/Frustum.h"
#include "../Math/Plane.h"
#include "../Math/MathHelper.h"
//==============================

#define SPARTAN_ASSERT(expression) assert(expression)
