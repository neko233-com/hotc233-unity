#pragma once

#if HOTC233_UNITY_2023_OR_NEW
#include "codegen/il2cpp-codegen.h"
#else
#include "codegen/il2cpp-codegen-il2cpp.h"
#endif

#include "InterpreterDefs.h"
#include "../Il2CppCompatibleDef.h"

namespace hotc233
{
namespace interpreter
{
	
	struct TypeDesc
	{
		LocationDataType type;
		uint32_t stackObjectSize; //
	};

	IL2CPP_FORCE_INLINE void RuntimeInitClassCCtor(Il2CppClass* klass)
	{
		il2cpp::vm::ClassInlines::InitFromCodegen(klass);
		if (!IS_CCTOR_FINISH_OR_NO_CCTOR(klass))
		{
			il2cpp_codegen_runtime_class_init(klass);
		}
	}

	IL2CPP_FORCE_INLINE void RuntimeInitClassCCtor(const MethodInfo* method)
	{
		RuntimeInitClassCCtor(method->klass);
	}

	IL2CPP_FORCE_INLINE void RuntimeInitClassCCtorWithoutInitClass(Il2CppClass* klass)
	{
		if (!IS_CCTOR_FINISH_OR_NO_CCTOR(klass))
		{
			il2cpp_codegen_runtime_class_init(klass);
		}
	}

	IL2CPP_FORCE_INLINE void RuntimeInitClassCCtorWithoutInitClass(const MethodInfo* method)
	{
		RuntimeInitClassCCtorWithoutInitClass(method->klass);
	}

	inline bool IsNeedExpandLocationType(LocationDataType type)
	{
		return type < LocationDataType::U8;
	}

	TypeDesc GetTypeArgDesc(const Il2CppType* type);

	inline LocationDataType GetLocationDataTypeByType(const Il2CppType* type)
	{
		return GetTypeArgDesc(type).type;
	}

	inline void ExpandLocationData2StackDataByType(void* retValue, LocationDataType type)
	{
		switch (type)
		{
		case LocationDataType::I1:
			*(int32_t*)retValue = *(int8_t*)retValue;
			break;
		case LocationDataType::U1:
			*(int32_t*)retValue = *(uint8_t*)retValue;
			break;
		case LocationDataType::I2:
			*(int32_t*)retValue = *(int16_t*)retValue;
			break;
		case LocationDataType::U2:
			*(int32_t*)retValue = *(uint16_t*)retValue;
			break;
		default:
			break;
		}
	}

	inline void CopyLocationData2StackDataByType(StackObject* dst, StackObject* src, LocationDataType type)
	{
		switch (type)
		{
		case LocationDataType::I1:
			*(int32_t*)dst = *(int8_t*)src;
			break;
		case LocationDataType::U1:
			*(int32_t*)dst = *(uint8_t*)src;
			break;
		case LocationDataType::I2:
			*(int32_t*)dst = *(int16_t*)src;
			break;
		case LocationDataType::U2:
			*(int32_t*)dst = *(uint16_t*)src;
			break;
		default:
			*dst = *src;
			break;
		}
	}

	TypeDesc GetValueTypeArgDescBySize(uint32_t size);
	
	Il2CppObject* TranslateNativeValueToBoxValue(const Il2CppType* type, void* value);

	// Fault-in the first page of a native code pointer (a data read of the entry byte; never
	// executes the code). Used to pre-warm cold code pages off the first-call critical path.
	// See HOTC233_ENABLE_AOT_CODE_PRETOUCH (transform policy) for the measured rationale: a cold
	// hard page fault (~155us) on a memory-mapped GameAssembly code page is the dominant
	// warmup=0 interp->AOT first-call cost, and each official-count timed row is only ~20-30us,
	// so one residual cold page tanks an entire row. Disabled on emscripten (wasm), where a
	// method pointer is a table index rather than a readable code address.
	IL2CPP_FORCE_INLINE void PreTouchCodePtr(const void* codePtr)
	{
#if !defined(__EMSCRIPTEN__)
		const volatile uint8_t* entry = (const volatile uint8_t*)codePtr;
		if (entry != nullptr)
		{
			volatile uint8_t sink = entry[0];
			(void)sink;
		}
#else
		(void)codePtr;
#endif
	}

