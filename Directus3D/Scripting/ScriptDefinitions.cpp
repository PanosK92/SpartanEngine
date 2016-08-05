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

//= INCLUDES =======================
#include "ScriptDefinitions.h"
#include <string>
#include "../IO/Log.h"
#include "../Components/RigidBody.h"
#include "../Core/Settings.h"
#include "../Components/Camera.h"
#include "../Core/GameObject.h"
#include "../Components/Transform.h"
#include "../Math/Vector3.h"
#include "../Math/Matrix.h"
#include "../Math/Quaternion.h"
#include "../Math/MathHelper.h"
//==================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

void ScriptDefinitions::Register(asIScriptEngine* scriptEngine, Input* input, Timer* timer)
{
	m_scriptEngine = scriptEngine;
	m_input = input;
	m_timer = timer;

	RegisterEnumerations();
	RegisterTypes();
	RegisterSettings();
	RegisterInput();
	RegisterTime();
	RegisterMathHelper();
	RegisterVector2();
	RegisterVector3();
	RegisterQuaternion();
	RegisterTransform();
	RegisterCamera();
	RegisterRigidBody();
	RegisterGameObject();
	RegisterDebug();
}

void ScriptDefinitions::RegisterEnumerations()
{
	// Log
	m_scriptEngine->RegisterEnum("LogType");
	m_scriptEngine->RegisterEnumValue("LogType", "Info", int(Log::Info));
	m_scriptEngine->RegisterEnumValue("LogType", "Warning", int(Log::Warning));
	m_scriptEngine->RegisterEnumValue("LogType", "Error", int(Log::Error));
	m_scriptEngine->RegisterEnumValue("LogType", "Undefined", int(Log::Undefined));

	// KeyCode
	m_scriptEngine->RegisterEnum("KeyCode");
	m_scriptEngine->RegisterEnumValue("KeyCode", "Space", int(Space));
	m_scriptEngine->RegisterEnumValue("KeyCode", "Q", int(Q));
	m_scriptEngine->RegisterEnumValue("KeyCode", "W", int(W));
	m_scriptEngine->RegisterEnumValue("KeyCode", "E", int(E));
	m_scriptEngine->RegisterEnumValue("KeyCode", "R", int(R));
	m_scriptEngine->RegisterEnumValue("KeyCode", "T", int(T));
	m_scriptEngine->RegisterEnumValue("KeyCode", "Y", int(Y));
	m_scriptEngine->RegisterEnumValue("KeyCode", "U", int(U));
	m_scriptEngine->RegisterEnumValue("KeyCode", "I", int(I));
	m_scriptEngine->RegisterEnumValue("KeyCode", "O", int(O));
	m_scriptEngine->RegisterEnumValue("KeyCode", "P", int(P));
	m_scriptEngine->RegisterEnumValue("KeyCode", "A", int(A));
	m_scriptEngine->RegisterEnumValue("KeyCode", "S", int(S));
	m_scriptEngine->RegisterEnumValue("KeyCode", "D", int(D));
	m_scriptEngine->RegisterEnumValue("KeyCode", "F", int(F));
	m_scriptEngine->RegisterEnumValue("KeyCode", "G", int(G));
	m_scriptEngine->RegisterEnumValue("KeyCode", "H", int(H));
	m_scriptEngine->RegisterEnumValue("KeyCode", "J", int(J));
	m_scriptEngine->RegisterEnumValue("KeyCode", "K", int(K));
	m_scriptEngine->RegisterEnumValue("KeyCode", "L", int(L));
	m_scriptEngine->RegisterEnumValue("KeyCode", "Z", int(Z));
	m_scriptEngine->RegisterEnumValue("KeyCode", "X", int(X));
	m_scriptEngine->RegisterEnumValue("KeyCode", "C", int(C));
	m_scriptEngine->RegisterEnumValue("KeyCode", "V", int(V));
	m_scriptEngine->RegisterEnumValue("KeyCode", "B", int(B));
	m_scriptEngine->RegisterEnumValue("KeyCode", "N", int(N));
	m_scriptEngine->RegisterEnumValue("KeyCode", "M", int(M));

	// ForceMode
	m_scriptEngine->RegisterEnum("ForceMode");
	m_scriptEngine->RegisterEnumValue("ForceMode", "Force", int(Force));
	m_scriptEngine->RegisterEnumValue("ForceMode", "Impulse", int(Impulse));

	// EngineMode
	m_scriptEngine->RegisterEnum("EngineMode");
	m_scriptEngine->RegisterEnumValue("EngineMode", "Editor_Play", int(Editor_Play));
	m_scriptEngine->RegisterEnumValue("EngineMode", "Editor_Stop", int(Editor_Stop));
	m_scriptEngine->RegisterEnumValue("EngineMode", "Editor_Pause", int(Editor_Pause));
	m_scriptEngine->RegisterEnumValue("EngineMode", "Build_Developer", int(Build_Developer));
	m_scriptEngine->RegisterEnumValue("EngineMode", "Build_Release", int(Build_Release));

	// Space
	m_scriptEngine->RegisterEnum("Space");
	m_scriptEngine->RegisterEnumValue("Space", "Local", int(Transform::Local));
	m_scriptEngine->RegisterEnumValue("Space", "World", int(Transform::World));
}

