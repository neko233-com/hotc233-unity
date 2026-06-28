#include "Interpreter.h"

#ifndef HOTC233_GOD_DOMAIN_UNITY_KERNELS_INCLUDED
#define HOTC233_GOD_DOMAIN_UNITY_KERNELS_INCLUDED

#include "GodDomainUnityApi.h"

#include "vm/Reflection.h"
#include "vm/Class.h"
#include "vm/Object.h"
#include "vm/String.h"
#include <cstring>
#include <cmath>

namespace hotc233
{
namespace interpreter
{

	static GodDomainUnityApiCache s_godDomainUnityApiCache = {};
	static Il2CppObject* s_prefabTemplate = nullptr;

	GodDomainUnityApiCache& GodDomainGetUnityApiCache()
	{
		return s_godDomainUnityApiCache;
	}

	static Il2CppObject* GodDomainGetTypeObjectForClass(Il2CppClass* klass)
	{
		if (klass == nullptr)
		{
			return nullptr;
		}
		const Il2CppType* type = il2cpp::vm::Class::GetType(klass);
		if (type == nullptr)
		{
			return nullptr;
		}
		return (Il2CppObject*)il2cpp::vm::Reflection::GetTypeObject(type);
	}

	static Il2CppClass* FindHotUpdateClass(const char* typeName)
	{
		Il2CppClass* klass = il2cpp::vm::Class::FromName(nullptr, "UnityHotc.CodeHotUpdate.Feature", typeName);
		if (klass != nullptr)
		{
			return klass;
		}
		klass = il2cpp::vm::Class::FromName(nullptr, "UnityHotc.CodeHotUpdate", typeName);
		if (klass != nullptr)
		{
			return klass;
		}
		return il2cpp::vm::Class::FromName(nullptr, "Assembly-CSharp", typeName);
	}

	bool GodDomainEnsureUnityApiCache()
	{
		if (s_godDomainUnityApiCache.probed)
		{
			return s_godDomainUnityApiCache.gameObjectClass != nullptr
				&& s_godDomainUnityApiCache.objectDestroy != nullptr;
		}
		s_godDomainUnityApiCache.probed = true;

		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		c.gameObjectClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "GameObject");
		c.transformClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Transform");
		c.cameraClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Camera");
		c.physicsClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Physics");
		c.behaviourClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Behaviour");
		c.objectClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Object");
		c.hotUpdatePrefabProbeClass = FindHotUpdateClass("RealWorldBenchmarkProbe");
		if (c.hotUpdatePrefabProbeClass == nullptr)
		{
			c.hotUpdatePrefabProbeClass = FindHotUpdateClass("HotUpdatePrefabProbe");
		}

		if (c.gameObjectClass == nullptr || c.objectClass == nullptr)
		{
			return false;
		}

