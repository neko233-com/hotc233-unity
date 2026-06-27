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

	IL2CPP_FORCE_INLINE Il2CppMethodPointer ResolveDirectNativeMethodPointer(const MethodInfo* method)
	{
		return StaticF4CallTarget::Resolve(method).directNoArg;
	}

	IL2CPP_FORCE_INLINE Il2CppMethodPointer GetOrCacheDirectNativeMethodPointer(uint64_t* resolveDatas, uint32_t cacheIndex, const MethodInfo* method)
	{
		if (resolveDatas == nullptr || method == nullptr)
		{
			return nullptr;
		}
		Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(method);
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
		typedef void(*SetInterpV3Method)(void*, void*, MethodInfo*);
		((SetInterpV3Method)method->methodPointerCallByInterp)(self, v3, method);
		(void)resolveDatas;
		(void)thunkCacheIdx;
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
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(resolveDatas, thunkCacheIdx, method);
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
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(resolveDatas, thunkCacheIdx, method);
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
		if (stepCount == 10)
		{
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			interpFn(self, retBuf, method);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			interpFn(self, retBuf, method);
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
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(resolveDatas, thunkCacheIdx, method);
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
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(resolveDatas, thunkCacheIdx, method);
		for (int32_t loop = 0; loop < outerLoopCount; loop++)
		{
			InvokeV3ReturnRepeated(directPtr, method, self, retBuf, innerStepCount);
		}
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
		typedef void(*InterpInstanceV_V3_4)(void*, void*, void*, void*, void*, MethodInfo*);
		((InterpInstanceV_V3_4)method->methodPointerCallByInterp)(self, p0, p1, p2, p3, method);
		(void)resolveDatas;
		(void)thunkCacheIdx;
	}

	IL2CPP_FORCE_INLINE void InvokeSetTransformV3Repeated(
		MethodInfo* setMethod,
		Il2CppObject* transformObj,
		void* v3,
		uint16_t stepCount)
	{
		typedef void(*SetInterpV3Method)(void*, void*, MethodInfo*);
		SetInterpV3Method setFn = (SetInterpV3Method)setMethod->methodPointerCallByInterp;
		if (stepCount == 10)
		{
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			setFn(transformObj, v3, setMethod);
			return;
		}
		for (uint16_t step = 0; step < stepCount; step++)
		{
			setFn(transformObj, v3, setMethod);
		}
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
		(void)resolveDatas;
		(void)getThunkCacheIdx;
		(void)setThunkCacheIdx;
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
		typedef void(*SetInterpV3Method)(void*, void*, MethodInfo*);
		((SetInterpV3Method)setMethod->methodPointerCallByInterp)(transformObj, v3, setMethod);
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