void ScriptDefinitions::RegisterTypes()
{
	m_scriptEngine->RegisterInterface("ScriptBehavior");

	m_scriptEngine->RegisterObjectType("Settings", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("Input", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("Time", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("GameObject", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("Transform", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("Camera", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("RigidBody", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("MathHelper", 0, asOBJ_REF | asOBJ_NOCOUNT);
	m_scriptEngine->RegisterObjectType("Vector2", sizeof(Vector2), asOBJ_VALUE | asOBJ_APP_CLASS | asOBJ_APP_CLASS_CONSTRUCTOR | asOBJ_APP_CLASS_COPY_CONSTRUCTOR | asOBJ_APP_CLASS_DESTRUCTOR);
	m_scriptEngine->RegisterObjectType("Vector3", sizeof(Vector3), asOBJ_VALUE | asOBJ_APP_CLASS | asOBJ_APP_CLASS_CONSTRUCTOR | asOBJ_APP_CLASS_COPY_CONSTRUCTOR | asOBJ_APP_CLASS_DESTRUCTOR);
	m_scriptEngine->RegisterObjectType("Quaternion", sizeof(Quaternion), asOBJ_VALUE | asOBJ_APP_CLASS | asOBJ_APP_CLASS_CONSTRUCTOR | asOBJ_APP_CLASS_COPY_CONSTRUCTOR | asOBJ_APP_CLASS_DESTRUCTOR);
}

/*------------------------------------------------------------------------------
									[SETTINGS]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterSettings()
{

}

/*------------------------------------------------------------------------------
									[INPUT]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterInput()
{
	m_scriptEngine->RegisterGlobalProperty("Input input", m_input);
	m_scriptEngine->RegisterObjectMethod("Input", "Vector2 GetMousePosition()", asMETHOD(Input, GetMousePosition), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Input", "Vector2 GetMousePositionDelta()", asMETHOD(Input, GetMousePositionDelta), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Input", "bool GetKey(KeyCode key)", asMETHOD(Input, GetKey), asCALL_THISCALL);
}

/*------------------------------------------------------------------------------
									[TIMER]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterTime()
{
	m_scriptEngine->RegisterGlobalProperty("Time time", m_timer);
	m_scriptEngine->RegisterObjectMethod("Time", "float GetDeltaTime()", asMETHOD(Timer, GetDeltaTime), asCALL_THISCALL);
	//m_scriptEngine->RegisterObjectProperty("Time", "float deltaTime", asOFFSET(Timer, GetDeltaTime));
}

/*------------------------------------------------------------------------------
									[GAMEOBJECT]
------------------------------------------------------------------------------*/

void ScriptDefinitions::RegisterGameObject()
{
	m_scriptEngine->RegisterObjectMethod("GameObject", "GameObject &opAssign(const GameObject &in)", asMETHODPR(GameObject, operator =, (const GameObject&), GameObject&), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "int GetID()", asMETHOD(GameObject, GetID), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "string GetName()", asMETHOD(GameObject, GetName), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "void SetName(string)", asMETHOD(GameObject, SetName), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "bool IsActive()", asMETHOD(GameObject, IsActive), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "void SetActive(bool)", asMETHOD(GameObject, SetActive), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "Transform &GetTransform()", asMETHOD(GameObject, GetTransform), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "bool HasCamera()", asMETHOD(GameObject, HasComponent<Camera>), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "Camera &GetCamera()", asMETHOD(GameObject, GetComponent<Camera>), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "bool HasRigidBody()", asMETHOD(GameObject, HasComponent<RigidBody>), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("GameObject", "RigidBody &GetRigidBody()", asMETHOD(GameObject, GetComponent<RigidBody>), asCALL_THISCALL);
}

/*------------------------------------------------------------------------------
									[TRANSFORM]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterTransform()
{
	m_scriptEngine->RegisterObjectMethod("Transform", "Transform &opAssign(const Transform &in)", asMETHODPR(Transform, operator =, (const Transform&), Transform&), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetPosition()", asMETHOD(Transform, GetPosition), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetPosition(Vector3)", asMETHOD(Transform, SetPosition), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetPositionLocal()", asMETHOD(Transform, GetPositionLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetPositionLocal(Vector3)", asMETHOD(Transform, SetPositionLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetScale()", asMETHOD(Transform, GetScale), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetScale(Vector3)", asMETHOD(Transform, SetScale), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetScaleLocal()", asMETHOD(Transform, GetScaleLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetScaleLocal(Vector3)", asMETHOD(Transform, SetScaleLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Quaternion GetRotation()", asMETHOD(Transform, GetRotation), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetRotation(Quaternion)", asMETHOD(Transform, SetRotation), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Quaternion GetRotationLocal()", asMETHOD(Transform, GetRotationLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void SetRotationLocal(Quaternion)", asMETHOD(Transform, SetRotationLocal), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetUp()", asMETHOD(Transform, GetUp), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetForward()", asMETHOD(Transform, GetForward), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Vector3 GetRight()", asMETHOD(Transform, GetRight), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Transform &GetRoot()", asMETHOD(Transform, GetRoot), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Transform &GetParent()", asMETHOD(Transform, GetParent), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "Transform &GetChildByIndex(int)", asMETHOD(Transform, GetChildByIndex), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "GameObject &GetGameObject()", asMETHOD(Transform, GetGameObject), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void Translate(const Vector3& in)", asMETHOD(Transform, Translate), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Transform", "void Rotate(const Quaternion& in, Space)", asMETHOD(Transform, Rotate), asCALL_THISCALL);
}

/*------------------------------------------------------------------------------
								[CAMERA]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterCamera()
{
}

/*------------------------------------------------------------------------------
								[RIGIDBODY]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterRigidBody()
{
	m_scriptEngine->RegisterObjectMethod("RigidBody", "RigidBody &opAssign(const RigidBody &in)", asMETHODPR(RigidBody, operator =, (const RigidBody&), RigidBody&), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("RigidBody", "void ApplyForce(Vector3, ForceMode)", asMETHOD(RigidBody, ApplyForce), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("RigidBody", "void ApplyForceAtPosition(Vector3, Vector3, ForceMode)", asMETHOD(RigidBody, ApplyForceAtPosition), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("RigidBody", "void ApplyTorque(Vector3, ForceMode)", asMETHOD(RigidBody, ApplyTorque), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("RigidBody", "void SetRotation(Quaternion)", asMETHOD(RigidBody, SetRotation), asCALL_THISCALL);
}

/*------------------------------------------------------------------------------
									[MATH HELPER]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterMathHelper()
{
	m_scriptEngine->RegisterGlobalFunction("float Lerp(float, float, float)", asFUNCTIONPR(Lerp, (float, float, float), float), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("float Abs(float)", asFUNCTIONPR(Abs, (float), float), asCALL_CDECL);
}

/*------------------------------------------------------------------------------
									[VECTOR2]
------------------------------------------------------------------------------*/
void ConstructorVector2(Vector2* other)
{
	new(other) Vector2(0, 0);
}

void CopyConstructorVector2(const Vector2& in, Vector2* other)
{
	new(other) Vector2(in.x, in.y);
}

void ConstructorVector2Floats(float x, float y, Vector2* other)
{
	new(other) Vector2(x, y);
}

void DestructVector2(Vector2* other)
{
	other->~Vector2();
}

static Vector2& Vector2AddAssignVector2(const Vector2& other, Vector2* self)
{
	return *self = *self + other;
}

void ScriptDefinitions::RegisterVector2()
{
	m_scriptEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructorVector2), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT, "void f(const Vector2 &in)", asFUNCTION(CopyConstructorVector2), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT, "void f(float, float)", asFUNCTION(ConstructorVector2Floats), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DestructVector2), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector2", "Vector2 &opAddAssign(const Vector2 &in)", asFUNCTION(Vector2AddAssignVector2), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector2", "Vector2 &opAssign(const Vector2 &in)", asMETHODPR(Vector2, operator=, (const Vector2&), Vector2&), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectProperty("Vector2", "float x", asOFFSET(Vector2, x));
	m_scriptEngine->RegisterObjectProperty("Vector2", "float y", asOFFSET(Vector2, y));
}

/*------------------------------------------------------------------------------
								[VECTOR3]
------------------------------------------------------------------------------*/
void ConstructorVector3(Vector3* self)
{
	new(self) Vector3(0, 0, 0);
}

void CopyConstructorVector3(const Vector3& other, Vector3* self)
{
	new(self) Vector3(other.x, other.y, other.z);
}

void ConstructorVector3Floats(float x, float y, float z, Vector3* self)
{
	new(self) Vector3(x, y, z);
}

void DestructVector3(Vector3* self)
{
	self->~Vector3();
}

static Vector3& Vector3Assignment(const Vector3& other, Vector3* self)
{
	return *self = other;
}

//= Addition ===================================================================
static Vector3 Vector3AddVector3(const Vector3& other, Vector3* self)
{
	return *self + other;
}

static Vector3& Vector3AddAssignVector3(const Vector3& other, Vector3* self)
{
	return *self = *self + other;
}

//= Subtraction ================================================================
static Vector3& Vector3SubAssignVector3(const Vector3& other, Vector3* self)
{
	return *self = *self - other;
}

//= Multiplication =============================================================
static Vector3& Vector3MulAssignVector3(const Vector3& other, Vector3* self)
{
	return *self = *self * other;
}

static Vector3& Vector3MulAssignFloat(float value, Vector3* self)
{
	return *self = *self * value;
}

static Vector3 Vector3MulVector3(const Vector3& other, Vector3* self)
{
	return *self * other;
}

static Vector3 Vector3MulFloat(float value, Vector3* self)
{
	return *self * value;
}

//= Registration ================================================================
void ScriptDefinitions::RegisterVector3()
{
	// operator overloads http://www.angelcode.com/angelscript/sdk/docs/manual/doc_script_class_ops.html

	m_scriptEngine->RegisterObjectBehaviour("Vector3", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructorVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector3", asBEHAVE_CONSTRUCT, "void f(const Vector3 &in)", asFUNCTION(CopyConstructorVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector3", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTION(ConstructorVector3Floats), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Vector3", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DestructVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 &opAssign(const Vector3 &in)", asFUNCTION(Vector3Assignment), asCALL_CDECL_OBJLAST);

	//= Addition ===================================================================
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 opAdd(const Vector3 &in)", asFUNCTION(Vector3AddVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 &opAddAssign(const Vector3 &in)", asFUNCTION(Vector3AddAssignVector3), asCALL_CDECL_OBJLAST);

	//= Subtraction ================================================================
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 &opSubAssign(const Vector3 &in)", asFUNCTION(Vector3SubAssignVector3), asCALL_CDECL_OBJLAST);

	//= Multiplication =============================================================
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 &opMulAssign(const Vector3 &in)", asFUNCTION(Vector3MulAssignVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 &opMulAssign(float)", asFUNCTION(Vector3MulAssignFloat), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 opMul(const Vector3 &in)", asFUNCTION(Vector3MulVector3), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 opMul(float)", asFUNCTION(Vector3MulFloat), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Vector3", "Vector3 opMul_r(float)", asFUNCTION(Vector3MulFloat), asCALL_CDECL_OBJLAST);

	// x, y, z components
	m_scriptEngine->RegisterObjectProperty("Vector3", "float x", asOFFSET(Vector3, x));
	m_scriptEngine->RegisterObjectProperty("Vector3", "float y", asOFFSET(Vector3, y));
	m_scriptEngine->RegisterObjectProperty("Vector3", "float z", asOFFSET(Vector3, z));
}

/*------------------------------------------------------------------------------
									[QUATERNION]
------------------------------------------------------------------------------*/
void ConstructorQuaternion(Quaternion* self)
{
	new(self) Quaternion(0, 0, 0, 1);
}

void CopyConstructorQuaternion(const Quaternion& other, Quaternion* self)
{
	new(self) Quaternion(other.x, other.y, other.z, other.w);
}

void ConstructorQuaternionDoubles(double x, double y, double z, double w, Quaternion* self)
{
	new(self) Quaternion(x, y, z, w);
}

void DestructQuaternion(Quaternion* in)
{
	in->~Quaternion();
}

static Quaternion& QuaternionMulAssignQuaternion(const Quaternion& other, Quaternion* self)
{
	return *self = *self * other;
}

static Quaternion QuaternionMulQuaternion(const Quaternion& other, Quaternion* self)
{
	return *self * other;
}

void ScriptDefinitions::RegisterQuaternion()
{
	//= CONSTRUCTORS/DESTRUCTOR ==============================================================================================================================================================
	m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructorQuaternion), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f(const Quaternion &in)", asFUNCTION(CopyConstructorQuaternion), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f(double, double, double, double)", asFUNCTION(ConstructorQuaternionDoubles), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DestructQuaternion), asCALL_CDECL_OBJLAST);
	//========================================================================================================================================================================================

	//= PROPERTIES ===========================================================================
	m_scriptEngine->RegisterObjectProperty("Quaternion", "double x", asOFFSET(Quaternion, x));
	m_scriptEngine->RegisterObjectProperty("Quaternion", "double y", asOFFSET(Quaternion, y));
	m_scriptEngine->RegisterObjectProperty("Quaternion", "double z", asOFFSET(Quaternion, z));
	m_scriptEngine->RegisterObjectProperty("Quaternion", "double w", asOFFSET(Quaternion, w));
	//========================================================================================

	//= OPERATORS ============================================================================================================================================================================
	m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion &opAssign(const Quaternion &in)", asMETHODPR(Quaternion, operator=, (const Quaternion&), Quaternion&), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion &opMulAssign(const Quaternion &in)", asFUNCTION(QuaternionMulAssignQuaternion), asCALL_CDECL_OBJLAST);
	m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion opMul(const Quaternion &in)", asFUNCTION(QuaternionMulQuaternion), asCALL_CDECL_OBJFIRST);
	//========================================================================================================================================================================================

	//= FUNCTIONS ============================================================================================================================================================================
	m_scriptEngine->RegisterObjectMethod("Quaternion", "Vector3 ToEulerAngles()", asMETHOD(Quaternion, ToEulerAngles), asCALL_THISCALL);
	m_scriptEngine->RegisterObjectMethod("Quaternion", "bool FromLookRotation(const Vector3& in, const Vector3& in)", asMETHOD(Quaternion, FromLookRotation), asCALL_THISCALL);
	//========================================================================================================================================================================================

	//= STATIC FUNCTIONS =====================================================================================================================================================================
	m_scriptEngine->RegisterGlobalFunction("Quaternion QuaternionFromEuler(float, float, float)", asFUNCTIONPR(Quaternion::FromEulerAngles, (float, float, float), Quaternion), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("Quaternion QuaternionFromEuler(const Vector3& in)", asFUNCTIONPR(Quaternion::FromEulerAngles, (const Vector3&), Quaternion), asCALL_CDECL);
	//========================================================================================================================================================================================
}

/*------------------------------------------------------------------------------
									[DEBUG]
------------------------------------------------------------------------------*/
void ScriptDefinitions::RegisterDebug()
{
	m_scriptEngine->RegisterGlobalFunction("void Log(string, LogType)", asFUNCTIONPR(Log::Write, (string, Log::LogType), void), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("void Log(int, LogType)", asFUNCTIONPR(Log::Write, (int, Log::LogType), void), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("void Log(float, LogType)", asFUNCTIONPR(Log::Write, (float, Log::LogType), void), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("void Log(const Vector3& in, LogType)", asFUNCTIONPR(Log::Write, (const Vector3&, Log::LogType), void), asCALL_CDECL);
	m_scriptEngine->RegisterGlobalFunction("void Log(const Quaternion& in, LogType)", asFUNCTIONPR(Log::Write, (const Quaternion&, Log::LogType), void), asCALL_CDECL);
}
