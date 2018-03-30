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

//= INCLUDES ================================
#include "ScriptInterface.h"
#include "../Core/Timer.h"
#include "../Logging/Log.h"
#include "../Graphics/Material.h"
#include "../Math/Vector3.h"
#include "../Math/Quaternion.h"
#include "../Scene/Components/RigidBody.h"
#include "../Scene/Components/Camera.h"
#include "../Scene/GameObject.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Input/DInput/DInput.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	void ScriptInterface::Register(asIScriptEngine* scriptEngine, Context* context)
	{
		m_context = context;
		m_scriptEngine = scriptEngine;

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
		RegisterMaterial();
		RegisterRenderable();
		RegisterCamera();
		RegisterRigidBody();
		RegisterGameObject();
		RegisterDebug();
	}

	void ScriptInterface::RegisterEnumerations()
	{
		// Log
		m_scriptEngine->RegisterEnum("LogType");
		m_scriptEngine->RegisterEnumValue("LogType", "Info",	int(Log::Info));
		m_scriptEngine->RegisterEnumValue("LogType", "Warning", int(Log::Warning));
		m_scriptEngine->RegisterEnumValue("LogType", "Error",	int(Log::Error));

		// Component types
		m_scriptEngine->RegisterEnum("ComponentType");
		m_scriptEngine->RegisterEnumValue("ComponentType", "AudioListener", int(ComponentType_AudioListener));
		m_scriptEngine->RegisterEnumValue("ComponentType", "AudioSource",	int(ComponentType_AudioSource));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Camera",		int(ComponentType_Camera));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Collider",		int(ComponentType_Collider));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Constraint",	int(ComponentType_Constraint));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Light",			int(ComponentType_Light));
		m_scriptEngine->RegisterEnumValue("ComponentType", "LineRenderer",	int(ComponentType_LineRenderer));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Renderable",	int(ComponentType_Renderable));
		m_scriptEngine->RegisterEnumValue("ComponentType", "RigidBody",		int(ComponentType_RigidBody));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Script",		int(ComponentType_Script));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Skybox",		int(ComponentType_Skybox));
		m_scriptEngine->RegisterEnumValue("ComponentType", "Transform",		int(ComponentType_Transform));

		// Button_Keyboard
		m_scriptEngine->RegisterEnum("Button_Keyboard");
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "Space",	int(Space));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "Q",		int(Q));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "W",		int(W));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "E",		int(E));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "R",		int(R));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "T",		int(T));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "Y",		int(Y));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "U",		int(U));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "I",		int(I));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "O",		int(O));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "P",		int(P));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "A",		int(A));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "S",		int(S));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "D",		int(D));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "F",		int(F));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "G",		int(G));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "H",		int(H));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "J",		int(J));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "K",		int(K));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "L",		int(L));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "Z",		int(Z));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "X",		int(X));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "C",		int(C));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "V",		int(V));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "B",		int(B));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "N",		int(N));
		m_scriptEngine->RegisterEnumValue("Button_Keyboard", "M",		int(M));

		// Button_Mouse
		m_scriptEngine->RegisterEnum("Button_Mouse");
		m_scriptEngine->RegisterEnumValue("Button_Mouse", "Left",	int(Click_Left));
		m_scriptEngine->RegisterEnumValue("Button_Mouse", "Middle",	int(Click_Middle));
		m_scriptEngine->RegisterEnumValue("Button_Mouse", "Right",	int(Click_Right));

		// ForceMode
		m_scriptEngine->RegisterEnum("ForceMode");
		m_scriptEngine->RegisterEnumValue("ForceMode", "Force",		int(Force));
		m_scriptEngine->RegisterEnumValue("ForceMode", "Impulse",	int(Impulse));
	}

	void ScriptInterface::RegisterTypes()
	{
		m_scriptEngine->RegisterInterface("ScriptBehavior");

		m_scriptEngine->RegisterObjectType("Settings", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("Input", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("Time", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("GameObject", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("Transform", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("Renderable", 0, asOBJ_REF | asOBJ_NOCOUNT);
		m_scriptEngine->RegisterObjectType("Material", 0, asOBJ_REF | asOBJ_NOCOUNT);
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
	void ScriptInterface::RegisterSettings()
	{

	}

	/*------------------------------------------------------------------------------
										[INPUT]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterInput()
	{
		m_scriptEngine->RegisterGlobalProperty("Input input", m_context->GetSubsystem<Input>());
		m_scriptEngine->RegisterObjectMethod("Input", "Vector2 &GetMousePosition()", asMETHOD(Input, GetMousePosition), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Input", "Vector2 &GetMouseDelta()", asMETHOD(Input, GetMouseDelta), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Input", "bool GetButtonKeyboard(Button_Keyboard key)", asMETHOD(Input, GetButtonKeyboard), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Input", "bool GetButtonMouse(Button_Mouse key)", asMETHOD(Input, GetButtonMouse), asCALL_THISCALL);
	}

	/*------------------------------------------------------------------------------
										[TIMER]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterTime()
	{
		m_scriptEngine->RegisterGlobalProperty("Time time", m_context->GetSubsystem<Timer>());
		m_scriptEngine->RegisterObjectMethod("Time", "float GetDeltaTime()", asMETHOD(Timer, GetDeltaTimeSec), asCALL_THISCALL);
	}

	/*------------------------------------------------------------------------------
										[GAMEOBJECT]
	------------------------------------------------------------------------------*/

	void ScriptInterface::RegisterGameObject()
	{
		m_scriptEngine->RegisterObjectMethod("GameObject", "GameObject &opAssign(const GameObject &in)", asMETHODPR(GameObject, operator =, (const GameObject&), GameObject&), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "int GetID()", asMETHOD(GameObject, GetID), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "string GetName()", asMETHOD(GameObject, GetName), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "void SetName(string)", asMETHOD(GameObject, SetName), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "bool IsActive()", asMETHOD(GameObject, IsActive), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "void SetActive(bool)", asMETHOD(GameObject, SetActive), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "Transform &GetTransform()", asMETHOD(GameObject, GetTransformRef), asCALL_THISCALL);	
		m_scriptEngine->RegisterObjectMethod("GameObject", "Camera &GetCamera()", asMETHOD(GameObject, GetComponent<Camera>), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "RigidBody &GetRigidBody()", asMETHOD(GameObject, GetComponent<RigidBody>), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("GameObject", "Renderable &GetRenderable()", asMETHOD(GameObject, GetComponent<Renderable>), asCALL_THISCALL);
	}

	/*------------------------------------------------------------------------------
										[TRANSFORM]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterTransform()
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
		m_scriptEngine->RegisterObjectMethod("Transform", "Transform &GetChildByName(string)", asMETHOD(Transform, GetChildByName), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Transform", "GameObject &GetGameObject()", asMETHOD(Transform, GetGameObject_Ref), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Transform", "void Translate(const Vector3& in)", asMETHOD(Transform, Translate), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Transform", "void Rotate(const Quaternion& in)", asMETHOD(Transform, Rotate), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Transform", "void RotateLocal(const Quaternion& in)", asMETHOD(Transform, RotateLocal), asCALL_THISCALL);
	}

	//= MATERIAL ====================================================================
	void ScriptInterface::RegisterMaterial()
	{
		m_scriptEngine->RegisterObjectMethod("Material", "void SetOffsetUV(Vector2)", asMETHOD(Material, SetOffset), asCALL_THISCALL);
	}
	//===============================================================================


	//= RENDERABLE ==================================================================
	void ScriptInterface::RegisterRenderable()
	{
		m_scriptEngine->RegisterObjectMethod("Renderable", "Material &GetMaterial()", asMETHOD(Renderable, GetMaterial_RefWeak), asCALL_THISCALL);
	}
	//===============================================================================

	//= CAMERA ======================================================================
	void ScriptInterface::RegisterCamera()
	{

	}
	//===============================================================================

	/*------------------------------------------------------------------------------
									[RIGIDBODY]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterRigidBody()
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
	void ScriptInterface::RegisterMathHelper()
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

	void ScriptInterface::RegisterVector2()
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
	void ScriptInterface::RegisterVector3()
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

	void ConstructorQuaternionFloats(float x, float y, float z, float w, Quaternion* self)
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

	void ScriptInterface::RegisterQuaternion()
	{
		//= CONSTRUCTORS/DESTRUCTOR ====================================================================================================================================================
		m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructorQuaternion), asCALL_CDECL_OBJLAST);
		m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f(const Quaternion &in)", asFUNCTION(CopyConstructorQuaternion), asCALL_CDECL_OBJLAST);
		m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(ConstructorQuaternionFloats), asCALL_CDECL_OBJLAST);
		m_scriptEngine->RegisterObjectBehaviour("Quaternion", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DestructQuaternion), asCALL_CDECL_OBJLAST);
		//==============================================================================================================================================================================

		//= PROPERTIES ===========================================================================
		m_scriptEngine->RegisterObjectProperty("Quaternion", "float x", asOFFSET(Quaternion, x));
		m_scriptEngine->RegisterObjectProperty("Quaternion", "float y", asOFFSET(Quaternion, y));
		m_scriptEngine->RegisterObjectProperty("Quaternion", "float z", asOFFSET(Quaternion, z));
		m_scriptEngine->RegisterObjectProperty("Quaternion", "float w", asOFFSET(Quaternion, w));
		//========================================================================================

		//= OPERATORS ============================================================================================================================================================================
		m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion &opAssign(const Quaternion &in)", asMETHODPR(Quaternion, operator=, (const Quaternion&), Quaternion&), asCALL_THISCALL);
		m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion &opMulAssign(const Quaternion &in)", asFUNCTION(QuaternionMulAssignQuaternion), asCALL_CDECL_OBJLAST);
		m_scriptEngine->RegisterObjectMethod("Quaternion", "Quaternion opMul(const Quaternion &in)", asFUNCTION(QuaternionMulQuaternion), asCALL_CDECL_OBJFIRST);
		//========================================================================================================================================================================================

		//= FUNCTIONS ============================================================================================================================================================================
		m_scriptEngine->RegisterObjectMethod("Quaternion", "Vector3 ToEulerAngles()", asMETHOD(Quaternion, ToEulerAngles), asCALL_THISCALL);
		m_scriptEngine->RegisterGlobalFunction("Quaternion FromLookRotation(const Vector3& in, const Vector3& in)", asFUNCTIONPR(Quaternion::FromLookRotation, (const Vector3&, const Vector3&), Quaternion), asCALL_CDECL);
		//========================================================================================================================================================================================

		//= STATIC FUNCTIONS =====================================================================================================================================================================
		m_scriptEngine->RegisterGlobalFunction("Quaternion QuaternionFromEuler(float, float, float)", asFUNCTIONPR(Quaternion::FromEulerAngles, (float, float, float), Quaternion), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("Quaternion QuaternionFromEuler(const Vector3& in)", asFUNCTIONPR(Quaternion::FromEulerAngles, (const Vector3&), Quaternion), asCALL_CDECL);
		//========================================================================================================================================================================================
	}

	/*------------------------------------------------------------------------------
												[MATH]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterMath()
	{
		//m_scriptEngine->RegisterGlobalFunction("float Clamp(float, float)", asFUNCTIONPR(Clamp, (float, float), float), asCALL_CDECL);
	}

	/*------------------------------------------------------------------------------
										[LOG]
	------------------------------------------------------------------------------*/
	void ScriptInterface::RegisterDebug()
	{
		m_scriptEngine->RegisterGlobalFunction("void Log(const string& in, LogType)", asFUNCTIONPR(Log::Write, (const string&, Log::LogType), void), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("void Log(int, LogType)", asFUNCTIONPR(Log::Write, (int, Log::LogType), void), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("void Log(bool, LogType)", asFUNCTIONPR(Log::Write, (bool, Log::LogType), void), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("void Log(float, LogType)", asFUNCTIONPR(Log::Write, (float, Log::LogType), void), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("void Log(const Vector3& in, LogType)", asFUNCTIONPR(Log::Write, (const Vector3&, Log::LogType), void), asCALL_CDECL);
		m_scriptEngine->RegisterGlobalFunction("void Log(const Quaternion& in, LogType)", asFUNCTIONPR(Log::Write, (const Quaternion&, Log::LogType), void), asCALL_CDECL);
	}
}