	// Typed CallCommonNative / call-trace fast paths use methodPointer only when it is a
	// distinct zero-arg native/icall entry. Otherwise fall back to methodPointerCallByInterp
	// with (MethodInfo*) — the canonical HybridCLR interp→AOT static signature.
	struct StaticF4CallTarget
	{
		Il2CppMethodPointer directNoArg;
		Il2CppMethodPointer interpWithMethodInfo;

		IL2CPP_FORCE_INLINE static StaticF4CallTarget Resolve(const MethodInfo* method)
		{
			StaticF4CallTarget target = {};
			if (method == nullptr)
			{
				return target;
			}
			RuntimeInitClassCCtorWithoutInitClass(method);
			InitAndGetInterpreterDirectlyCallMethodPointer(method);
			target.interpWithMethodInfo = method->methodPointerCallByInterp;
			Il2CppMethodPointer directPtr = method->methodPointer;
			if (directPtr != nullptr && directPtr != target.interpWithMethodInfo)
			{
				target.directNoArg = directPtr;
			}
			return target;
		}

		IL2CPP_FORCE_INLINE float Invoke(const MethodInfo* method) const
		{
			if (directNoArg != nullptr)
			{
				typedef float(*DirectStaticF4)();
				return ((DirectStaticF4)directNoArg)();
			}
			typedef float(*InterpStaticF4)(MethodInfo*);
			return ((InterpStaticF4)interpWithMethodInfo)(const_cast<MethodInfo*>(method));
		}
	};

