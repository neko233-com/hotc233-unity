#pragma once

#include "InterpreterUtil.h"
#include "vm/Class.h"
#include "vm/Object.h"
#include "vm/String.h"

namespace hotc233
{
namespace interpreter
{

	struct Hotc233Vector3f
	{
		float x;
		float y;
		float z;
	};

	struct GodDomainUnityApiCache
	{
		bool probed;
		Il2CppClass* gameObjectClass;
		Il2CppClass* transformClass;
		Il2CppClass* cameraClass;
		Il2CppClass* physicsClass;
		Il2CppClass* behaviourClass;
		Il2CppClass* objectClass;
		Il2CppClass* inputClass;
		Il2CppClass* audioSourceClass;
		Il2CppClass* animatorClass;
		Il2CppClass* hotUpdatePrefabProbeClass;
		const MethodInfo* goCtorWithName;
		const MethodInfo* goSetActive;
		const MethodInfo* goGetComponentType;
		const MethodInfo* goAddComponentType;
		const MethodInfo* goCompareTag;
		const MethodInfo* transformSetPosition;
		const MethodInfo* transformSetRotation;
		const MethodInfo* transformSetLocalScale;
		const MethodInfo* transformFind;
		const MethodInfo* transformGetPosition;
		const MethodInfo* cameraWorldToScreenPoint;
		const MethodInfo* behaviourSetEnabled;
		const MethodInfo* inputGetAxisRaw;
		const MethodInfo* audioSourceSetVolume;
		const MethodInfo* audioSourceGetVolume;
		const MethodInfo* animatorSetFloat;
		const MethodInfo* animatorGetFloat;
		const MethodInfo* objectInstantiate;
		const MethodInfo* objectDestroy;
		const MethodInfo* objectSetName;
		const MethodInfo* physicsRaycast;
		Il2CppString* defaultGoName;
		Il2CppString* defaultTag;
		Il2CppString* childName;
		Il2CppString* horizontalAxisName;
		Il2CppString* speedParamName;
	};

	GodDomainUnityApiCache& GodDomainGetUnityApiCache();
	bool GodDomainEnsureUnityApiCache();

	IL2CPP_FORCE_INLINE void GodDomainInvokeDestroy(Il2CppObject* obj)
	{
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		if (obj == nullptr || c.objectDestroy == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.objectDestroy);
		typedef void(*ObjectDestroyMethod)(Il2CppObject*, const MethodInfo*);
		((ObjectDestroyMethod)c.objectDestroy->methodPointer)(obj, c.objectDestroy);
	}

	IL2CPP_FORCE_INLINE Il2CppObject* GodDomainInvokeInstantiate(Il2CppObject* templateObj)
	{
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		if (templateObj == nullptr || c.objectInstantiate == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.objectInstantiate);
		typedef Il2CppObject*(*ObjectInstantiateMethod)(Il2CppObject*, const MethodInfo*);
		return ((ObjectInstantiateMethod)c.objectInstantiate->methodPointer)(templateObj, c.objectInstantiate);
	}

	IL2CPP_FORCE_INLINE void GodDomainInvokeSetActive(Il2CppObject* go, bool active)
	{
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		if (go == nullptr || c.goSetActive == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.goSetActive);
		bool activeArg = active;
		void* args[1] = { &activeArg };
		c.goSetActive->invoker_method(
			c.goSetActive->methodPointerCallByInterp,
			const_cast<MethodInfo*>(c.goSetActive),
			go,
			args,
			nullptr);
	}

	IL2CPP_FORCE_INLINE Il2CppObject* GodDomainCreateGameObjectNamed(const char* name)
	{
		if (!GodDomainEnsureUnityApiCache())
		{
			return nullptr;
		}
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		Il2CppObject* obj = il2cpp::vm::Object::New(c.gameObjectClass);
		if (obj == nullptr || c.goCtorWithName == nullptr)
		{
			return nullptr;
		}
		Il2CppString* nameStr = il2cpp::vm::String::New(name);
		if (nameStr == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.goCtorWithName);
		typedef void(*GameObjectCtorWithNameMethod)(Il2CppObject*, Il2CppString*, const MethodInfo*);
		((GameObjectCtorWithNameMethod)c.goCtorWithName->methodPointer)(obj, nameStr, c.goCtorWithName);
		if (c.objectSetName != nullptr)
		{
			RuntimeInitClassCCtorWithoutInitClass(c.objectSetName);
			typedef void(*ObjectSetNameMethod)(Il2CppObject*, Il2CppString*, const MethodInfo*);
			((ObjectSetNameMethod)c.objectSetName->methodPointer)(obj, nameStr, c.objectSetName);
		}
		return obj;
	}

	IL2CPP_FORCE_INLINE Il2CppObject* GodDomainGetTransform(Il2CppObject* go)
	{
		if (go == nullptr || !GodDomainEnsureUnityApiCache())
		{
			return nullptr;
		}
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		const MethodInfo* getTransform = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, "get_transform", 0);
		if (getTransform == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(getTransform);
		Il2CppObject* tr = nullptr;
		getTransform->invoker_method(getTransform->methodPointer, getTransform, go, nullptr, &tr);
		return tr;
	}

	IL2CPP_FORCE_INLINE void GodDomainSetTransformPosition(Il2CppObject* transformObj, void* v3)
	{
		GodDomainUnityApiCache& c = GodDomainGetUnityApiCache();
		if (transformObj == nullptr || v3 == nullptr || c.transformSetPosition == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.transformSetPosition);
		Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(c.transformSetPosition, Hotc233DirectCallKind::InstanceVoidV3Setter);
		if (directPtr != nullptr)
		{
			typedef void(*DirectSetV3)(void*, void*);
			((DirectSetV3)directPtr)(transformObj, v3);
			return;
		}
		void* args[1] = { v3 };
		c.transformSetPosition->invoker_method(
			c.transformSetPosition->methodPointerCallByInterp,
			const_cast<MethodInfo*>(c.transformSetPosition),
			transformObj,
			args,
			nullptr);
	}

	int32_t GodDomainRunUnityKernel(int32_t fastPathKind, int32_t iterations);
	uint32_t ClassifyUnityKernelFastPathKindFromName(const char* methodName);

}
}