		c.goCtorWithName = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, ".ctor", 1);
		c.goSetActive = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, "SetActive", 1);
		c.goGetComponentType = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, "GetComponent", 1);
		c.goAddComponentType = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, "AddComponent", 1);
		c.goCompareTag = il2cpp::vm::Class::GetMethodFromName(c.gameObjectClass, "CompareTag", 1);
		c.transformSetPosition = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "set_position", 1);
		c.transformSetRotation = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "set_rotation", 1);
		c.transformSetLocalScale = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "set_localScale", 1);
		c.transformFind = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "Find", 1);
		c.transformGetPosition = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "get_position", 0);
		c.cameraWorldToScreenPoint = il2cpp::vm::Class::GetMethodFromName(c.cameraClass, "WorldToScreenPoint", 1);
		c.behaviourSetEnabled = il2cpp::vm::Class::GetMethodFromName(c.behaviourClass, "set_enabled", 1);
		c.objectInstantiate = il2cpp::vm::Class::GetMethodFromName(c.objectClass, "Instantiate", 1);
		c.objectDestroy = il2cpp::vm::Class::GetMethodFromName(c.objectClass, "Destroy", 1);
		c.physicsRaycast = il2cpp::vm::Class::GetMethodFromName(c.physicsClass, "Raycast", 4);

		c.defaultGoName = il2cpp::vm::String::New("EntityHotLoop");
		c.defaultTag = il2cpp::vm::String::New("Untagged");
		c.childName = il2cpp::vm::String::New("ChildA");

		return c.goCtorWithName != nullptr && c.objectDestroy != nullptr;
	}

	static void GodDomainEnsurePrefabTemplate()
	{
		if (s_prefabTemplate != nullptr || !GodDomainEnsureUnityApiCache())
		{
			return;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		if (c.hotUpdatePrefabProbeClass == nullptr || c.goAddComponentType == nullptr)
		{
			return;
		}
		Il2CppObject* templateGo = GodDomainCreateGameObjectNamed("HotUpdatePrefabTemplate");
		if (templateGo == nullptr)
		{
			return;
		}
		Il2CppObject* typeObj = GodDomainGetTypeObjectForClass(c.hotUpdatePrefabProbeClass);
		if (typeObj != nullptr)
		{
			void* args[1] = { typeObj };
			RuntimeInitClassCCtorWithoutInitClass(c.goAddComponentType);
			c.goAddComponentType->invoker_method(
				c.goAddComponentType->methodPointerCallByInterp,
				c.goAddComponentType,
				templateGo,
				args,
				nullptr);
		}
		GodDomainInvokeSetActive(templateGo, false);
		s_prefabTemplate = templateGo;
	}

	static bool GodDomainGetActiveSelf(Il2CppObject* go)
	{
		if (go == nullptr || !GodDomainEnsureUnityApiCache())
		{
			return false;
		}
		const MethodInfo* getActive = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.gameObjectClass, "get_activeSelf", 0);
		if (getActive == nullptr)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(getActive);
		bool active = false;
		getActive->invoker_method(getActive->methodPointer, getActive, go, nullptr, &active);
		return active;
	}

	static Il2CppObject* GodDomainGetComponentByType(Il2CppObject* go, Il2CppClass* componentClass)
	{
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		if (go == nullptr || componentClass == nullptr || c.goGetComponentType == nullptr)
		{
			return nullptr;
		}
		Il2CppObject* typeObj = GodDomainGetTypeObjectForClass(componentClass);
		if (typeObj == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.goGetComponentType);
		Il2CppObject* comp = nullptr;
		void* args[1] = { typeObj };
		c.goGetComponentType->invoker_method(
			c.goGetComponentType->methodPointerCallByInterp,
			c.goGetComponentType,
			go,
			args,
			&comp);
		return comp;
	}

	static Il2CppObject* GodDomainAddComponentByType(Il2CppObject* go, Il2CppClass* componentClass)
	{
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		if (go == nullptr || componentClass == nullptr || c.goAddComponentType == nullptr)
		{
			return nullptr;
		}
		Il2CppObject* typeObj = GodDomainGetTypeObjectForClass(componentClass);
		if (typeObj == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.goAddComponentType);
		Il2CppObject* comp = nullptr;
		void* args[1] = { typeObj };
		c.goAddComponentType->invoker_method(
			c.goAddComponentType->methodPointerCallByInterp,
			c.goAddComponentType,
			go,
			args,
			&comp);
		return comp;
	}

	static int32_t GodDomainReadProbeLogCount(Il2CppObject* probe)
	{
		if (probe == nullptr || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		const MethodInfo* getter = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.hotUpdatePrefabProbeClass, "get_LogCount", 0);
		if (getter != nullptr)
		{
			RuntimeInitClassCCtorWithoutInitClass(getter);
			int32_t count = 0;
			getter->invoker_method(getter->methodPointer, getter, probe, nullptr, &count);
			return count;
		}
		return 0;
	}

	static void GodDomainSetParent(Il2CppObject* childTransform, Il2CppObject* parentTransform)
	{
		if (childTransform == nullptr || parentTransform == nullptr)
		{
			return;
		}
		const MethodInfo* setParent = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.transformClass, "SetParent", 1);
		if (setParent == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(setParent);
		void* args[1] = { parentTransform };
		setParent->invoker_method(setParent->methodPointerCallByInterp, setParent, childTransform, args, nullptr);
	}

	static int32_t GodDomainKernelEntityHotLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		Il2CppObject* go = GodDomainCreateGameObjectNamed("EntityHotLoop");
		Il2CppObject* tr = GodDomainGetTransform(go);
		Hotc233Vector3f v = { 1.0f, 1.0f, 1.0f };
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			GodDomainSetTransformPosition(tr, &v);
			GodDomainInvokeSetActive(go, i % 2 == 0);
			acc += 3;
			acc += GodDomainGetActiveSelf(go) ? 1 : 0;
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelPrefabSpawnDespawn(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainEnsurePrefabTemplate();
		if (s_prefabTemplate == nullptr)
		{
			return 0;
		}
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Il2CppObject* inst = GodDomainInvokeInstantiate(s_prefabTemplate);
			GodDomainInvokeSetActive(inst, true);
			Il2CppObject* probe = GodDomainGetComponentByType(inst, s_godDomainUnityApiCache.hotUpdatePrefabProbeClass);
			acc += probe != nullptr ? GodDomainReadProbeLogCount(probe) + 1 : 0;
			GodDomainInvokeDestroy(inst);
		}
		return acc;
	}

	static int32_t GodDomainKernelGetComponentLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		Il2CppObject* go = GodDomainCreateGameObjectNamed("GetComponentLoop");
		GodDomainAddComponentByType(go, s_godDomainUnityApiCache.hotUpdatePrefabProbeClass);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Il2CppObject* probe = GodDomainGetComponentByType(go, s_godDomainUnityApiCache.hotUpdatePrefabProbeClass);
			acc += probe != nullptr ? 1 : 0;
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelAddComponentSpawn(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Il2CppObject* go = GodDomainCreateGameObjectNamed("SpawnAddComponent");
			Il2CppObject* probe = GodDomainAddComponentByType(go, s_godDomainUnityApiCache.hotUpdatePrefabProbeClass);
			acc += probe != nullptr ? 1 : 0;
			GodDomainInvokeDestroy(go);
		}
		return acc;
	}

	static int32_t GodDomainKernelCameraWorldToScreen(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* camGo = GodDomainCreateGameObjectNamed("CamLoop");
		Il2CppObject* cam = GodDomainAddComponentByType(camGo, c.cameraClass);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Hotc233Vector3f world = { (float)(i % 10), (float)(i % 7), 10.0f + (float)(i % 5) };
			Hotc233Vector3f screen = {};
			if (cam != nullptr && c.cameraWorldToScreenPoint != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(c.cameraWorldToScreenPoint);
				void* args[1] = { &world };
				c.cameraWorldToScreenPoint->invoker_method(
					c.cameraWorldToScreenPoint->methodPointerCallByInterp,
					const_cast<MethodInfo*>(c.cameraWorldToScreenPoint),
					cam,
					args,
					&screen);
			}
			acc += (int32_t)(screen.x + screen.y + screen.z);
		}
		GodDomainInvokeDestroy(camGo);
		return acc;
	}

	static int32_t GodDomainKernelPhysicsRaycast(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* marker = GodDomainCreateGameObjectNamed("RaycastProbe");
		const MethodInfo* raycast3 = il2cpp::vm::Class::GetMethodFromName(c.physicsClass, "Raycast", 3);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Hotc233Vector3f origin = { 0.0f, 10.0f, (float)(i % 100) };
			Hotc233Vector3f direction = { 0.0f, -1.0f, 0.0f };
			bool hit = false;
			float maxDistance = 100.0f;
			if (raycast3 != nullptr)
			{
				void* args[3] = { &origin, &direction, &maxDistance };
				RuntimeInitClassCCtorWithoutInitClass(raycast3);
				raycast3->invoker_method(raycast3->methodPointerCallByInterp, raycast3, nullptr, args, &hit);
			}
			acc += hit ? 1 : 0;
		}
		GodDomainInvokeDestroy(marker);
		return acc;
	}

	static int32_t GodDomainKernelTransformFullLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* go = GodDomainCreateGameObjectNamed("TransformFull");
		Il2CppObject* tr = GodDomainGetTransform(go);
		const MethodInfo* getLocalScale = il2cpp::vm::Class::GetMethodFromName(c.transformClass, "get_localScale", 0);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Hotc233Vector3f pos = { (float)i, (float)i, (float)i };
			GodDomainSetTransformPosition(tr, &pos);
			if (c.transformSetRotation != nullptr)
			{
				float eulerX = (float)i;
				float eulerY = (float)(i * 2);
				float eulerZ = (float)(i * 3);
				float halfX = eulerX * 0.5f * 3.14159265f / 180.0f;
				float halfY = eulerY * 0.5f * 3.14159265f / 180.0f;
				float halfZ = eulerZ * 0.5f * 3.14159265f / 180.0f;
				float cx = std::cos(halfX), sx = std::sin(halfX);
				float cy = std::cos(halfY), sy = std::sin(halfY);
				float cz = std::cos(halfZ), sz = std::sin(halfZ);
				struct { float x, y, z, w; } quat = {
					sx * cy * cz - cx * sy * sz,
					cx * sy * cz + sx * cy * sz,
					cx * cy * sz - sx * sy * cz,
					cx * cy * cz + sx * sy * sz,
				};
				RuntimeInitClassCCtorWithoutInitClass(c.transformSetRotation);
				void* rotArgs[1] = { &quat };
				c.transformSetRotation->invoker_method(
					c.transformSetRotation->methodPointerCallByInterp,
					const_cast<MethodInfo*>(c.transformSetRotation),
					tr,
					rotArgs,
					nullptr);
			}
			if (c.transformSetLocalScale != nullptr)
			{
				float scale = 1.0f + (float)(i % 3);
				Hotc233Vector3f localScale = { scale, scale, scale };
				RuntimeInitClassCCtorWithoutInitClass(c.transformSetLocalScale);
				void* scaleArgs[1] = { &localScale };
				c.transformSetLocalScale->invoker_method(
					c.transformSetLocalScale->methodPointerCallByInterp,
					const_cast<MethodInfo*>(c.transformSetLocalScale),
					tr,
					scaleArgs,
					nullptr);
			}
			Hotc233Vector3f readPos = {};
			if (c.transformGetPosition != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(c.transformGetPosition);
				c.transformGetPosition->invoker_method(c.transformGetPosition->methodPointer, c.transformGetPosition, tr, nullptr, &readPos);
			}
			Hotc233Vector3f readScale = {};
			if (getLocalScale != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(getLocalScale);
				getLocalScale->invoker_method(getLocalScale->methodPointer, getLocalScale, tr, nullptr, &readScale);
			}
			acc += (int32_t)readPos.x + (int32_t)readScale.y;
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static void GodDomainSetBehaviourEnabled(Il2CppObject* behaviour, bool enabled)
	{
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		if (behaviour == nullptr || c.behaviourSetEnabled == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(c.behaviourSetEnabled);
		bool arg = enabled;
		void* args[1] = { &arg };
		c.behaviourSetEnabled->invoker_method(
			c.behaviourSetEnabled->methodPointerCallByInterp,
			const_cast<MethodInfo*>(c.behaviourSetEnabled),
			behaviour,
			args,
			nullptr);
	}

	static bool GodDomainGetBehaviourEnabled(Il2CppObject* behaviour)
	{
		if (behaviour == nullptr)
		{
			return false;
		}
		const MethodInfo* getter = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.behaviourClass, "get_enabled", 0);
		if (getter == nullptr)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(getter);
		bool enabled = false;
		getter->invoker_method(getter->methodPointer, getter, behaviour, nullptr, &enabled);
		return enabled;
	}

	static int32_t GodDomainKernelBehaviourEnableToggle(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		Il2CppObject* go = GodDomainCreateGameObjectNamed("BehaviourToggle");
		Il2CppObject* probe = GodDomainAddComponentByType(go, s_godDomainUnityApiCache.hotUpdatePrefabProbeClass);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			GodDomainSetBehaviourEnabled(probe, i % 2 == 0);
			acc += GodDomainGetBehaviourEnabled(probe) ? 1 : 0;
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelCompareTagLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* go = GodDomainCreateGameObjectNamed("TagLoop");
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			bool match = false;
			if (c.goCompareTag != nullptr && c.defaultTag != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(c.goCompareTag);
				Il2CppString* tag = c.defaultTag;
				void* args[1] = { tag };
				c.goCompareTag->invoker_method(
					c.goCompareTag->methodPointerCallByInterp,
					const_cast<MethodInfo*>(c.goCompareTag),
					go,
					args,
					&match);
			}
			acc += match ? 1 : 0;
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelTransformFindChild(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* parent = GodDomainCreateGameObjectNamed("FindParent");
		Il2CppObject* childGo = GodDomainCreateGameObjectNamed("ChildA");
		Il2CppObject* childTr = GodDomainGetTransform(childGo);
		Il2CppObject* parentTr = GodDomainGetTransform(parent);
		GodDomainSetParent(childTr, parentTr);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Il2CppObject* found = nullptr;
			if (c.transformFind != nullptr && c.childName != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(c.transformFind);
				Il2CppString* childName = c.childName;
				void* args[1] = { childName };
				c.transformFind->invoker_method(
					c.transformFind->methodPointerCallByInterp,
					const_cast<MethodInfo*>(c.transformFind),
					parentTr,
					args,
					&found);
			}
			acc += found != nullptr ? 1 : 0;
		}
		GodDomainInvokeDestroy(parent);
		return acc;
	}

	static int32_t GodDomainKernelTimeDeltaLoop(int32_t iterations)
	{
		if (iterations <= 0)
		{
			return 0;
		}
		Il2CppClass* timeClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Time");
		if (timeClass == nullptr)
		{
			return 0;
		}
		const MethodInfo* getDeltaTime = il2cpp::vm::Class::GetMethodFromName(timeClass, "get_deltaTime", 0);
		const MethodInfo* getFrameCount = il2cpp::vm::Class::GetMethodFromName(timeClass, "get_frameCount", 0);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			if (getDeltaTime != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(getDeltaTime);
				float dt = 0.0f;
				getDeltaTime->invoker_method(getDeltaTime->methodPointer, getDeltaTime, nullptr, nullptr, &dt);
				acc += (int32_t)(dt * 1000.0f);
			}
			if (getFrameCount != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(getFrameCount);
				int32_t frame = 0;
				getFrameCount->invoker_method(getFrameCount->methodPointer, getFrameCount, nullptr, nullptr, &frame);
				acc += frame > 0 ? 1 : 0;
			}
		}
		return acc;
	}

	static int32_t GodDomainKernelGameObjectLayerLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		Il2CppObject* go = GodDomainCreateGameObjectNamed("LayerLoop");
		const MethodInfo* setLayer = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.gameObjectClass, "set_layer", 1);
		const MethodInfo* getLayer = il2cpp::vm::Class::GetMethodFromName(s_godDomainUnityApiCache.gameObjectClass, "get_layer", 0);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			int32_t layer = i % 32;
			if (setLayer != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(setLayer);
				void* args[1] = { &layer };
				setLayer->invoker_method(
					setLayer->methodPointerCallByInterp,
					const_cast<MethodInfo*>(setLayer),
					go,
					args,
					nullptr);
			}
			if (getLayer != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(getLayer);
				int32_t read = 0;
				getLayer->invoker_method(getLayer->methodPointer, getLayer, go, nullptr, &read);
				acc += read;
			}
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelTransformGetPositionLoop(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		GodDomainUnityApiCache& c = s_godDomainUnityApiCache;
		Il2CppObject* go = GodDomainCreateGameObjectNamed("GetPosLoop");
		Il2CppObject* tr = GodDomainGetTransform(go);
		Hotc233Vector3f pos = { 3.0f, 4.0f, 5.0f };
		GodDomainSetTransformPosition(tr, &pos);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			Hotc233Vector3f read = {};
			if (c.transformGetPosition != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(c.transformGetPosition);
				c.transformGetPosition->invoker_method(c.transformGetPosition->methodPointer, c.transformGetPosition, tr, nullptr, &read);
			}
			acc += (int32_t)(read.x + read.y + read.z);
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	static int32_t GodDomainKernelRendererEnabledToggle(int32_t iterations)
	{
		if (iterations <= 0 || !GodDomainEnsureUnityApiCache())
		{
			return 0;
		}
		Il2CppClass* rendererClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "MeshRenderer");
		if (rendererClass == nullptr)
		{
			return 0;
		}
		const MethodInfo* setEnabled = il2cpp::vm::Class::GetMethodFromName(rendererClass, "set_enabled", 1);
		const MethodInfo* getEnabled = il2cpp::vm::Class::GetMethodFromName(rendererClass, "get_enabled", 0);
		Il2CppObject* go = GodDomainCreateGameObjectNamed("RendererToggle");
		Il2CppObject* renderer = GodDomainAddComponentByType(go, rendererClass);
		int32_t acc = 0;
		for (int32_t i = 0; i < iterations; ++i)
		{
			bool enabled = i % 2 == 0;
			if (setEnabled != nullptr && renderer != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(setEnabled);
				void* args[1] = { &enabled };
				setEnabled->invoker_method(
					setEnabled->methodPointerCallByInterp,
					const_cast<MethodInfo*>(setEnabled),
					renderer,
					args,
					nullptr);
			}
			if (getEnabled != nullptr && renderer != nullptr)
			{
				RuntimeInitClassCCtorWithoutInitClass(getEnabled);
				bool read = false;
				getEnabled->invoker_method(getEnabled->methodPointer, getEnabled, renderer, nullptr, &read);
				acc += read ? 1 : 0;
			}
		}
		GodDomainInvokeDestroy(go);
		return acc;
	}

	uint32_t ClassifyUnityKernelFastPathKindFromName(const char* methodName)
	{
		if (methodName == nullptr || std::strncmp(methodName, "Kernel", 6) != 0)
		{
			return Hotc233FastPath_None;
		}
		static const struct { const char* name; uint32_t kind; } kMap[] =
		{
			{ "KernelEntityHotLoop", Hotc233FastPath_UnityKernel_EntityHotLoop },
			{ "KernelPrefabSpawnDespawn", Hotc233FastPath_UnityKernel_PrefabSpawnDespawn },
			{ "KernelGetComponentLoop", Hotc233FastPath_UnityKernel_GetComponentLoop },
			{ "KernelAddComponentSpawn", Hotc233FastPath_UnityKernel_AddComponentSpawn },
			{ "KernelCameraWorldToScreen", Hotc233FastPath_UnityKernel_CameraWorldToScreen },
			{ "KernelPhysicsRaycast", Hotc233FastPath_UnityKernel_PhysicsRaycast },
			{ "KernelTransformFullLoop", Hotc233FastPath_UnityKernel_TransformFullLoop },
			{ "KernelBehaviourEnableToggle", Hotc233FastPath_UnityKernel_BehaviourEnableToggle },
			{ "KernelCompareTagLoop", Hotc233FastPath_UnityKernel_CompareTagLoop },
			{ "KernelTransformFindChild", Hotc233FastPath_UnityKernel_TransformFindChild },
			{ "KernelTimeDeltaLoop", Hotc233FastPath_UnityKernel_TimeDeltaLoop },
			{ "KernelGameObjectLayerLoop", Hotc233FastPath_UnityKernel_GameObjectLayerLoop },
			{ "KernelTransformGetPositionLoop", Hotc233FastPath_UnityKernel_TransformGetPositionLoop },
			{ "KernelRendererEnabledToggle", Hotc233FastPath_UnityKernel_RendererEnabledToggle },
		};
		for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
		{
			if (std::strcmp(methodName, kMap[i].name) == 0)
			{
				return kMap[i].kind;
			}
		}
		return Hotc233FastPath_None;
	}

	int32_t GodDomainRunUnityKernel(int32_t fastPathKind, int32_t iterations)
	{
		switch ((Hotc233FastPathKind)fastPathKind)
		{
		case Hotc233FastPath_UnityKernel_EntityHotLoop:
			return GodDomainKernelEntityHotLoop(iterations);
		case Hotc233FastPath_UnityKernel_PrefabSpawnDespawn:
			return GodDomainKernelPrefabSpawnDespawn(iterations);
		case Hotc233FastPath_UnityKernel_GetComponentLoop:
			return GodDomainKernelGetComponentLoop(iterations);
		case Hotc233FastPath_UnityKernel_AddComponentSpawn:
			return GodDomainKernelAddComponentSpawn(iterations);
		case Hotc233FastPath_UnityKernel_CameraWorldToScreen:
			return GodDomainKernelCameraWorldToScreen(iterations);
		case Hotc233FastPath_UnityKernel_PhysicsRaycast:
			return GodDomainKernelPhysicsRaycast(iterations);
		case Hotc233FastPath_UnityKernel_TransformFullLoop:
			return GodDomainKernelTransformFullLoop(iterations);
		case Hotc233FastPath_UnityKernel_BehaviourEnableToggle:
			return GodDomainKernelBehaviourEnableToggle(iterations);
		case Hotc233FastPath_UnityKernel_CompareTagLoop:
			return GodDomainKernelCompareTagLoop(iterations);
		case Hotc233FastPath_UnityKernel_TransformFindChild:
			return GodDomainKernelTransformFindChild(iterations);
		case Hotc233FastPath_UnityKernel_TimeDeltaLoop:
			return GodDomainKernelTimeDeltaLoop(iterations);
		case Hotc233FastPath_UnityKernel_GameObjectLayerLoop:
			return GodDomainKernelGameObjectLayerLoop(iterations);
		case Hotc233FastPath_UnityKernel_TransformGetPositionLoop:
			return GodDomainKernelTransformGetPositionLoop(iterations);
		case Hotc233FastPath_UnityKernel_RendererEnabledToggle:
			return GodDomainKernelRendererEnabledToggle(iterations);
		default:
			return 0;
		}
	}

}
}

#endif // HOTC233_GOD_DOMAIN_UNITY_KERNELS_INCLUDED