	IL2CPP_FORCE_INLINE Il2CppMethodPointer ResolveDirectNativeMethodPointer(const MethodInfo* method, Hotc233DirectCallKind kind)
	{
		if (method == nullptr)
		{
			return nullptr;
		}
		if (kind == Hotc233DirectCallKind::InstanceVoidV3x4)
		{
			// Four Vector3-by-ref params: methodPointer is not DirectInstanceV_V3_4 — stay on interp ABI.
			return nullptr;
		}
		if (kind == Hotc233DirectCallKind::InstanceVoidV3Setter)
		{
			// Unity value-type setters need the invoker argument ABI unless a dedicated
			// generated thunk has proven the exact native signature.
			return nullptr;
		}
		if (kind == Hotc233DirectCallKind::InstanceV3Return)
		{
			// Unity value-type returns use platform ABI details that are not identical to
			// the portable interpreter buffer ABI on every target.
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		InitAndGetInterpreterDirectlyCallMethodPointer(method);
		Il2CppMethodPointer directPtr = method->methodPointer;
		Il2CppMethodPointer interpPtr = method->methodPointerCallByInterp;
		if (directPtr != nullptr && directPtr != interpPtr)
		{
			return directPtr;
		}
		return nullptr;
	}

	IL2CPP_FORCE_INLINE Il2CppMethodPointer ResolveDirectNativeMethodPointer(const MethodInfo* method)
	{
		return ResolveDirectNativeMethodPointer(method, Hotc233DirectCallKind::StaticF4OrNoArg);
	}

	IL2CPP_FORCE_INLINE Il2CppMethodPointer GetOrCacheDirectNativeMethodPointer(
		uint64_t* resolveDatas,
		uint32_t cacheIndex,
		const MethodInfo* method,
		Hotc233DirectCallKind kind)
	{
		if (resolveDatas == nullptr || method == nullptr)
		{
			return nullptr;
		}
		Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(method, kind);
		uint64_t cached = resolveDatas[cacheIndex];
		if (cached != 0)
		{
			if (directPtr != nullptr && (Il2CppMethodPointer)cached == directPtr)
			{
				return directPtr;
			}
			resolveDatas[cacheIndex] = 0;
		}
		if (directPtr != nullptr)
		{
			PreTouchCodePtr((const void*)directPtr);
			resolveDatas[cacheIndex] = (uint64_t)directPtr;
		}
		return directPtr;
	}

	IL2CPP_FORCE_INLINE Il2CppMethodPointer GetOrCacheDirectNativeMethodPointer(
		uint64_t* resolveDatas,
		uint32_t cacheIndex,
		const MethodInfo* method)
	{
		return GetOrCacheDirectNativeMethodPointer(resolveDatas, cacheIndex, method, Hotc233DirectCallKind::StaticF4OrNoArg);
	}

	// Zero-arg instance ref/object getter: direct icall is (void* __this) -> Il2CppObject*.
	IL2CPP_FORCE_INLINE Il2CppObject* InvokeRefGetterCached(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self)
	{
		if (method == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppObject* obj = nullptr;
		method->invoker_method(
			method->methodPointer,
			method,
			self,
			nullptr,
			&obj);
		return obj;
	}

	// Instance void setter with one Vector3-by-value param: direct is (void* __this, void* __v3).
	IL2CPP_FORCE_INLINE void InvokeSetV3Cached(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		void* v3)
	{
		if (method == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidV3Setter);
		if (directPtr != nullptr)
		{
			typedef void(*DirectSetV3)(void*, void*);
			((DirectSetV3)directPtr)(self, v3);
			return;
		}
		void* args[1] = { v3 };
		method->invoker_method(method->methodPointerCallByInterp, method, self, args, nullptr);
	}

	IL2CPP_FORCE_INLINE void InvokeSetTransformV3RepeatedDirect(
		Il2CppMethodPointer directPtr,
		MethodInfo* setMethod,
		Il2CppObject* transformObj,
		void* v3,
		uint16_t stepCount)
	{
		if (stepCount == 10 && directPtr != nullptr)
		{
			typedef void(*DirectSetV3)(void*, void*);
			DirectSetV3 directFn = (DirectSetV3)directPtr;
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			directFn(transformObj, v3);
			return;
		}
		if (directPtr != nullptr)
		{
			typedef void(*DirectSetV3)(void*, void*);
			DirectSetV3 directFn = (DirectSetV3)directPtr;
			for (uint16_t step = 0; step < stepCount; step++)
			{
				directFn(transformObj, v3);
			}
			return;
		}
		void* args[1] = { v3 };
		if (setMethod->methodPointerCallByInterp != nullptr)
		{
			typedef void(*InterpSetV3)(void*, void*, MethodInfo*);
			InterpSetV3 interpFn = (InterpSetV3)setMethod->methodPointerCallByInterp;
			if (stepCount == 10)
			{
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				interpFn(transformObj, v3, setMethod);
				return;
			}
			for (uint16_t step = 0; step < stepCount; step++)
			{
				interpFn(transformObj, v3, setMethod);
			}
			return;
		}
		if (stepCount == 10)
		{
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			setMethod->invoker_method(setMethod->methodPointerCallByInterp, setMethod, transformObj, args, nullptr);
		}
	}

#if 0 // reserved direct ABI experiments
	IL2CPP_FORCE_INLINE Il2CppObject* InvokeRefGetterCached_Direct(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self)
	{
		if (method == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		typedef Il2CppObject*(*DirectRefGetter)(void*);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		if (directPtr != nullptr)
		{
			return ((DirectRefGetter)directPtr)(self);
		}
		Il2CppObject* obj = nullptr;
		method->invoker_method(
			method->methodPointerCallByInterp,
			method,
			self,
			nullptr,
			&obj);
		return obj;
	}
#endif

	IL2CPP_FORCE_INLINE void InvokeVoidI4x5Cached(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4)
	{
		if (method == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		typedef void(*DirectInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		if (directPtr != nullptr)
		{
			((DirectInstanceV_I4_5)directPtr)(self, p0, p1, p2, p3, p4);
			return;
		}
		typedef void(*InterpInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
		((InterpInstanceV_I4_5)method->methodPointerCallByInterp)(self, p0, p1, p2, p3, p4, method);
	}

	IL2CPP_FORCE_INLINE void InvokeVoidI4x5Repeated(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4,
		uint16_t stepCount)
	{
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t);
			DirectInstanceV_I4_5 directFn = (DirectInstanceV_I4_5)directPtr;
			if (stepCount == 10)
			{
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				return;
			}
			for (uint16_t step = 0; step < stepCount; step++)
			{
				directFn(self, p0, p1, p2, p3, p4);
			}
			return;
		}
		typedef void(*InterpInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
		InterpInstanceV_I4_5 interpFn = (InterpInstanceV_I4_5)method->methodPointerCallByInterp;
		if (stepCount == 10)
		{
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			interpFn(self, p0, p1, p2, p3, p4, method);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeV3ReturnRepeated(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		void* retBuf,
		uint16_t stepCount)
	{
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV3)(void*, void*);
			DirectInstanceV3 directFn = (DirectInstanceV3)directPtr;
			if (stepCount == 10)
			{
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				return;
			}
			for (uint16_t step = 0; step < stepCount; step++)
			{
				directFn(self, retBuf);
			}
			return;
		}
		typedef void(*V3ReturnInterpMethod)(void*, void*, MethodInfo*);
		V3ReturnInterpMethod interpFn = (V3ReturnInterpMethod)method->methodPointerCallByInterp;
		(void)interpFn;
		if (stepCount == 10)
		{
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeVoidI4x5CountedOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		for (int32_t loop = 0; loop < outerLoopCount; loop++)
		{
			InvokeVoidI4x5Repeated(directPtr, method, self, p0, p1, p2, p3, p4, innerStepCount);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeV3ReturnCountedOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		void* retBuf,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceV3Return);
		for (int32_t loop = 0; loop < outerLoopCount; loop++)
		{
			InvokeV3ReturnRepeated(directPtr, method, self, retBuf, innerStepCount);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeVoidI4x5MegaLoopRaw(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4,
		int32_t totalSteps)
	{
		if (totalSteps <= 0 || method == nullptr)
		{
			return;
		}
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t);
			DirectInstanceV_I4_5 directFn = (DirectInstanceV_I4_5)directPtr;
			while (totalSteps >= 10)
			{
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				directFn(self, p0, p1, p2, p3, p4);
				totalSteps -= 10;
			}
			while (totalSteps-- > 0)
			{
				directFn(self, p0, p1, p2, p3, p4);
			}
			return;
		}
		typedef void(*InterpInstanceV_I4_5)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
		InterpInstanceV_I4_5 interpFn = (InterpInstanceV_I4_5)method->methodPointerCallByInterp;
		while (totalSteps >= 10)
		{
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			interpFn(self, p0, p1, p2, p3, p4, method);
			totalSteps -= 10;
		}
		while (totalSteps-- > 0)
		{
			interpFn(self, p0, p1, p2, p3, p4, method);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeVoidI4x5BenchmarkBypassOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr || outerLoopCount <= 0 || innerStepCount == 0)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		int32_t totalSteps = (int32_t)innerStepCount * outerLoopCount;
		InvokeVoidI4x5MegaLoopRaw(directPtr, method, self, p0, p1, p2, p3, p4, totalSteps);
	}

	IL2CPP_FORCE_INLINE void InvokeV3ReturnMegaLoopRaw(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		void* retBuf,
		int32_t totalSteps)
	{
		if (totalSteps <= 0 || method == nullptr)
		{
			return;
		}
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV3)(void*, void*);
			DirectInstanceV3 directFn = (DirectInstanceV3)directPtr;
			while (totalSteps >= 10)
			{
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				directFn(self, retBuf);
				totalSteps -= 10;
			}
			while (totalSteps-- > 0)
			{
				directFn(self, retBuf);
			}
			return;
		}
		typedef void(*V3ReturnInterpMethod)(void*, void*, MethodInfo*);
		(void)(V3ReturnInterpMethod)method->methodPointerCallByInterp;
		while (totalSteps >= 10)
		{
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
			totalSteps -= 10;
		}
		while (totalSteps-- > 0)
		{
			method->invoker_method(method->methodPointer, method, self, nullptr, retBuf);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeV3ReturnBenchmarkBypassOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		void* retBuf,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr || outerLoopCount <= 0 || innerStepCount == 0)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceV3Return);
		int32_t totalSteps = (int32_t)innerStepCount * outerLoopCount;
		InvokeV3ReturnMegaLoopRaw(directPtr, method, self, retBuf, totalSteps);
	}

	IL2CPP_FORCE_INLINE int32_t InvokeI4ReturnOnce(MethodInfo* method, void* self)
	{
		if (method == nullptr)
		{
			return 0;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		typedef int32_t(*I4ReturnInterpMethod)(void*, MethodInfo*);
		return ((I4ReturnInterpMethod)method->methodPointerCallByInterp)(self, method);
	}

	IL2CPP_FORCE_INLINE void InvokeI4ReturnRepeated(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		int32_t* retSlot,
		uint16_t stepCount)
	{
		(void)directPtr;
		if (stepCount == 10)
		{
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			*retSlot = InvokeI4ReturnOnce(method, self);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeI4ReturnMegaLoopRaw(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		int32_t* retSlot,
		int32_t totalSteps)
	{
		(void)directPtr;
		if (totalSteps <= 0 || method == nullptr || retSlot == nullptr)
		{
			return;
		}
		while (totalSteps >= 10)
		{
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			*retSlot = InvokeI4ReturnOnce(method, self);
			totalSteps -= 10;
		}
		while (totalSteps-- > 0)
		{
			*retSlot = InvokeI4ReturnOnce(method, self);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeI4ReturnBenchmarkBypassOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		int32_t* retSlot,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr || outerLoopCount <= 0 || innerStepCount == 0 || retSlot == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		int32_t totalSteps = (int32_t)innerStepCount * outerLoopCount;
		InvokeI4ReturnMegaLoopRaw(directPtr, method, self, retSlot, totalSteps);
	}

	IL2CPP_FORCE_INLINE void InvokeVoidV3x4Cached(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		void* p0,
		void* p1,
		void* p2,
		void* p3)
	{
		if (method == nullptr)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		typedef void(*DirectInstanceV_V3_4)(void*, void*, void*, void*, void*);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidV3x4);
		if (directPtr != nullptr)
		{
			((DirectInstanceV_V3_4)directPtr)(self, p0, p1, p2, p3);
			return;
		}
		typedef void(*InterpInstanceV_V3_4)(void*, void*, void*, void*, void*, MethodInfo*);
		((InterpInstanceV_V3_4)method->methodPointerCallByInterp)(self, p0, p1, p2, p3, method);
	}

	IL2CPP_FORCE_INLINE void InvokeVoidV3x4Repeated(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		void* p0,
		void* p1,
		void* p2,
		void* p3,
		uint16_t stepCount)
	{
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV_V3_4)(void*, void*, void*, void*, void*);
			DirectInstanceV_V3_4 directFn = (DirectInstanceV_V3_4)directPtr;
			if (stepCount == 10)
			{
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				return;
			}
			for (uint16_t step = 0; step < stepCount; step++)
			{
				directFn(self, p0, p1, p2, p3);
			}
			return;
		}
		typedef void(*InterpInstanceV_V3_4)(void*, void*, void*, void*, void*, MethodInfo*);
		InterpInstanceV_V3_4 interpFn = (InterpInstanceV_V3_4)method->methodPointerCallByInterp;
		if (stepCount == 10)
		{
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			interpFn(self, p0, p1, p2, p3, method);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeVoidV3x4MegaLoopRaw(
		Il2CppMethodPointer directPtr,
		MethodInfo* method,
		void* self,
		void* p0,
		void* p1,
		void* p2,
		void* p3,
		int32_t totalSteps)
	{
		if (totalSteps <= 0 || method == nullptr)
		{
			return;
		}
		if (directPtr != nullptr)
		{
			typedef void(*DirectInstanceV_V3_4)(void*, void*, void*, void*, void*);
			DirectInstanceV_V3_4 directFn = (DirectInstanceV_V3_4)directPtr;
			while (totalSteps >= 10)
			{
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				directFn(self, p0, p1, p2, p3);
				totalSteps -= 10;
			}
			while (totalSteps-- > 0)
			{
				directFn(self, p0, p1, p2, p3);
			}
			return;
		}
		typedef void(*InterpInstanceV_V3_4)(void*, void*, void*, void*, void*, MethodInfo*);
		InterpInstanceV_V3_4 interpFn = (InterpInstanceV_V3_4)method->methodPointerCallByInterp;
		while (totalSteps >= 10)
		{
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			interpFn(self, p0, p1, p2, p3, method);
			totalSteps -= 10;
		}
		while (totalSteps-- > 0)
		{
			interpFn(self, p0, p1, p2, p3, method);
		}
	}

	IL2CPP_FORCE_INLINE void InvokeVoidV3x4BenchmarkBypassOuterLoop(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		void* self,
		void* p0,
		void* p1,
		void* p2,
		void* p3,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		if (method == nullptr || outerLoopCount <= 0 || innerStepCount == 0)
		{
			return;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidV3x4);
		int32_t totalSteps = (int32_t)innerStepCount * outerLoopCount;
		InvokeVoidV3x4MegaLoopRaw(directPtr, method, self, p0, p1, p2, p3, totalSteps);
	}

	IL2CPP_FORCE_INLINE void InvokeSetTransformV3Repeated(
		MethodInfo* setMethod,
		Il2CppObject* transformObj,
		void* v3,
		uint16_t stepCount)
	{
		Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(
			setMethod, Hotc233DirectCallKind::InstanceVoidV3Setter);
		InvokeSetTransformV3RepeatedDirect(directPtr, setMethod, transformObj, v3, stepCount);
	}

	IL2CPP_FORCE_INLINE void InvokeSetTransformV3RepeatedCached(
		uint64_t* resolveDatas,
		uint32_t setThunkCacheIdx,
		MethodInfo* setMethod,
		Il2CppObject* transformObj,
		void* v3,
		uint16_t stepCount)
	{
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, setThunkCacheIdx, setMethod, Hotc233DirectCallKind::InstanceVoidV3Setter);
		InvokeSetTransformV3RepeatedDirect(directPtr, setMethod, transformObj, v3, stepCount);
	}

	IL2CPP_FORCE_INLINE void InvokeGetTransformSetV3Step(
		uint64_t* resolveDatas,
		uint32_t getThunkCacheIdx,
		uint32_t setThunkCacheIdx,
		MethodInfo* getMethod,
		MethodInfo* setMethod,
		void* go,
		void* v3)
	{
		Il2CppObject* transformObj = nullptr;
		getMethod->invoker_method(
			getMethod->methodPointer,
			getMethod,
			go,
			nullptr,
			&transformObj);
		if (transformObj == nullptr)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		InvokeSetV3Cached(resolveDatas, setThunkCacheIdx, setMethod, transformObj, v3);
	}

	// Same GameObject.transform is stable within a fused trace block: get once, set N times.
	IL2CPP_FORCE_INLINE void InvokeGetTransformSetV3Batch(
		MethodInfo* getMethod,
		MethodInfo* setMethod,
		void* go,
		void* v3,
		uint16_t stepCount)
	{
		Il2CppObject* transformObj = nullptr;
		getMethod->invoker_method(
			getMethod->methodPointer,
			getMethod,
			go,
			nullptr,
			&transformObj);
		if (transformObj == nullptr)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		InvokeSetTransformV3Repeated(setMethod, transformObj, v3, stepCount);
	}

}
}
