#include "InterpreterModule.h"

#include "Interpreter.h"

#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cstring>

#include "Baselib.h"
#include "vm/GlobalMetadata.h"
#include "vm/MetadataCache.h"
#include "vm/MetadataLock.h"
#include "vm/Class.h"
#include "vm/Array.h"
#include "vm/Object.h"
#include "vm/Method.h"
#include "vm/Field.h"
#include "gc/GarbageCollector.h"
#include "metadata/GenericMetadata.h"

#include "../metadata/MetadataModule.h"
#include "../metadata/MetadataUtil.h"
#include "../metadata/InterpreterImage.h"
#include "../transform/Transform.h"
#include "../transform/Hotc233TransformPolicy.h"

#include "MethodBridge.h"
#include "InterpreterUtil.h"

namespace hotc233
{
namespace interpreter
{
	il2cpp::os::ThreadLocalValue InterpreterModule::s_machineState;

	static Il2CppHashMap<const char*, Managed2NativeCallMethod, CStringHash, CStringEqualTo> s_managed2natives;
	static Il2CppHashMap<const char*, Il2CppMethodPointer, CStringHash, CStringEqualTo> s_native2manageds;
	static Il2CppHashMap<const char*, Il2CppMethodPointer, CStringHash, CStringEqualTo> s_adjustThunks;
	static Il2CppHashMap<const char*, const char*, CStringHash, CStringEqualTo> s_fullName2signature;

	static Il2CppHashMap<const MethodInfo*, const ReversePInvokeInfo*, il2cpp::utils::PointerHash<MethodInfo>> s_methodInfo2ReverseInfos;
	static Il2CppHashMap<Il2CppMethodPointer, const ReversePInvokeInfo*, il2cpp::utils::PassThroughHash<Il2CppMethodPointer>> s_methodPointer2ReverseInfos;
	static Il2CppHashMap<const char*, int32_t, CStringHash, CStringEqualTo> s_methodSig2Indexs;
	static std::vector<ReversePInvokeInfo> s_reverseInfos;

	static Il2CppHashMap<const char*, Managed2NativeFunctionPointerCallMethod, CStringHash, CStringEqualTo> s_managed2nativeFunctionPointers;

	static std::unordered_map<void*, bool> s_functionPointerMap;

	static baselib::ReentrantLock s_reversePInvokeMethodLock;

	struct FullySharedGenericMethodInfoForHotc : MethodInfo
	{
		Il2CppMethodPointer rawVirtualMethodPointer;
		Il2CppMethodPointer rawDirectMethodPointer;
		InvokerMethod rawInvokerMethod;
	};

	const MethodInfo* InterpreterModule::GetMethodInfoByReversePInvokeWrapperIndex(int32_t index)
	{
		return s_reverseInfos[index].methodInfo;
	}

	const MethodInfo* InterpreterModule::GetMethodInfoByReversePInvokeWrapperMethodPointer(Il2CppMethodPointer methodPointer)
	{
		auto it = s_methodPointer2ReverseInfos.find(methodPointer);
		return it != s_methodPointer2ReverseInfos.end() ? it->second->methodInfo : nullptr;
	}

	int32_t InterpreterModule::GetWrapperIndexByReversePInvokeWrapperMethodPointer(Il2CppMethodPointer methodPointer)
	{
		auto it = s_methodPointer2ReverseInfos.find(methodPointer);
		return it != s_methodPointer2ReverseInfos.end() ? it->second->index : -1;
	}

	static void InitReversePInvokeInfo()
	{
		for (int32_t i = 0; ; i++)
		{
			const ReversePInvokeMethodData& data = g_reversePInvokeMethodStub[i];
			if (data.methodPointer == nullptr)
			{
				break;
			}
			s_reverseInfos.push_back({ i, data.methodPointer, nullptr });
			auto it = s_methodSig2Indexs.find(data.methodSig);
			if (it == s_methodSig2Indexs.end())
			{
				s_methodSig2Indexs.insert({ data.methodSig, i });
			}
		}
		s_methodInfo2ReverseInfos.resize(s_reverseInfos.size() * 2);
		s_methodPointer2ReverseInfos.resize(s_reverseInfos.size() * 2);
		for (ReversePInvokeInfo& rpi : s_reverseInfos)
		{
			s_methodPointer2ReverseInfos.insert({ rpi.methodPointer, &rpi });
		}
	}

    constexpr int32_t kMaxSignatureNameLength = 1024 * 4;

	Il2CppMethodPointer InterpreterModule::GetReversePInvokeWrapper(const Il2CppImage* image, const MethodInfo* method, Il2CppCallConvention callConvention)
	{
		if (!hotc233::metadata::IsStaticMethod(method))
		{
			return nullptr;
		}

		{
			il2cpp::os::FastAutoLock lock(&s_reversePInvokeMethodLock);
			auto it = s_methodInfo2ReverseInfos.find(method);
			if (it != s_methodInfo2ReverseInfos.end())
			{
				return it->second->methodPointer;
			}
		}

		char sigName[kMaxSignatureNameLength];
		sigName[0] = 'A' + callConvention;
		ComputeSignature(method, false, sigName + 1, sizeof(sigName) - 2);

		il2cpp::os::FastAutoLock lock(&s_reversePInvokeMethodLock);


		auto it = s_methodInfo2ReverseInfos.find(method);
		if (it != s_methodInfo2ReverseInfos.end())
		{
			return it->second->methodPointer;
		}

		auto it2 = s_methodSig2Indexs.find(sigName);
		if (it2 == s_methodSig2Indexs.end())
		{
			TEMP_FORMAT(methodSigBuf, "GetReversePInvokeWrapper fail. not find wrapper of method:%s", GetMethodNameWithSignature(method).c_str());
			RaiseExecutionEngineException(methodSigBuf);
		}
		int32_t wrapperIndex = it2->second;
		const ReversePInvokeMethodData& data = g_reversePInvokeMethodStub[wrapperIndex];
		if (data.methodPointer == nullptr || std::strcmp(data.methodSig, sigName))
		{
			TEMP_FORMAT(methodSigBuf, "GetReversePInvokeWrapper fail. exceed max wrapper num of method:%s", GetMethodNameWithSignature(method).c_str());
			RaiseExecutionEngineException(methodSigBuf);
		}

		s_methodSig2Indexs[sigName] = wrapperIndex + 1;

		ReversePInvokeInfo& rpi = s_reverseInfos[wrapperIndex];
		rpi.methodInfo = method;
		s_methodInfo2ReverseInfos.insert({ method, &rpi });
		return rpi.methodPointer;
	}

	static void InitMethodBridge()
	{
		for (size_t i = 0; ; i++)
		{
			const Managed2NativeMethodInfo& method = g_managed2nativeStub[i];
			if (!method.signature)
			{
				break;
			}
			s_managed2natives.insert({ method.signature, method.method });
		}
		for (size_t i = 0; ; i++)
		{
			const Native2ManagedMethodInfo& method = g_native2managedStub[i];
			if (!method.signature)
			{
				break;
			}
			s_native2manageds.insert({ method.signature, method.method });
		}

		for (size_t i = 0; ; i++)
		{
			const NativeAdjustThunkMethodInfo& method = g_adjustThunkStub[i];
			if (!method.signature)
			{
				break;
			}
			s_adjustThunks.insert({ method.signature, method.method });
		}
		for (size_t i = 0; ; i++)
		{
			const FullName2Signature& nameSig = g_fullName2SignatureStub[i];
			if (!nameSig.fullName)
			{
				break;
			}
			s_fullName2signature.insert({ nameSig.fullName, nameSig.signature });
		}
		for (size_t i = 0; ; i++)
		{
			const Managed2NativeFunctionPointerCallData& method = g_managed2NativeFunctionPointerCallStub[i];
			if (!method.methodSig)
			{
				break;
			}
			s_managed2nativeFunctionPointers.insert({ method.methodSig, method.methodPointer });
		}
	}

	void InterpreterModule::Initialize()
	{
		InitMethodBridge();
		InitReversePInvokeInfo();
	}

	void InterpreterModule::NotSupportNative2Managed()
	{
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException("NotSupportNative2Managed"));
	}

	void InterpreterModule::NotSupportAdjustorThunk()
	{
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException("NotSupportAdjustorThunk"));
	}

	const char* InterpreterModule::GetValueTypeSignature(const char* fullName)
	{
		auto it = s_fullName2signature.find(fullName);
		return it != s_fullName2signature.end() ? it->second : "$";
	}

	bool InterpreterModule::IsMethodInfoPointer(void* pointer)
	{
		il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
		return il2cpp::vm::MetadataContains(pointer);
	}

	static void* NotSupportInvoke(Il2CppMethodPointer, const MethodInfo* method, void*, void**)
	{
		char sigName[kMaxSignatureNameLength];
		ComputeSignature(method, true, sigName, sizeof(sigName) - 1);
		TEMP_FORMAT(errMsg, "Invoke method missing. sinature:%s %s.%s::%s", sigName, method->klass->namespaze, method->klass->name, method->name);
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException(errMsg));
		return nullptr;
	}

	static void NotSupportManaged2NativeFunctionMethod(Il2CppMethodPointer methodPointer, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException("NotSupportManaged2NativeFunctionMethod"));
	}

	static void CopyMethodParameterStackArgs(const MethodInfo* method, StackObject* dst, StackObject* src)
	{
		InterpMethodInfo* imi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
		const bool isInstanceMethod = metadata::IsInstanceMethod(method);
		MethodArgDesc* argDescs = imi->args + isInstanceMethod;
		uint32_t dstIndex = isInstanceMethod ? 1 : 0;
		uint32_t srcIndex = 0;
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const uint16_t stackObjectSize = argDescs[i].stackObjectSize;
#if SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS
			CopyStackObject(dst + dstIndex, src + srcIndex, stackObjectSize);
#else
			std::memcpy(dst + dstIndex, src + srcIndex, stackObjectSize * sizeof(StackObject));
#endif
			dstIndex += stackObjectSize;
			srcIndex += stackObjectSize;
		}
	}

	static void CopyClosedStaticDelegateTargetArg(const MethodInfo* method, StackObject* dst, Il2CppObject* target)
	{
		const Il2CppType* firstArgType = GET_METHOD_PARAMETER_TYPE(method->parameters[0]);
		if (!firstArgType->byref && metadata::IsValueType(firstArgType))
		{
			Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(firstArgType);
			std::memcpy(dst, il2cpp::vm::Object::Unbox(target), il2cpp::vm::Class::GetValueSize(klass, nullptr));
			return;
		}
		dst->obj = target;
	}

	static void CopyFullySharedDelegateArgs(const MethodInfo* method, StackObject* dst, StackObject* src)
	{
		InterpMethodInfo* imi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
		const bool isInstanceMethod = metadata::IsInstanceMethod(method);
		MethodArgDesc* argDescs = imi->args + isInstanceMethod;
		uint32_t dstIndex = isInstanceMethod ? 1 : 0;
		uint32_t srcIndex = 0;
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const Il2CppType* argType = GET_METHOD_PARAMETER_TYPE(method->parameters[i]);
			const uint16_t stackObjectSize = argDescs[i].stackObjectSize;
			if (!argType->byref && metadata::IsValueType(argType))
			{
				Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(argType);
				uint32_t byteSize = il2cpp::vm::Class::GetValueSize(klass, nullptr);
				uintptr_t srcAddress = (uintptr_t)src[srcIndex].ptr;
				void* srcData = srcAddress > 0x10000 ? src[srcIndex].ptr : (void*)(src + srcIndex);
				std::memset(dst + dstIndex, 0, stackObjectSize * sizeof(StackObject));
				std::memcpy(dst + dstIndex, srcData, byteSize);
			}
			else
			{
				dst[dstIndex].ptr = src[srcIndex].ptr;
			}
			dstIndex += stackObjectSize;
			srcIndex += stackObjectSize;
		}
	}

	static void* GetHiddenFullSharedDelegateReturnBuffer(const MethodInfo* invokeMethod, StackObject* args, void* ret)
	{
		if (ret || !invokeMethod || metadata::IsReturnVoidMethod(invokeMethod))
		{
			return ret;
		}

		return args[1 + invokeMethod->parameters_count].ptr;
	}

	static void TraceHiddenDelegateReturn(const char* phase, const MethodInfo* invokeMethod, const MethodInfo* targetMethod, StackObject* args, void* ret)
	{
		return;
		static int32_t s_traceCount = 0;
		if (!ret || s_traceCount >= 64)
		{
			return;
		}

		++s_traceCount;
		std::printf("[hotc233][N2MHiddenRetProbe] %s invoke=%s.%s::%s target=%s.%s::%s invokeParams=%u targetParams=%u ret=%p retU64=%llu arg1=%p arg2=%p\n",
			phase,
			invokeMethod && invokeMethod->klass ? invokeMethod->klass->namespaze : "<null>",
			invokeMethod && invokeMethod->klass ? invokeMethod->klass->name : "<null>",
			invokeMethod ? invokeMethod->name : "<null>",
			targetMethod && targetMethod->klass ? targetMethod->klass->namespaze : "<null>",
			targetMethod && targetMethod->klass ? targetMethod->klass->name : "<null>",
			targetMethod ? targetMethod->name : "<null>",
			invokeMethod ? invokeMethod->parameters_count : 0,
			targetMethod ? targetMethod->parameters_count : 0,
			ret,
			(unsigned long long)*(uint64_t*)ret,
			args ? args[1].ptr : nullptr,
			(args && invokeMethod && invokeMethod->parameters_count > 0) ? args[2].ptr : nullptr);
		std::fflush(stdout);
	}

	static const Il2CppType* InflateMethodParameterTypeIfNeeded(const MethodInfo* method, const Il2CppType* type);
	static bool IsValueTypeForInvoke(const Il2CppType* type);
	static bool ShouldBoxDelegateTargetReturn(const MethodInfo* invokeMethod, const MethodInfo* targetMethod);
	static int32_t GetDelegateTargetReturnStorageSize(const MethodInfo* targetMethod);
	static void BoxDelegateTargetReturnToExpectedSlot(const MethodInfo* targetMethod, void* sourceRet, void* expectedRet);

	bool TryExecuteNative2ManagedOpenDelegateInvoker(const MethodInfo* maybeRetAddress, StackObject* args)
	{
		static int32_t s_openDelegateSkipTraceCount = 0;
		static int32_t s_openDelegateExecTraceCount = 0;
		if (InterpreterModule::IsMethodInfoPointer((void*)maybeRetAddress))
		{
			const MethodInfo* maybeMethod = (const MethodInfo*)maybeRetAddress;
			if (maybeMethod->name && maybeMethod->klass)
			{
				if (false && s_openDelegateSkipTraceCount < 16)
				{
					std::printf("[hotc233][N2MOpenDelegateProbe] skip-real-method slot=%p method=%s.%s::%s args0=%p\n",
						(void*)maybeRetAddress,
						maybeMethod->klass->namespaze ? maybeMethod->klass->namespaze : "",
						maybeMethod->klass->name ? maybeMethod->klass->name : "",
						maybeMethod->name,
						args ? args[0].ptr : nullptr);
					std::fflush(stdout);
					s_openDelegateSkipTraceCount++;
				}
				return false;
			}
		}
		Il2CppDelegate* del = (Il2CppDelegate*)args[0].obj;
		if (!del || !del->method)
		{
			if (false && s_openDelegateExecTraceCount < 128)
			{
				std::printf("[hotc233][N2MOpenDelegateProbe] missing-delegate ret=%p del=%p args0=%p\n",
					(void*)maybeRetAddress,
					(void*)del,
					args ? args[0].ptr : nullptr);
				std::fflush(stdout);
				s_openDelegateExecTraceCount++;
			}
			return false;
		}
		const MethodInfo* curMethod = del->method;
		if (false && s_openDelegateExecTraceCount < 128)
		{
			std::printf("[hotc233][N2MOpenDelegateProbe] exec ret=%p del=%p klass=%s.%s target=%p method=%s.%s::%s interp=%d methodPtr=%p invokeImpl=%p invokeThis=%p interpMethod=%p interpInvoke=%p virtual=%d pcount=%d retType=%d\n",
				(void*)maybeRetAddress,
				(void*)del,
				del->object.klass && del->object.klass->namespaze ? del->object.klass->namespaze : "",
				del->object.klass && del->object.klass->name ? del->object.klass->name : "",
				(void*)del->target,
				curMethod->klass && curMethod->klass->namespaze ? curMethod->klass->namespaze : "",
				curMethod->klass && curMethod->klass->name ? curMethod->klass->name : "",
				curMethod->name ? curMethod->name : "<null>",
				curMethod->interpData ? 1 : 0,
				(void*)del->method_ptr,
				(void*)del->invoke_impl,
				(void*)del->invoke_impl_this,
				(void*)del->interp_method,
				(void*)del->interp_invoke_impl,
				del->method_is_virtual ? 1 : 0,
				(int)curMethod->parameters_count,
				curMethod->return_type ? (int)curMethod->return_type->type : -1);
			std::fflush(stdout);
			s_openDelegateExecTraceCount++;
		}
		InterpMethodInfo* imi = curMethod->interpData ? (InterpMethodInfo*)curMethod->interpData : InterpreterModule::GetInterpMethodInfo(curMethod);
		StackObject* curArgs = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
		if (metadata::IsInstanceMethod(curMethod))
		{
			Il2CppObject* target = del->target;
			if (!target)
			{
				il2cpp::vm::Exception::RaiseNullReferenceException();
				return true;
			}
			curArgs[0].ptr = (uint8_t*)target + (IS_CLASS_VALUE_TYPE(curMethod->klass) ? sizeof(Il2CppObject) : 0);
		}
		CopyFullySharedDelegateArgs(curMethod, curArgs, args + 1);
		Interpreter::Execute(curMethod, curArgs, (void*)maybeRetAddress);
		return true;
	}

	bool TryExecuteNative2ManagedClosedDelegateTarget(const MethodInfo* method, StackObject* args, void* ret)
	{
		if (!method || !InterpreterModule::IsMethodInfoPointer((void*)method) || metadata::IsInstanceMethod(method) || !args || method->parameters_count == 0)
		{
			return false;
		}

		const Il2CppType* firstArgType = GET_METHOD_PARAMETER_TYPE(method->parameters[0]);
		if (!firstArgType || firstArgType->byref || metadata::IsValueType(firstArgType))
		{
			return false;
		}

		Il2CppDelegate* del = (Il2CppDelegate*)args[0].obj;
		if (!del || !del->object.klass || !metadata::IsChildTypeOfMulticastDelegate(del->object.klass) || del->method != method)
		{
			return false;
		}
		InterpMethodInfo* imi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
		StackObject* curArgs = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
		CopyMethodParameterStackArgs(method, curArgs, args + 1);
		Interpreter::Execute(method, curArgs, ret);
		return true;
	}

	static bool ExecuteNative2ManagedDelegateInvoke(Il2CppMulticastDelegate* del, const MethodInfo* invokeMethod, StackObject* invokeArgs, void* ret)
	{
		if (!del)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
			return true;
		}

		Il2CppDelegate** firstSubDel;
		int32_t subDelCount;
		if (del->delegates)
		{
			firstSubDel = (Il2CppDelegate**)il2cpp::vm::Array::GetFirstElementAddress(del->delegates);
			subDelCount = il2cpp::vm::Array::GetLength(del->delegates);
		}
		else
		{
			firstSubDel = (Il2CppDelegate**)&del;
			firstSubDel[0] = &del->delegate;
			subDelCount = 1;
		}

		for (int32_t i = 0; i < subDelCount; i++)
		{
			Il2CppDelegate* cur = firstSubDel[i];
			const MethodInfo* curMethod = cur->method;
			Il2CppObject* curTarget = cur->target;
			if (!curMethod)
			{
				RaiseExecutionEngineException("bad delegate method");
				return true;
			}
			const int paramDelta = (int)invokeMethod->parameters_count - (int)curMethod->parameters_count;
			const bool boxReturnToExpectedSlot = ret && ShouldBoxDelegateTargetReturn(invokeMethod, curMethod);
			void* curRet = ret;
			if (boxReturnToExpectedSlot)
			{
				const int32_t retStorageSize = GetDelegateTargetReturnStorageSize(curMethod);
				curRet = alloca((size_t)retStorageSize);
				std::memset(curRet, 0, (size_t)retStorageSize);
			}
			switch (paramDelta)
			{
			case 0:
			{
				if (metadata::IsInstanceMethod(curMethod))
				{
					if (!curTarget)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
						return true;
					}
					InterpMethodInfo* imi = curMethod->interpData ? (InterpMethodInfo*)curMethod->interpData : InterpreterModule::GetInterpMethodInfo(curMethod);
					StackObject* curArgs = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
					curArgs[0].ptr = (uint8_t*)curTarget + (IS_CLASS_VALUE_TYPE(curMethod->klass) ? sizeof(Il2CppObject) : 0);
					CopyMethodParameterStackArgs(curMethod, curArgs, invokeArgs);
					Interpreter::Execute(curMethod, curArgs, curRet);
				}
				else
				{
					Interpreter::Execute(curMethod, invokeArgs, curRet);
				}
				break;
			}
			case -1:
			{
				if (metadata::IsInstanceMethod(curMethod))
				{
					RaiseExecutionEngineException("bad delegate method");
					return true;
				}
				InterpMethodInfo* imi = curMethod->interpData ? (InterpMethodInfo*)curMethod->interpData : InterpreterModule::GetInterpMethodInfo(curMethod);
				StackObject* curArgs = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
				CopyClosedStaticDelegateTargetArg(curMethod, curArgs, curTarget);
				CopyMethodParameterStackArgs(curMethod, curArgs + 1, invokeArgs);
				Interpreter::Execute(curMethod, curArgs, curRet);
				break;
			}
			case 1:
			{
				if (!metadata::IsInstanceMethod(curMethod))
				{
					RaiseExecutionEngineException("bad delegate method");
					return true;
				}
				Il2CppObject* openTarget = invokeArgs[0].obj;
				if (!openTarget)
				{
					il2cpp::vm::Exception::RaiseNullReferenceException();
					return true;
				}
				InterpMethodInfo* imi = curMethod->interpData ? (InterpMethodInfo*)curMethod->interpData : InterpreterModule::GetInterpMethodInfo(curMethod);
				StackObject* curArgs = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
				curArgs[0].ptr = (uint8_t*)openTarget + (IS_CLASS_VALUE_TYPE(curMethod->klass) ? sizeof(Il2CppObject) : 0);
				CopyMethodParameterStackArgs(curMethod, curArgs, invokeArgs + 1);
				Interpreter::Execute(curMethod, curArgs, curRet);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("bad delegate method");
				return true;
			}
			}
			if (boxReturnToExpectedSlot)
			{
				BoxDelegateTargetReturnToExpectedSlot(curMethod, curRet, ret);
			}
		}
		return true;
	}

	bool TryExecuteNative2ManagedInlineDelegateInvoker(const MethodInfo* method, StackObject* args, void* ret)
	{
		return false;
		if (!method || !args)
		{
			return false;
		}

		Il2CppMulticastDelegate* del = (Il2CppMulticastDelegate*)args[0].obj;
		if (!del || !del->delegate.object.klass || !metadata::IsChildTypeOfMulticastDelegate(del->delegate.object.klass))
		{
			return false;
		}
		if (method->klass && metadata::IsChildTypeOfMulticastDelegate(method->klass))
		{
			return false;
		}

		const MethodInfo* invokeMethod = il2cpp::vm::Class::GetMethodFromName(del->delegate.object.klass, "Invoke", -1);
		if (!invokeMethod)
		{
			RaiseExecutionEngineException("delegate Invoke method missing");
			return true;
		}

		if (!del->delegates && del->delegate.method != method)
		{
			return false;
		}

		void* actualRet = GetHiddenFullSharedDelegateReturnBuffer(invokeMethod, args, ret);
		if (!ret && actualRet)
		{
			TraceHiddenDelegateReturn("inline-before", invokeMethod, method, args, actualRet);
		}
		bool handled = ExecuteNative2ManagedDelegateInvoke(del, invokeMethod, args + 1, actualRet);
		if (!ret && actualRet)
		{
			TraceHiddenDelegateReturn("inline-after", invokeMethod, method, args, actualRet);
		}
		return handled;
	}

	bool TryExecuteNative2ManagedDelegateInvoke(const MethodInfo* method, StackObject* args, void* ret)
	{
		if (!method || !args || !InterpreterModule::IsMethodInfoPointer((void*)method) || !method->name || std::strcmp(method->name, "Invoke") != 0 || !method->klass)
		{
			return false;
		}
		if (!metadata::IsChildTypeOfMulticastDelegate(method->klass))
		{
			return false;
		}
		const MethodInfo* invokeMethod = method;

		Il2CppMulticastDelegate* del = (Il2CppMulticastDelegate*)args[0].obj;
		if (!del || !del->delegate.object.klass || !metadata::IsChildTypeOfMulticastDelegate(del->delegate.object.klass))
		{
			return false;
		}
		void* actualRet = GetHiddenFullSharedDelegateReturnBuffer(invokeMethod, args, ret);
		if (!ret && actualRet)
		{
			TraceHiddenDelegateReturn("invoke-before", invokeMethod, method, args, actualRet);
		}
		bool handled = ExecuteNative2ManagedDelegateInvoke(del, invokeMethod, args + 1, actualRet);
		if (!ret && actualRet)
		{
			TraceHiddenDelegateReturn("invoke-after", invokeMethod, method, args, actualRet);
		}
		return handled;
	}

	template<typename T>
	const Managed2NativeCallMethod GetManaged2NativeMethod(const T* method, bool forceStatic)
	{
		char sigName[kMaxSignatureNameLength];
		if (IsFullGenericVariableValueTypeReturn(method))
		{
			char callSigName[kMaxSignatureNameLength];
			ComputeSignature(method, !forceStatic, callSigName, sizeof(callSigName) - 1);
			std::snprintf(sigName, sizeof(sigName), "h%s", callSigName);
		}
		else
		{
			ComputeSignature(method, !forceStatic, sigName, sizeof(sigName) - 1);
		}
		auto it = s_managed2natives.find(sigName);
		return it != s_managed2natives.end() ? it->second : nullptr;
	}

	static bool IsInterestingNative2ManagedSignature(const char* sigName)
	{
		return sigName
			&& (std::strstr(sigName, "i4us41s41")
				|| std::strstr(sigName, "i4us46s46")
				|| std::strstr(sigName, "hi4us41s41")
				|| std::strstr(sigName, "hi4us46s46"));
	}

	static void TraceNative2ManagedBinding(const Il2CppMethodDefinition* method, const char* sigName)
	{
		return;
		static int32_t s_n2mDefinitionBindingTraceCount = 0;
		if (!method || !sigName || s_n2mDefinitionBindingTraceCount >= 256)
		{
			return;
		}
		Il2CppClass* declaringClass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromTypeDefinitionIndex(method->declaringType);
		const char* methodName = il2cpp::vm::GlobalMetadata::GetStringFromIndex(method->nameIndex);
		bool interestingSignature = IsInterestingNative2ManagedSignature(sigName);
		bool interestingMethod = declaringClass
			&& ((declaringClass->name && std::strstr(declaringClass->name, "LinqAggregateProbe"))
				|| (methodName && std::strstr(methodName, "VerifyAggregate"))
				|| (methodName && std::strstr(methodName, "b__"))
				|| (methodName && std::strcmp(methodName, "Invoke") == 0));
		if (!interestingSignature && !interestingMethod)
		{
			return;
		}
		const Il2CppType* rawReturnType = hotc233::metadata::MetadataModule::GetIl2CppTypeFromEncodeIndex(method->returnType);
		const Il2CppType* p0 = method->parameterCount > 0
			? hotc233::metadata::MetadataModule::GetIl2CppTypeFromEncodeIndex(il2cpp::vm::GlobalMetadata::GetParameterDefinitionFromIndex(method, method->parameterStart)->typeIndex)
			: nullptr;
		const Il2CppType* p1 = method->parameterCount > 1
			? hotc233::metadata::MetadataModule::GetIl2CppTypeFromEncodeIndex(il2cpp::vm::GlobalMetadata::GetParameterDefinitionFromIndex(method, method->parameterStart + 1)->typeIndex)
			: nullptr;
		std::printf("[hotc233][N2MDefinitionBindingProbe] sig=%s methodDef=%p %s.%s::%s retType=%d retByref=%d pcount=%d p0=%d/%d p1=%d/%d flags=0x%x token=0x%x\n",
			sigName,
			(void*)method,
			declaringClass && declaringClass->namespaze ? declaringClass->namespaze : "",
			declaringClass && declaringClass->name ? declaringClass->name : "",
			methodName ? methodName : "<null>",
			rawReturnType ? (int32_t)rawReturnType->type : -1,
			rawReturnType ? (int32_t)rawReturnType->byref : -1,
			(int32_t)method->parameterCount,
			p0 ? (int32_t)p0->type : -1,
			p0 ? (int32_t)p0->byref : -1,
			p1 ? (int32_t)p1->type : -1,
			p1 ? (int32_t)p1->byref : -1,
			(uint32_t)method->flags,
			(uint32_t)method->token);
		std::fflush(stdout);
		s_n2mDefinitionBindingTraceCount++;
	}

	static void TraceNative2ManagedBinding(const MethodInfo* method, const char* sigName)
	{
		return;
		static int32_t s_n2mBindingTraceCount = 0;
		if (!method || !sigName || s_n2mBindingTraceCount >= 64)
		{
			return;
		}
		bool interestingSignature = IsInterestingNative2ManagedSignature(sigName);
		bool interestingMethod = method->klass
			&& ((method->klass->namespaze && std::strstr(method->klass->namespaze, "UnityHotc"))
				|| (method->klass->name && std::strstr(method->klass->name, "LinqAggregateProbe"))
				|| (method->name && std::strstr(method->name, "VerifyAggregate"))
				|| (method->name && std::strstr(method->name, "b__"))
				|| (method->name && std::strcmp(method->name, "Invoke") == 0));
		if (!interestingSignature && !interestingMethod)
		{
			return;
		}
		int32_t p0Type = -1;
		int32_t p1Type = -1;
		int32_t p0Byref = -1;
		int32_t p1Byref = -1;
		if (method->parameters_count > 0)
		{
			const Il2CppType* p0 = GET_METHOD_PARAMETER_TYPE(method->parameters[0]);
			p0Type = p0 ? (int32_t)p0->type : -1;
			p0Byref = p0 ? (int32_t)p0->byref : -1;
		}
		if (method->parameters_count > 1)
		{
			const Il2CppType* p1 = GET_METHOD_PARAMETER_TYPE(method->parameters[1]);
			p1Type = p1 ? (int32_t)p1->type : -1;
			p1Byref = p1 ? (int32_t)p1->byref : -1;
		}
		bool isDelegateInvoke = method->name
			&& std::strcmp(method->name, "Invoke") == 0
			&& method->klass
			&& hotc233::metadata::IsChildTypeOfMulticastDelegate(method->klass);
		std::printf("[hotc233][N2MBindingProbe] sig=%s method=%p %s.%s::%s retType=%d retByref=%d pcount=%d p0=%d/%d p1=%d/%d inflated=%d generic=%d fullShare=%d delegateInvoke=%d genericMethod=%p rgctx=%p invoker=%p methodPtr=%p interpPtr=%p\n",
			sigName,
			(void*)method,
			method->klass && method->klass->namespaze ? method->klass->namespaze : "",
			method->klass && method->klass->name ? method->klass->name : "",
			method->name ? method->name : "<null>",
			method->return_type ? (int32_t)method->return_type->type : -1,
			method->return_type ? (int32_t)method->return_type->byref : -1,
			(int32_t)method->parameters_count,
			p0Type,
			p0Byref,
			p1Type,
			p1Byref,
			method->is_inflated ? 1 : 0,
			method->is_generic ? 1 : 0,
			method->has_full_generic_sharing_signature ? 1 : 0,
			isDelegateInvoke ? 1 : 0,
			method->is_inflated ? (void*)method->genericMethod : nullptr,
			method->is_inflated ? (void*)method->rgctx_data : nullptr,
			(void*)method->invoker_method,
			(void*)method->methodPointer,
			(void*)method->methodPointerCallByInterp);
		std::fflush(stdout);
		s_n2mBindingTraceCount++;
	}

	template<typename T>
	const Il2CppMethodPointer GetNative2ManagedMethod(const T* method, bool forceStatic)
	{
		char sigName[kMaxSignatureNameLength];
		ComputeNative2ManagedSignature(method, !forceStatic, sigName, sizeof(sigName) - 1);
		TraceNative2ManagedBinding(method, sigName);
		auto it = s_native2manageds.find(sigName);
		return it != s_native2manageds.end() ? it->second : InterpreterModule::NotSupportNative2Managed;
	}

	template<typename T>
	const Il2CppMethodPointer GetNativeAdjustMethodMethod(const T* method, bool forceStatic)
	{
		char sigName[kMaxSignatureNameLength];
		ComputeNative2ManagedSignature(method, !forceStatic, sigName, sizeof(sigName) - 1);
		auto it = s_adjustThunks.find(sigName);
		return it != s_adjustThunks.end() ? it->second : InterpreterModule::NotSupportAdjustorThunk;
	}

	static void RaiseMethodNotSupportException(const MethodInfo* method, const char* desc)
	{
		TEMP_FORMAT(errMsg, "%s. %s.%s::%s", desc, method->klass->namespaze, method->klass->name, method->name);
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException(errMsg));
	}

	static void RaiseMethodNotSupportException(const Il2CppMethodDefinition* method, const char* desc)
	{
		Il2CppClass* klass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromTypeDefinitionIndex(method->declaringType);
		TEMP_FORMAT(errMsg, "%s. %s.%s::%s", desc, klass->namespaze, klass->name, il2cpp::vm::GlobalMetadata::GetStringFromIndex(method->nameIndex));
		il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException(errMsg));
	}

	static const Il2CppType* InflateMethodParameterTypeIfNeeded(const MethodInfo* method, const Il2CppType* type)
	{
		if (!method || !type)
		{
			return type;
		}
		if (type->type != IL2CPP_TYPE_VAR && type->type != IL2CPP_TYPE_MVAR)
		{
			return type;
		}
		const Il2CppGenericContext* context = nullptr;
		if (method->is_inflated && method->genericMethod)
		{
			context = &method->genericMethod->context;
		}
		else if (method->klass && method->klass->generic_class)
		{
			context = &method->klass->generic_class->context;
		}
		return context ? il2cpp::metadata::GenericMetadata::InflateIfNeeded(type, context, true) : type;
	}

	static bool IsValueTypeForInvoke(const Il2CppType* type)
	{
		if (!type)
		{
			return false;
		}
		if (hotc233::metadata::IsValueType(type))
		{
			return true;
		}
		if (type->type == IL2CPP_TYPE_GENERICINST)
		{
			Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
			return klass && IS_CLASS_VALUE_TYPE(klass);
		}
		return false;
	}

	static bool IsReferenceReturnForDelegateInvoke(const MethodInfo* method)
	{
		if (!method || !method->return_type || metadata::IsVoidType(method->return_type))
		{
			return false;
		}
		const Il2CppType* returnType = InflateMethodParameterTypeIfNeeded(method, method->return_type);
		return returnType && !returnType->byref && !IsValueTypeForInvoke(returnType);
	}

	static bool ShouldBoxDelegateTargetReturn(const MethodInfo* invokeMethod, const MethodInfo* targetMethod)
	{
		if (!IsReferenceReturnForDelegateInvoke(invokeMethod) || !targetMethod || !targetMethod->return_type || metadata::IsVoidType(targetMethod->return_type))
		{
			return false;
		}
		const Il2CppType* targetReturnType = InflateMethodParameterTypeIfNeeded(targetMethod, targetMethod->return_type);
		return targetReturnType && !targetReturnType->byref && IsValueTypeForInvoke(targetReturnType);
	}

	static int32_t GetDelegateTargetReturnStorageSize(const MethodInfo* targetMethod)
	{
		int32_t retStorageSize = sizeof(StackObject);
		if (!targetMethod || !targetMethod->return_type)
		{
			return retStorageSize;
		}
		const Il2CppType* targetReturnType = InflateMethodParameterTypeIfNeeded(targetMethod, targetMethod->return_type);
		if (targetReturnType && !targetReturnType->byref && IsValueTypeForInvoke(targetReturnType))
		{
			int32_t valueSize = metadata::GetTypeValueSize(targetReturnType);
			if (valueSize > retStorageSize)
			{
				retStorageSize = valueSize;
			}
		}
		return retStorageSize;
	}

	static void BoxDelegateTargetReturnToExpectedSlot(const MethodInfo* targetMethod, void* sourceRet, void* expectedRet)
	{
		if (!targetMethod || !sourceRet || !expectedRet)
		{
			return;
		}
		const Il2CppType* targetReturnType = InflateMethodParameterTypeIfNeeded(targetMethod, targetMethod->return_type);
		if (!targetReturnType || targetReturnType->byref || !IsValueTypeForInvoke(targetReturnType))
		{
			return;
		}
		*(Il2CppObject**)expectedRet = TranslateNativeValueToBoxValue(targetReturnType, sourceRet);
	}

	static bool ShouldUseInvokerForSharedStructM2N(const MethodInfo* method, const char* sigName)
	{
		if (!method)
		{
			return false;
		}
#if HOTC233_UNITY_2021_OR_NEW
		if (method->has_full_generic_sharing_signature)
		{
			return false;
		}
#endif
		const Il2CppType* returnType = InflateMethodParameterTypeIfNeeded(method, method->return_type);
		if (returnType && !returnType->byref && IsValueTypeForInvoke(returnType))
		{
			return true;
		}
		if (std::strchr(sigName, 's'))
		{
			return false;
		}
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const Il2CppType* paramType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
			if (!paramType->byref && (IsValueTypeForInvoke(paramType) || paramType->type == IL2CPP_TYPE_VAR || paramType->type == IL2CPP_TYPE_MVAR))
			{
				return true;
			}
		}
		return false;
	}

	Il2CppMethodPointer InterpreterModule::GetMethodPointer(const Il2CppMethodDefinition* method)
	{
		Il2CppMethodPointer ncm = GetNative2ManagedMethod(method, false);
		return ncm ? ncm : (Il2CppMethodPointer)NotSupportNative2Managed;
	}

	Il2CppMethodPointer InterpreterModule::GetMethodPointer(const MethodInfo* method)
	{
		Il2CppMethodPointer ncm = GetNative2ManagedMethod(method, false);
		return ncm ? ncm : (Il2CppMethodPointer)NotSupportNative2Managed;
	}

	Il2CppMethodPointer InterpreterModule::GetAdjustThunkMethodPointer(const Il2CppMethodDefinition* method)
	{
		return GetNativeAdjustMethodMethod(method, false);
	}

	Il2CppMethodPointer InterpreterModule::GetAdjustThunkMethodPointer(const MethodInfo* method)
	{
		return GetNativeAdjustMethodMethod(method, false);
	}

	static int32_t GetManaged2NativeReturnStorageSize(const MethodInfo* method)
	{
		if (!method || !method->return_type || metadata::IsVoidType(method->return_type))
		{
			return 0;
		}

		int32_t clearSize = sizeof(StackObject);
		if (method->return_type->type != IL2CPP_TYPE_VAR && method->return_type->type != IL2CPP_TYPE_MVAR)
		{
			int32_t valueSize = metadata::GetTypeValueSize(method->return_type);
			if (valueSize > clearSize)
			{
				clearSize = valueSize;
			}
		}
		return clearSize;
	}

	static void ClearManaged2NativeReturnStorage(const MethodInfo* method, void* ret)
	{
		if (!ret)
		{
			return;
		}
		int32_t clearSize = GetManaged2NativeReturnStorageSize(method);
		if (clearSize <= 0)
		{
			return;
		}
		std::memset(ret, 0, (size_t)clearSize);
	}

	static bool IsAddressRangeOverlap(void* left, int32_t leftSize, void* right, int32_t rightSize)
	{
		if (!left || !right || leftSize <= 0 || rightSize <= 0)
		{
			return false;
		}
		uintptr_t leftBegin = (uintptr_t)left;
		uintptr_t rightBegin = (uintptr_t)right;
		return leftBegin < rightBegin + (uintptr_t)rightSize && rightBegin < leftBegin + (uintptr_t)leftSize;
	}

	static void NormalizeFullGenericValueTypeReturn(const MethodInfo* method, void* ret, bool rawInvokerWroteHiddenReturn)
	{
		if (!method || !ret || !method->return_type || !method->has_full_generic_sharing_signature)
		{
			return;
		}
		const Il2CppType* rawReturnType = method->return_type;
		if (method->genericMethod && method->genericMethod->methodDefinition)
		{
			rawReturnType = method->genericMethod->methodDefinition->return_type;
		}
		if (!rawReturnType || (rawReturnType->type != IL2CPP_TYPE_VAR && rawReturnType->type != IL2CPP_TYPE_MVAR))
		{
			return;
		}
		const Il2CppType* inflatedReturnType = InflateMethodParameterTypeIfNeeded(method, method->return_type);
		if (!inflatedReturnType || inflatedReturnType->byref || !IsValueTypeForInvoke(inflatedReturnType))
		{
			return;
		}
		int32_t valueSize = metadata::GetTypeValueSize(inflatedReturnType);
		if (valueSize <= 0)
		{
			return;
		}
		if (rawInvokerWroteHiddenReturn)
		{
			return;
		}
		uintptr_t rawRetWord = *(uintptr_t*)ret;
		if (rawRetWord <= 0x10000 && valueSize <= (int32_t)sizeof(uintptr_t))
		{
			return;
		}
		Il2CppObject* boxed = *(Il2CppObject**)ret;
		if (!boxed)
		{
			std::memset(ret, 0, (size_t)GetManaged2NativeReturnStorageSize(method));
			return;
		}
		Il2CppClass* returnKlass = il2cpp::vm::Class::FromIl2CppType(inflatedReturnType);
		if (returnKlass && valueSize > 0)
		{
			std::memcpy(ret, il2cpp::vm::Object::Unbox(boxed), (size_t)valueSize);
		}
	}

	static bool IsFullGenericVariableValueTypeReturn(const MethodInfo* method)
	{
		if (!method || !method->return_type || !method->has_full_generic_sharing_signature)
		{
			return false;
		}
		const Il2CppType* rawReturnType = method->return_type;
		if (method->genericMethod && method->genericMethod->methodDefinition)
		{
			rawReturnType = method->genericMethod->methodDefinition->return_type;
		}
		if (!rawReturnType || (rawReturnType->type != IL2CPP_TYPE_VAR && rawReturnType->type != IL2CPP_TYPE_MVAR))
		{
			return false;
		}
		const Il2CppType* inflatedReturnType = InflateMethodParameterTypeIfNeeded(method, method->return_type);
		return inflatedReturnType && !inflatedReturnType->byref && IsValueTypeForInvoke(inflatedReturnType);
	}

	static bool TryInvokeFullGenericVariableValueReturn(const MethodInfo* method, void* thisPtr, void** invokeParams, void* ret)
	{
		if (!ret || !IsFullGenericVariableValueTypeReturn(method) || !method->genericMethod || !method->genericMethod->methodDefinition)
		{
			return false;
		}
		const FullySharedGenericMethodInfoForHotc* sharedMethod = reinterpret_cast<const FullySharedGenericMethodInfoForHotc*>(method);
		if (!sharedMethod->rawInvokerMethod || !sharedMethod->rawDirectMethodPointer)
		{
			return false;
		}
		MethodInfo rawInvokerMethodInfo = *method;
		rawInvokerMethodInfo.return_type = method->genericMethod->methodDefinition->return_type;
		rawInvokerMethodInfo.parameters = method->genericMethod->methodDefinition->parameters;
		sharedMethod->rawInvokerMethod(sharedMethod->rawDirectMethodPointer, &rawInvokerMethodInfo, thisPtr, invokeParams, ret);
		return true;
	}

	void InterpreterModule::Managed2NativeCallByReflectionInvoke(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		bool tracePerformanceEntry = false;
		if (hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method))
		{
			StackObject* argBase = (hotc233::metadata::IsInstanceMethod(method) || method->parameters_count > 0)
				? localVarBase + argVarIndexs[0]
				: localVarBase;
			if (tracePerformanceEntry)
			{
				std::printf("[hotc233][M2NReflectionProbe] direct-interpreter-enter %s.%s::%s method=%p argBase=%p local=%p arg0=%u pcount=%d ret=%p\n",
					method->klass->namespaze ? method->klass->namespaze : "",
					method->klass->name ? method->klass->name : "",
					method->name,
					(void*)method,
					(void*)argBase,
					(void*)localVarBase,
					(uint32_t)(argVarIndexs ? argVarIndexs[0] : 0),
					(int)method->parameters_count,
					ret);
				std::fflush(stdout);
			}
			Interpreter::Execute(method, argBase, ret);
			if (tracePerformanceEntry)
			{
				std::printf("[hotc233][M2NReflectionProbe] direct-interpreter-exit %s.%s::%s method=%p ret=%p\n",
					method->klass->namespaze ? method->klass->namespaze : "",
					method->klass->name ? method->klass->name : "",
					method->name,
					(void*)method,
					ret);
				std::fflush(stdout);
			}
			return;
		}
		bool traceDictionarySetItem = false;
		bool traceColorOp = false;
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] enter %s.%s::%s method=%p methodPointer=%p interpPointer=%p invoker=%p pcount=%d ret=%p\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				method->name,
				(void*)method,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				(void*)method->invoker_method,
				(int)method->parameters_count,
				ret);
			std::fflush(stdout);
		}
		if (method->invoker_method == nullptr)
		{
			char sigName[kMaxSignatureNameLength];
			ComputeSignature(method, true, sigName, sizeof(sigName) - 1);

			TEMP_FORMAT(errMsg, "GetManaged2NativeMethodPointer. sinature:%s not support.", sigName);
			RaiseMethodNotSupportException(method, errMsg);
		}
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] before-init method=%p interpPointer=%p invoker=%p\n",
				(void*)method,
				(void*)method->methodPointerCallByInterp,
				(void*)method->invoker_method);
			std::fflush(stdout);
		}
		if (!InitAndGetInterpreterDirectlyCallMethodPointer(method))
		{
			if (traceDictionarySetItem)
			{
				std::printf("[hotc233][M2NReflectionProbe] init-failed method=%p methodPointer=%p interpPointer=%p invoker=%p\n",
					(void*)method,
					(void*)method->methodPointer,
					(void*)method->methodPointerCallByInterp,
					(void*)method->invoker_method);
				std::fflush(stdout);
			}
			RaiseAOTGenericMethodNotInstantiatedException(method);
		}
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] after-init method=%p methodPointer=%p interpPointer=%p invoker=%p\n",
				(void*)method,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				(void*)method->invoker_method);
			std::fflush(stdout);
		}
		void* thisPtr;
		uint16_t* argVarIndexBase;
		if (hotc233::metadata::IsInstanceMethod(method))
		{
			thisPtr = localVarBase[argVarIndexs[0]].obj;
			argVarIndexBase = argVarIndexs + 1;
		}
		else
		{
			thisPtr = nullptr;
			argVarIndexBase = argVarIndexs;
		}
		void* invokeParams[256];
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const Il2CppType* argType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
			StackObject* argValue = localVarBase + argVarIndexBase[i];
			if (!argType->byref && IsValueTypeForInvoke(argType))
			{
				invokeParams[i] = argValue;
			}
			else
			{
				invokeParams[i] = argValue->ptr;
			}
			if (traceDictionarySetItem)
			{
				Il2CppClass* argKlass = argType ? il2cpp::vm::Class::FromIl2CppType(argType) : nullptr;
				std::printf("[hotc233][M2NReflectionProbe] arg%d var=%u type=%d byref=%d value=%d klass=%s.%s stack=%p param=%p ptr=%p obj=%p\n",
					(int)i,
					(uint32_t)argVarIndexBase[i],
					argType ? (int)argType->type : -1,
					argType && argType->byref ? 1 : 0,
					argType && IsValueTypeForInvoke(argType) ? 1 : 0,
					argKlass && argKlass->namespaze ? argKlass->namespaze : "",
					argKlass && argKlass->name ? argKlass->name : "",
					(void*)argValue,
					invokeParams[i],
					argValue->ptr,
					(void*)argValue->obj);
				std::fflush(stdout);
			}
			if (traceColorOp && argValue && i < 2)
			{
				float* c = (float*)(void*)argValue;
				std::printf("[hotc233][ColorOpProbe] before %s arg%d stack=%p invokeParam=%p rgba=(%.6f,%.6f,%.6f,%.6f)\n",
					method->name,
					(int)i,
					(void*)argValue,
					invokeParams[i],
					c[0],
					c[1],
					c[2],
					c[3]);
				std::fflush(stdout);
			}
		}
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] before-invoker this=%p methodPointer=%p interpPointer=%p invoker=%p\n",
				thisPtr,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				(void*)method->invoker_method);
			std::fflush(stdout);
		}
		bool traceGroupingKey = false;
		if (traceGroupingKey)
		{
			std::printf("[hotc233][M2NGroupingKeyProbe] before %s.%s::%s full=%d retType=%d ret=%p retU64=%llu this=%p pcount=%d\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				method->name,
				method->has_full_generic_sharing_signature ? 1 : 0,
				method->return_type ? (int)method->return_type->type : -1,
				ret,
				ret ? (unsigned long long)*(uint64_t*)ret : 0ULL,
				thisPtr,
				(int)method->parameters_count);
			std::fflush(stdout);
		}
		void* actualRet = ret;
		int32_t retStorageSize = GetManaged2NativeReturnStorageSize(method);
		if (ret && retStorageSize > 0)
		{
			for (uint8_t i = 0; i < method->parameters_count; i++)
			{
				const Il2CppType* argType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
				if (!argType || argType->byref || !IsValueTypeForInvoke(argType))
				{
					continue;
				}
				int32_t argSize = metadata::GetTypeValueSize(argType);
				if (IsAddressRangeOverlap(ret, retStorageSize, invokeParams[i], argSize))
				{
					actualRet = alloca((size_t)retStorageSize);
					break;
				}
			}
		}
		ClearManaged2NativeReturnStorage(method, actualRet);
#if HOTC233_UNITY_2021_OR_NEW
		Il2CppMethodPointer invokerTarget = method->methodPointer != nullptr ? method->methodPointer : method->methodPointerCallByInterp;
		if (method->has_full_generic_sharing_signature && method->methodPointerCallByInterp != nullptr)
		{
			invokerTarget = method->methodPointerCallByInterp;
		}
		bool fullGenericVariableValueReturnHandled = TryInvokeFullGenericVariableValueReturn(method, thisPtr, invokeParams, actualRet);
		if (!fullGenericVariableValueReturnHandled)
		{
			method->invoker_method(invokerTarget, method, thisPtr, invokeParams, actualRet);
		}
		NormalizeFullGenericValueTypeReturn(method, actualRet, fullGenericVariableValueReturnHandled);
		if (actualRet != ret && ret && retStorageSize > 0)
		{
			std::memcpy(ret, actualRet, (size_t)retStorageSize);
		}
		if (traceColorOp)
		{
			std::printf("[hotc233][ColorOpProbe] after %s target=%p methodPointer=%p interpPointer=%p ret=%p retI4=%d retU64=%llu\n",
				method->name,
				(void*)invokerTarget,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				ret,
				ret ? *(int32_t*)ret : 0,
				ret ? (unsigned long long)*(uint64_t*)ret : 0ULL);
			std::fflush(stdout);
		}
		if (traceGroupingKey)
		{
			std::printf("[hotc233][M2NGroupingKeyProbe] after %s.%s::%s target=%p methodPointer=%p interpPointer=%p ret=%p retU64=%llu retI4=%d\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				method->name,
				(void*)invokerTarget,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				ret,
				ret ? (unsigned long long)*(uint64_t*)ret : 0ULL,
				ret ? *(int32_t*)ret : 0);
			std::fflush(stdout);
		}
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] after-invoker this=%p method=%p ret=%p\n",
				thisPtr,
				(void*)method,
				ret);
			std::fflush(stdout);
		}
#else
		void* retObj = method->invoker_method(method->methodPointerCallByInterp, method, thisPtr, invokeParams);
		if (traceDictionarySetItem)
		{
			std::printf("[hotc233][M2NReflectionProbe] after-invoker this=%p method=%p retObj=%p ret=%p\n",
				thisPtr,
				(void*)method,
				retObj,
				ret);
			std::fflush(stdout);
		}
		if (ret)
		{
			const Il2CppType* returnType = method->return_type;
			if (hotc233::metadata::IsValueType(returnType))
			{
				Il2CppClass* returnKlass = il2cpp::vm::Class::FromIl2CppType(returnType);
				if (il2cpp::vm::Class::IsNullable(returnKlass))
				{
					il2cpp::vm::Object::UnboxNullable((Il2CppObject*)retObj, returnKlass->element_class, ret);
				}
				else
				{
					std::memcpy(ret, il2cpp::vm::Object::Unbox((Il2CppObject*)retObj), il2cpp::vm::Class::GetValueSize(returnKlass, nullptr));
				}
			}
			else
			{
				*(void**)ret = retObj;
			}
		}
#endif
	}

	bool TryManaged2NativeCallByReflectionInvokeForSharedStruct(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!method)
		{
			return false;
		}
		const Il2CppType* returnType = InflateMethodParameterTypeIfNeeded(method, method->return_type);
		if (IsFullGenericVariableValueTypeReturn(method))
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return true;
		}
		if (returnType && !returnType->byref && IsValueTypeForInvoke(returnType))
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return true;
		}
		bool shouldTraceM2N =
			method->name
			&& method->klass
			&& method->klass->name
			&& ((!std::strcmp(method->name, "set_Item") && std::strstr(method->klass->name, "Dictionary"))
				|| (!std::strcmp(method->name, "Compare") && std::strstr(method->klass->name, "NullableComparer")));
		if (shouldTraceM2N)
		{
			std::printf("[hotc233][M2NBridgeProbe] %s.%s pcount=%d\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				(int)method->parameters_count);
			std::fflush(stdout);
		}
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const Il2CppType* paramType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
			if (method->name && !std::strcmp(method->name, "set_Item") && method->klass && method->klass->name && std::strstr(method->klass->name, "Dictionary"))
			{
				Il2CppClass* paramKlass = paramType ? il2cpp::vm::Class::FromIl2CppType(paramType) : nullptr;
				std::printf("[hotc233][M2NBridgeProbe] param%d type=%d klass=%s.%s value=%d generated-bridge=1\n",
					(int)i,
					paramType ? (int)paramType->type : -1,
					paramKlass && paramKlass->namespaze ? paramKlass->namespaze : "",
					paramKlass && paramKlass->name ? paramKlass->name : "",
					paramType && IsValueTypeForInvoke(paramType) ? 1 : 0);
				std::fflush(stdout);
				continue;
			}
			if (method->name && !std::strcmp(method->name, "set_Item") && method->klass && method->klass->name && std::strstr(method->klass->name, "Dictionary"))
			{
				Il2CppClass* paramKlass = paramType ? il2cpp::vm::Class::FromIl2CppType(paramType) : nullptr;
				std::printf("[hotc233][M2NBridgeProbe] param%d type=%d klass=%s.%s value=%d\n",
					(int)i,
					paramType ? (int)paramType->type : -1,
					paramKlass && paramKlass->namespaze ? paramKlass->namespaze : "",
					paramKlass && paramKlass->name ? paramKlass->name : "",
					paramType && IsValueTypeForInvoke(paramType) ? 1 : 0);
				std::fflush(stdout);
			}
			if (!paramType->byref && IsValueTypeForInvoke(paramType))
			{
				InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
				return true;
			}
		}
		return false;
	}

	static bool IsDictionaryIntTryGetValueMethod(const MethodInfo* method)
	{
		if (!method
			|| !method->name
			|| std::strcmp(method->name, "TryGetValue") != 0
			|| !method->klass
			|| !method->klass->name
			|| std::strcmp(method->klass->name, "Dictionary`2") != 0
			|| !hotc233::metadata::IsInstanceMethod(method)
			|| method->parameters_count != 2
			|| !method->return_type
			|| method->return_type->type != IL2CPP_TYPE_BOOLEAN)
		{
			return false;
		}
		const Il2CppType* keyType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[0]));
		const Il2CppType* valueByRefType = GET_METHOD_PARAMETER_TYPE(method->parameters[1]);
		return keyType
			&& keyType->type == IL2CPP_TYPE_I4
			&& valueByRefType
			&& valueByRefType->byref;
	}

	struct DictionaryIntTryGetValueOffsets
	{
		size_t bucketsOffset;
		size_t entriesOffset;
		size_t entryHashCodeOffset;
		size_t entryNextOffset;
		size_t entryKeyOffset;
		size_t entryValueOffset;
		int32_t entrySize;
		int32_t valueSize;
		bool valid;
	};

	static std::unordered_map<Il2CppClass*, DictionaryIntTryGetValueOffsets> s_dictionaryIntTryGetValueOffsets;

	static FieldInfo* FindM2NFieldAny(Il2CppClass* klass, const char* firstName, const char* secondName = nullptr)
	{
		if (!klass)
		{
			return nullptr;
		}
		FieldInfo* field = il2cpp::vm::Class::GetFieldFromName(klass, firstName);
		if (!field && secondName)
		{
			field = il2cpp::vm::Class::GetFieldFromName(klass, secondName);
		}
		return field;
	}

	static const Il2CppType* GetClassGenericArg(const MethodInfo* method, uint32_t index)
	{
		if (!method
			|| !method->klass
			|| !method->klass->generic_class
			|| !method->klass->generic_class->context.class_inst
			|| method->klass->generic_class->context.class_inst->type_argc <= index)
		{
			return nullptr;
		}
		return method->klass->generic_class->context.class_inst->type_argv[index];
	}

	static bool TryBuildDictionaryIntTryGetValueOffsets(const MethodInfo* method, Il2CppArray* entries, DictionaryIntTryGetValueOffsets& offsets)
	{
		auto cached = s_dictionaryIntTryGetValueOffsets.find(method->klass);
		if (cached != s_dictionaryIntTryGetValueOffsets.end())
		{
			offsets = cached->second;
			return offsets.valid;
		}
		DictionaryIntTryGetValueOffsets next = { 0, 0, 0, 0, 0, 0, 0, 0, false };
		const Il2CppType* valueType = GetClassGenericArg(method, 1);
		Il2CppClass* valueKlass = valueType ? il2cpp::vm::Class::FromIl2CppType(valueType) : nullptr;
		if (!entries)
		{
			offsets = next;
			return false;
		}
		if (!valueKlass || !IS_CLASS_VALUE_TYPE(valueKlass))
		{
			s_dictionaryIntTryGetValueOffsets.insert({ method->klass, next });
			offsets = next;
			return false;
		}
		Il2CppClass* entryKlass = ((Il2CppObject*)entries)->klass->element_class;
		FieldInfo* bucketsField = FindM2NFieldAny(method->klass, "_buckets", "buckets");
		FieldInfo* entriesField = FindM2NFieldAny(method->klass, "_entries", "entries");
		FieldInfo* hashCodeField = FindM2NFieldAny(entryKlass, "hashCode", "_hashCode");
		FieldInfo* nextField = FindM2NFieldAny(entryKlass, "next", "_next");
		FieldInfo* keyField = FindM2NFieldAny(entryKlass, "key", "_key");
		FieldInfo* valueField = FindM2NFieldAny(entryKlass, "value", "_value");
		if (!bucketsField || !entriesField || !hashCodeField || !nextField || !keyField || !valueField)
		{
			s_dictionaryIntTryGetValueOffsets.insert({ method->klass, next });
			offsets = next;
			return false;
		}
		next.bucketsOffset = il2cpp::vm::Field::GetOffset(bucketsField);
		next.entriesOffset = il2cpp::vm::Field::GetOffset(entriesField);
		next.entryHashCodeOffset = hotc233::metadata::GetFieldOffset(hashCodeField);
		next.entryNextOffset = hotc233::metadata::GetFieldOffset(nextField);
		next.entryKeyOffset = hotc233::metadata::GetFieldOffset(keyField);
		next.entryValueOffset = hotc233::metadata::GetFieldOffset(valueField);
		next.entrySize = (int32_t)il2cpp::vm::Class::GetValueSize(entryKlass, nullptr);
		next.valueSize = (int32_t)il2cpp::vm::Class::GetValueSize(valueKlass, nullptr);
		next.valid = next.entrySize > 0 && next.valueSize > 0;
		s_dictionaryIntTryGetValueOffsets.insert({ method->klass, next });
		offsets = next;
		return next.valid;
	}

	static bool TryManaged2NativeCallDictionaryIntTryGetValueFastPath(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!IsDictionaryIntTryGetValueMethod(method) || !argVarIndexs || !localVarBase || !ret)
		{
			return false;
		}
		Il2CppObject* dict = localVarBase[argVarIndexs[0]].obj;
		void* valueOut = (localVarBase + argVarIndexs[2])->ptr;
		if (!dict || !valueOut)
		{
			return false;
		}
		DictionaryIntTryGetValueOffsets offsets;
		auto cached = s_dictionaryIntTryGetValueOffsets.find(method->klass);
		if (cached != s_dictionaryIntTryGetValueOffsets.end())
		{
			offsets = cached->second;
			if (!offsets.valid)
			{
				return false;
			}
		}
		else
		{
			FieldInfo* entriesField = FindM2NFieldAny(method->klass, "_entries", "entries");
			if (!entriesField)
			{
				return false;
			}
			Il2CppArray* entriesForCache = *(Il2CppArray**)((uint8_t*)dict + il2cpp::vm::Field::GetOffset(entriesField));
			if (!TryBuildDictionaryIntTryGetValueOffsets(method, entriesForCache, offsets))
			{
				return false;
			}
		}
		uint8_t* dictBase = (uint8_t*)dict;
		Il2CppArray* buckets = *(Il2CppArray**)(dictBase + offsets.bucketsOffset);
		Il2CppArray* entries = *(Il2CppArray**)(dictBase + offsets.entriesOffset);
		if (!buckets || !entries)
		{
			std::memset(valueOut, 0, (size_t)offsets.valueSize);
			std::memset(ret, 0, sizeof(StackObject));
			return true;
		}
		int32_t key = localVarBase[argVarIndexs[1]].i32;
		int32_t hashCode = key & 0x7fffffff;
		uint32_t bucketLength = il2cpp::vm::Array::GetLength(buckets);
		if (bucketLength == 0)
		{
			std::memset(valueOut, 0, (size_t)offsets.valueSize);
			std::memset(ret, 0, sizeof(StackObject));
			return true;
		}
		int32_t bucketIndex = hashCode % (int32_t)bucketLength;
		int32_t* bucketData = (int32_t*)il2cpp::vm::Array::GetFirstElementAddress(buckets);
		int32_t entryIndex = bucketData[bucketIndex] - 1;
		uint32_t entryLength = il2cpp::vm::Array::GetLength(entries);
		uint8_t* entryData = (uint8_t*)il2cpp::vm::Array::GetFirstElementAddress(entries);
		while (entryIndex >= 0 && (uint32_t)entryIndex < entryLength)
		{
			uint8_t* entry = entryData + (size_t)offsets.entrySize * (size_t)entryIndex;
			int32_t entryHash = *(int32_t*)(entry + offsets.entryHashCodeOffset);
			int32_t entryKey = *(int32_t*)(entry + offsets.entryKeyOffset);
			if (entryHash == hashCode && entryKey == key)
			{
				std::memcpy(valueOut, entry + offsets.entryValueOffset, (size_t)offsets.valueSize);
				std::memset(ret, 0, sizeof(StackObject));
				((StackObject*)ret)->b = true;
				return true;
			}
			entryIndex = *(int32_t*)(entry + offsets.entryNextOffset);
		}
		std::memset(valueOut, 0, (size_t)offsets.valueSize);
		std::memset(ret, 0, sizeof(StackObject));
		return true;
	}

	enum AotContainerMethodKind
	{
		AotContainerMethod_Unsupported = 0,
		AotContainerMethod_ListClear,
		AotContainerMethod_ListGetCount,
		AotContainerMethod_ListAdd,
		AotContainerMethod_ListGetItem,
		AotContainerMethod_StackGetCount,
		AotContainerMethod_StackPush,
		AotContainerMethod_StackPop,
	};

	static AotContainerMethodKind ClassifyAotContainerMethod(const MethodInfo* method)
	{
		if (!method
			|| !method->name
			|| !method->klass
			|| !method->klass->name
			|| !method->klass->namespaze
			|| std::strcmp(method->klass->namespaze, "System.Collections.Generic") != 0
			|| !hotc233::metadata::IsInstanceMethod(method))
		{
			return AotContainerMethod_Unsupported;
		}
		const char* className = method->klass->name;
		const char* methodName = method->name;
		if (std::strcmp(className, "List`1") == 0)
		{
			if (method->parameters_count == 0)
			{
				if (std::strcmp(methodName, "Clear") == 0)
				{
					return AotContainerMethod_ListClear;
				}
				if (std::strcmp(methodName, "get_Count") == 0)
				{
					return AotContainerMethod_ListGetCount;
				}
			}
			if (method->parameters_count == 1)
			{
				if (std::strcmp(methodName, "Add") == 0)
				{
					return AotContainerMethod_ListAdd;
				}
				if (std::strcmp(methodName, "get_Item") == 0)
				{
					return AotContainerMethod_ListGetItem;
				}
			}
			return AotContainerMethod_Unsupported;
		}
		if (std::strcmp(className, "Stack`1") == 0)
		{
			if (method->parameters_count == 0)
			{
				if (std::strcmp(methodName, "get_Count") == 0)
				{
					return AotContainerMethod_StackGetCount;
				}
				if (std::strcmp(methodName, "Pop") == 0)
				{
					return AotContainerMethod_StackPop;
				}
			}
			if (std::strcmp(methodName, "Push") == 0 && method->parameters_count == 1)
			{
				return AotContainerMethod_StackPush;
			}
		}
		return AotContainerMethod_Unsupported;
	}

	static bool IsSupportedAotContainerInvokerMethod(const MethodInfo* method)
	{
		return ClassifyAotContainerMethod(method) != AotContainerMethod_Unsupported;
	}

	struct ListFieldOffsets
	{
		size_t itemsOffset;
		size_t sizeOffset;
		size_t versionOffset;
		bool elementIsValueType;
		bool elementHasReferences;
		bool elementIsInt32;
		bool valid;
	};

	static std::unordered_map<Il2CppClass*, ListFieldOffsets> s_listFieldOffsets;

	static const Il2CppType* GetFirstClassGenericArg(const MethodInfo* method)
	{
		if (!method
			|| !method->klass
			|| !method->klass->generic_class
			|| !method->klass->generic_class->context.class_inst
			|| method->klass->generic_class->context.class_inst->type_argc != 1)
		{
			return nullptr;
		}
		return method->klass->generic_class->context.class_inst->type_argv[0];
	}

	static bool IsListMethod(const MethodInfo* method)
	{
		return method
			&& method->klass
			&& method->klass->name
			&& std::strcmp(method->klass->name, "List`1") == 0
			&& GetFirstClassGenericArg(method) != nullptr;
	}

	static bool TryGetListFieldOffsets(const MethodInfo* method, ListFieldOffsets& offsets)
	{
		auto cached = s_listFieldOffsets.find(method->klass);
		if (cached != s_listFieldOffsets.end())
		{
			offsets = cached->second;
			return offsets.valid;
		}
		ListFieldOffsets next = { 0, 0, 0, false, false, false, false };
		const Il2CppType* elementType = GetFirstClassGenericArg(method);
		Il2CppClass* elementKlass = elementType ? il2cpp::vm::Class::FromIl2CppType(elementType) : nullptr;
		if (!elementKlass)
		{
			s_listFieldOffsets.insert({ method->klass, next });
			offsets = next;
			return false;
		}
		bool hasItems = false;
		bool hasSize = false;
		bool hasVersion = false;
		void* iter = nullptr;
		FieldInfo* field = nullptr;
		while ((field = il2cpp::vm::Class::GetFields(method->klass, &iter)) != nullptr)
		{
			const char* name = il2cpp::vm::Field::GetName(field);
			if (!name)
			{
				continue;
			}
			if (std::strcmp(name, "_items") == 0)
			{
				next.itemsOffset = il2cpp::vm::Field::GetOffset(field);
				hasItems = true;
			}
			else if (std::strcmp(name, "_size") == 0)
			{
				next.sizeOffset = il2cpp::vm::Field::GetOffset(field);
				hasSize = true;
			}
			else if (std::strcmp(name, "_version") == 0)
			{
				next.versionOffset = il2cpp::vm::Field::GetOffset(field);
				hasVersion = true;
			}
		}
		next.elementIsValueType = IS_CLASS_VALUE_TYPE(elementKlass);
		next.elementHasReferences = elementKlass->has_references;
		next.elementIsInt32 = elementType->type == IL2CPP_TYPE_I4;
		next.valid = hasItems && hasSize && hasVersion;
		s_listFieldOffsets.insert({ method->klass, next });
		offsets = next;
		return next.valid;
	}

	static bool TryManaged2NativeCallListFastPath(AotContainerMethodKind kind, const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!IsListMethod(method) || !method->name || !argVarIndexs || !localVarBase)
		{
			return false;
		}
		Il2CppObject* listObject = localVarBase[argVarIndexs[0]].obj;
		if (!listObject)
		{
			return false;
		}
		ListFieldOffsets offsets;
		if (!TryGetListFieldOffsets(method, offsets))
		{
			return false;
		}
		uint8_t* listBase = (uint8_t*)listObject;
		Il2CppArray* items = *(Il2CppArray**)(listBase + offsets.itemsOffset);
		int32_t* sizePtr = (int32_t*)(listBase + offsets.sizeOffset);
		int32_t* versionPtr = (int32_t*)(listBase + offsets.versionOffset);
		if (kind == AotContainerMethod_Unsupported)
		{
			kind = ClassifyAotContainerMethod(method);
		}
		if (kind == AotContainerMethod_ListGetCount)
		{
			if (ret)
			{
				std::memset(ret, 0, sizeof(StackObject));
				*(int32_t*)ret = *sizePtr;
			}
			return true;
		}
		if (kind == AotContainerMethod_ListClear)
		{
			int32_t size = *sizePtr;
			if (items && size > 0 && (!offsets.elementIsValueType || offsets.elementHasReferences))
			{
				uint32_t length = il2cpp::vm::Array::GetLength(items);
				uint32_t elementSize = items->klass->element_size;
				if ((uint32_t)size > length || elementSize == 0)
				{
					return false;
				}
				uint8_t* elementData = (uint8_t*)il2cpp::vm::Array::GetFirstElementAddress(items);
				std::memset(elementData, 0, (size_t)elementSize * (size_t)size);
				il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)elementData, (size_t)elementSize * (size_t)size);
			}
			*sizePtr = 0;
			++(*versionPtr);
			return true;
		}
		if (kind == AotContainerMethod_ListAdd)
		{
			if (!items)
			{
				return false;
			}
			uint32_t length = il2cpp::vm::Array::GetLength(items);
			uint32_t elementSize = items->klass->element_size;
			int32_t size = *sizePtr;
			if (size < 0 || (uint32_t)size >= length || elementSize == 0)
			{
				return false;
			}
			uint8_t* element = (uint8_t*)il2cpp::vm::Array::GetFirstElementAddress(items) + (size_t)elementSize * (size_t)size;
			if (offsets.elementIsValueType)
			{
				if (offsets.elementIsInt32)
				{
					*(int32_t*)element = localVarBase[argVarIndexs[1]].i32;
				}
				else
				{
					std::memcpy(element, localVarBase + argVarIndexs[1], (size_t)elementSize);
					if (offsets.elementHasReferences)
					{
						il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)element, (size_t)elementSize);
					}
				}
			}
			else
			{
				*(Il2CppObject**)element = localVarBase[argVarIndexs[1]].obj;
				il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)element);
			}
			*sizePtr = size + 1;
			++(*versionPtr);
			return true;
		}
		if (kind == AotContainerMethod_ListGetItem)
		{
			if (!items)
			{
				return false;
			}
			int32_t index = localVarBase[argVarIndexs[1]].i32;
			int32_t size = *sizePtr;
			if (index < 0 || index >= size)
			{
				return false;
			}
			uint32_t elementSize = items->klass->element_size;
			if (elementSize == 0)
			{
				return false;
			}
			uint8_t* element = (uint8_t*)il2cpp::vm::Array::GetFirstElementAddress(items) + (size_t)elementSize * (size_t)index;
			if (ret)
			{
				std::memset(ret, 0, sizeof(StackObject));
				if (offsets.elementIsValueType)
				{
					if (offsets.elementIsInt32)
					{
						*(int32_t*)ret = *(int32_t*)element;
					}
					else
					{
						std::memcpy(ret, element, (size_t)elementSize);
					}
				}
				else
				{
					((StackObject*)ret)->obj = *(Il2CppObject**)element;
				}
			}
			return true;
		}
		return false;
	}

	struct StackFieldOffsets
	{
		size_t arrayOffset;
		size_t sizeOffset;
		size_t versionOffset;
		bool elementIsValueType;
		bool elementHasReferences;
		bool valid;
	};

	static std::unordered_map<Il2CppClass*, StackFieldOffsets> s_stackFieldOffsets;

	static bool IsStackMethod(const MethodInfo* method)
	{
		return method
			&& method->klass
			&& method->klass->name
			&& std::strcmp(method->klass->name, "Stack`1") == 0
			&& GetFirstClassGenericArg(method) != nullptr;
	}

	static bool TryGetStackFieldOffsets(const MethodInfo* method, StackFieldOffsets& offsets)
	{
		auto cached = s_stackFieldOffsets.find(method->klass);
		if (cached != s_stackFieldOffsets.end())
		{
			offsets = cached->second;
			return offsets.valid;
		}
		StackFieldOffsets next = { 0, 0, 0, false, false, false };
		const Il2CppType* elementType = GetFirstClassGenericArg(method);
		Il2CppClass* elementKlass = elementType ? il2cpp::vm::Class::FromIl2CppType(elementType) : nullptr;
		FieldInfo* arrayField = FindM2NFieldAny(method->klass, "_array", "array");
		FieldInfo* sizeField = FindM2NFieldAny(method->klass, "_size", "size");
		FieldInfo* versionField = FindM2NFieldAny(method->klass, "_version", "version");
		if (!elementKlass || !arrayField || !sizeField || !versionField)
		{
			s_stackFieldOffsets.insert({ method->klass, next });
			offsets = next;
			return false;
		}
		next.arrayOffset = il2cpp::vm::Field::GetOffset(arrayField);
		next.sizeOffset = il2cpp::vm::Field::GetOffset(sizeField);
		next.versionOffset = il2cpp::vm::Field::GetOffset(versionField);
		next.elementIsValueType = IS_CLASS_VALUE_TYPE(elementKlass);
		next.elementHasReferences = elementKlass->has_references;
		next.valid = true;
		s_stackFieldOffsets.insert({ method->klass, next });
		offsets = next;
		return true;
	}

	static bool TryManaged2NativeCallStackFastPath(AotContainerMethodKind kind, const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!IsStackMethod(method) || !method->name || !argVarIndexs || !localVarBase)
		{
			return false;
		}
		Il2CppObject* stackObject = localVarBase[argVarIndexs[0]].obj;
		if (!stackObject)
		{
			return false;
		}
		StackFieldOffsets offsets;
		if (!TryGetStackFieldOffsets(method, offsets))
		{
			return false;
		}
		uint8_t* stackBase = (uint8_t*)stackObject;
		Il2CppArray* array = *(Il2CppArray**)(stackBase + offsets.arrayOffset);
		int32_t* sizePtr = (int32_t*)(stackBase + offsets.sizeOffset);
		int32_t* versionPtr = (int32_t*)(stackBase + offsets.versionOffset);
		if (kind == AotContainerMethod_Unsupported)
		{
			kind = ClassifyAotContainerMethod(method);
		}
		if (kind == AotContainerMethod_StackGetCount)
		{
			if (ret)
			{
				std::memset(ret, 0, sizeof(StackObject));
				*(int32_t*)ret = *sizePtr;
			}
			return true;
		}
		if (!array)
		{
			return false;
		}
		uint32_t length = il2cpp::vm::Array::GetLength(array);
		uint32_t elementSize = array->klass->element_size;
		uint8_t* elementData = (uint8_t*)il2cpp::vm::Array::GetFirstElementAddress(array);
		if (!elementData || elementSize == 0)
		{
			return false;
		}
		if (kind == AotContainerMethod_StackPush)
		{
			int32_t size = *sizePtr;
			if (size < 0 || (uint32_t)size >= length)
			{
				return false;
			}
			uint8_t* element = elementData + (size_t)elementSize * (size_t)size;
			if (offsets.elementIsValueType)
			{
				std::memcpy(element, localVarBase + argVarIndexs[1], (size_t)elementSize);
				if (offsets.elementHasReferences)
				{
					il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)element, (size_t)elementSize);
				}
			}
			else
			{
				*(Il2CppObject**)element = localVarBase[argVarIndexs[1]].obj;
				il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)element);
			}
			*sizePtr = size + 1;
			++(*versionPtr);
			return true;
		}
		if (kind == AotContainerMethod_StackPop)
		{
			int32_t size = *sizePtr;
			if (size <= 0 || (uint32_t)size > length)
			{
				return false;
			}
			int32_t nextSize = size - 1;
			uint8_t* element = elementData + (size_t)elementSize * (size_t)nextSize;
			if (ret)
			{
				if (offsets.elementIsValueType)
				{
					std::memcpy(ret, element, (size_t)elementSize);
				}
				else
				{
					std::memset(ret, 0, sizeof(StackObject));
					((StackObject*)ret)->obj = *(Il2CppObject**)element;
				}
			}
			std::memset(element, 0, (size_t)elementSize);
			if (!offsets.elementIsValueType || offsets.elementHasReferences)
			{
				il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)element, (size_t)elementSize);
			}
			*sizePtr = nextSize;
			++(*versionPtr);
			return true;
		}
		return false;
	}

	static void Managed2NativeCallAotContainerInvoker(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		AotContainerMethodKind kind = ClassifyAotContainerMethod(method);
		if (TryManaged2NativeCallListFastPath(kind, method, argVarIndexs, localVarBase, ret))
		{
			return;
		}
		if (TryManaged2NativeCallStackFastPath(kind, method, argVarIndexs, localVarBase, ret))
		{
			return;
		}
		if (!IsSupportedAotContainerInvokerMethod(method) || !method->invoker_method || !argVarIndexs || !localVarBase)
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return;
		}
		if (method->methodPointerCallByInterp == InterpreterModule::NotSupportNative2Managed)
		{
			InitAndGetInterpreterDirectlyCallMethodPointer(method);
		}
		if (method->methodPointerCallByInterp == InterpreterModule::NotSupportNative2Managed)
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return;
		}
		void* thisPtr = localVarBase[argVarIndexs[0]].obj;
		void* invokeParams[4];
		uint16_t* argVarIndexBase = argVarIndexs + 1;
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			const Il2CppType* argType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
			if (!argType)
			{
				InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
				return;
			}
			StackObject* argValue = localVarBase + argVarIndexBase[i];
			invokeParams[i] = (!argType->byref && IsValueTypeForInvoke(argType)) ? (void*)argValue : argValue->ptr;
		}
		void* actualRet = ret;
		int32_t retStorageSize = GetManaged2NativeReturnStorageSize(method);
		if (ret && retStorageSize > 0)
		{
			for (uint8_t i = 0; i < method->parameters_count; i++)
			{
				const Il2CppType* argType = InflateMethodParameterTypeIfNeeded(method, GET_METHOD_PARAMETER_TYPE(method->parameters[i]));
				if (!argType || argType->byref || !IsValueTypeForInvoke(argType))
				{
					continue;
				}
				int32_t argSize = metadata::GetTypeValueSize(argType);
				if (IsAddressRangeOverlap(ret, retStorageSize, invokeParams[i], argSize))
				{
					actualRet = alloca((size_t)retStorageSize);
					break;
				}
			}
		}
		ClearManaged2NativeReturnStorage(method, actualRet);
#if HOTC233_UNITY_2021_OR_NEW
		Il2CppMethodPointer invokerTarget = method->methodPointer != nullptr ? method->methodPointer : method->methodPointerCallByInterp;
		if (method->has_full_generic_sharing_signature && method->methodPointerCallByInterp != nullptr)
		{
			invokerTarget = method->methodPointerCallByInterp;
		}
		bool fullGenericVariableValueReturnHandled = TryInvokeFullGenericVariableValueReturn(method, thisPtr, invokeParams, actualRet);
		if (!fullGenericVariableValueReturnHandled)
		{
			method->invoker_method(invokerTarget, method, thisPtr, invokeParams, actualRet);
		}
		NormalizeFullGenericValueTypeReturn(method, actualRet, fullGenericVariableValueReturnHandled);
		if (actualRet != ret && ret && retStorageSize > 0)
		{
			std::memcpy(ret, actualRet, (size_t)retStorageSize);
		}
#else
		InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
#endif
	}

	static void Managed2NativeCallListClear(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallListFastPath(AotContainerMethod_ListClear, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallListGetCount(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallListFastPath(AotContainerMethod_ListGetCount, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallListAdd(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallListFastPath(AotContainerMethod_ListAdd, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallListGetItem(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallListFastPath(AotContainerMethod_ListGetItem, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallStackGetCount(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallStackFastPath(AotContainerMethod_StackGetCount, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallStackPush(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallStackFastPath(AotContainerMethod_StackPush, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallStackPop(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (!TryManaged2NativeCallStackFastPath(AotContainerMethod_StackPop, method, argVarIndexs, localVarBase, ret))
		{
			Managed2NativeCallAotContainerInvoker(method, argVarIndexs, localVarBase, ret);
		}
	}

	static void Managed2NativeCallDictionaryIntTryGetValue(const MethodInfo* method, uint16_t* argVarIndexs, StackObject* localVarBase, void* ret)
	{
		if (TryManaged2NativeCallDictionaryIntTryGetValueFastPath(method, argVarIndexs, localVarBase, ret))
		{
			return;
		}
		if (!IsDictionaryIntTryGetValueMethod(method) || !method->invoker_method)
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return;
		}
		if (method->methodPointerCallByInterp == InterpreterModule::NotSupportNative2Managed)
		{
			InitAndGetInterpreterDirectlyCallMethodPointer(method);
		}
		if (method->methodPointerCallByInterp == InterpreterModule::NotSupportNative2Managed)
		{
			InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
			return;
		}
		void* thisPtr = localVarBase[argVarIndexs[0]].obj;
		void* invokeParams[2];
		invokeParams[0] = localVarBase + argVarIndexs[1];
		invokeParams[1] = (localVarBase + argVarIndexs[2])->ptr;
		ClearManaged2NativeReturnStorage(method, ret);
#if HOTC233_UNITY_2021_OR_NEW
		Il2CppMethodPointer invokerTarget = method->methodPointer != nullptr ? method->methodPointer : method->methodPointerCallByInterp;
		if (method->has_full_generic_sharing_signature && method->methodPointerCallByInterp != nullptr)
		{
			invokerTarget = method->methodPointerCallByInterp;
		}
		method->invoker_method(invokerTarget, method, thisPtr, invokeParams, ret);
#else
		InterpreterModule::Managed2NativeCallByReflectionInvoke(method, argVarIndexs, localVarBase, ret);
#endif
	}

	Managed2NativeCallMethod InterpreterModule::GetManaged2NativeMethodPointer(const MethodInfo* method, bool forceStatic)
	{
		if (!forceStatic)
		{
			switch (ClassifyAotContainerMethod(method))
			{
			case AotContainerMethod_ListClear:
				return Managed2NativeCallListClear;
			case AotContainerMethod_ListGetCount:
				return Managed2NativeCallListGetCount;
			case AotContainerMethod_ListAdd:
				return Managed2NativeCallListAdd;
			case AotContainerMethod_ListGetItem:
				return Managed2NativeCallListGetItem;
			case AotContainerMethod_StackGetCount:
				return Managed2NativeCallStackGetCount;
			case AotContainerMethod_StackPush:
				return Managed2NativeCallStackPush;
			case AotContainerMethod_StackPop:
				return Managed2NativeCallStackPop;
			default:
				break;
			}
		}
		if (IsFullGenericVariableValueTypeReturn(method))
		{
			return Managed2NativeCallByReflectionInvoke;
		}
		if (method->methodPointerCallByInterp == NotSupportNative2Managed)
		{
			InitAndGetInterpreterDirectlyCallMethodPointer(method);
		}
		if (method->methodPointerCallByInterp == NotSupportNative2Managed)
		{
			return Managed2NativeCallByReflectionInvoke;
		}
		if (!forceStatic && IsDictionaryIntTryGetValueMethod(method))
		{
			return Managed2NativeCallDictionaryIntTryGetValue;
		}
		char sigName[kMaxSignatureNameLength];
		ComputeSignature(method, !forceStatic, sigName, sizeof(sigName) - 1);
		auto it = s_managed2natives.find(sigName);
		Managed2NativeCallMethod bridge = it != s_managed2natives.end() ? it->second : Managed2NativeCallByReflectionInvoke;
		if (method->name
			&& method->klass
			&& method->klass->name
			&& ((!std::strcmp(method->name, "set_Item") && std::strstr(method->klass->name, "Dictionary"))
				|| (!std::strcmp(method->name, "Compare") && std::strstr(method->klass->name, "NullableComparer"))
				|| (!std::strcmp(method->name, "Invoke") && method->klass->namespaze && std::strstr(method->klass->namespaze, "System.Reflection"))))
		{
			std::printf("[hotc233][M2NProbe] %s.%s sig=%s inflated=%d generic=%d full=%d bridge=%p reflection=%p methodPointer=%p callByInterp=%p invoker=%p pcount=%d\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				sigName,
				method->is_inflated ? 1 : 0,
				method->is_generic ? 1 : 0,
				method->has_full_generic_sharing_signature ? 1 : 0,
				(void*)bridge,
				(void*)Managed2NativeCallByReflectionInvoke,
				(void*)method->methodPointer,
				(void*)method->methodPointerCallByInterp,
				(void*)method->invoker_method,
				(int)method->parameters_count);
			for (uint8_t i = 0; i < method->parameters_count; i++)
			{
				const Il2CppType* rawType = GET_METHOD_PARAMETER_TYPE(method->parameters[i]);
				const Il2CppType* inflatedType = InflateMethodParameterTypeIfNeeded(method, rawType);
				Il2CppClass* inflatedKlass = inflatedType ? il2cpp::vm::Class::FromIl2CppType(inflatedType) : nullptr;
				std::printf("[hotc233][M2NProbe] param%d raw=%d inflated=%d byref=%d value=%d\n",
					(int)i,
					rawType ? (int)rawType->type : -1,
					inflatedType ? (int)inflatedType->type : -1,
					rawType && rawType->byref ? 1 : 0,
					inflatedType && IsValueTypeForInvoke(inflatedType) ? 1 : 0);
				std::printf("[hotc233][M2NProbe] param%d klass=%s.%s valuetype=%d\n",
					(int)i,
					inflatedKlass && inflatedKlass->namespaze ? inflatedKlass->namespaze : "",
					inflatedKlass && inflatedKlass->name ? inflatedKlass->name : "",
					inflatedKlass && IS_CLASS_VALUE_TYPE(inflatedKlass) ? 1 : 0);
				if (inflatedKlass && inflatedKlass->name && std::strcmp(inflatedKlass->name, "Nullable`1") == 0)
				{
					void* fieldIter = nullptr;
					FieldInfo* field = nullptr;
					while ((field = il2cpp::vm::Class::GetFields(inflatedKlass, &fieldIter)) != nullptr)
					{
						std::printf("[hotc233][M2NProbe] param%d field=%s offset=%zu\n",
							(int)i,
							il2cpp::vm::Field::GetName(field),
							il2cpp::vm::Field::GetOffset(field));
					}
				}
			}
			std::fflush(stdout);
		}
		if (method->name
			&& method->klass
			&& method->klass->name
			&& !std::strcmp(method->name, "Compare")
			&& std::strstr(method->klass->name, "NullableComparer")
#if HOTC233_UNITY_2021_OR_NEW
			&& method->has_full_generic_sharing_signature
#endif
			)
		{
			return Managed2NativeCallByReflectionInvoke;
		}
		if (bridge != Managed2NativeCallByReflectionInvoke && ShouldUseInvokerForSharedStructM2N(method, sigName))
		{
			bridge = Managed2NativeCallByReflectionInvoke;
		}
#if HOTC233_ENABLE_AOT_CODE_PRETOUCH
		// Fault-in the marshalling bridge thunk page now (transform time, before the timed first
		// call). For struct/multi-arg signatures the interp->AOT path runs through this generic
		// bridge, whose code page is otherwise cold on first use and costs a ~155us page fault.
		PreTouchCodePtr((const void*)bridge);
#endif
		return bridge;
	}

	Managed2NativeCallMethod InterpreterModule::GetManaged2NativeMethodPointer(const metadata::ResolveStandAloneMethodSig& method)
	{
		char sigName[kMaxSignatureNameLength];
		ComputeSignature(method.returnType, method.params, metadata::IsPrologHasThis(method.flags), sigName, sizeof(sigName) - 1);
		auto it = s_managed2natives.find(sigName);
		Managed2NativeCallMethod bridge = it != s_managed2natives.end() ? it->second : Managed2NativeCallByReflectionInvoke;
#if HOTC233_ENABLE_AOT_CODE_PRETOUCH
		PreTouchCodePtr((const void*)bridge);
#endif
		return bridge;
	}

	Managed2NativeFunctionPointerCallMethod InterpreterModule::GetManaged2NativeFunctionPointerMethodPointer(const MethodInfo* method, Il2CppCallConvention callConvention)
	{
		char sigName[kMaxSignatureNameLength];
		sigName[0] = 'A' + callConvention;
		ComputeSignature(method, false, sigName + 1, sizeof(sigName) - 1);
		auto it = s_managed2nativeFunctionPointers.find(sigName);
		return it != s_managed2nativeFunctionPointers.end() ? it->second : NotSupportManaged2NativeFunctionMethod;
	}

	Managed2NativeFunctionPointerCallMethod InterpreterModule::GetManaged2NativeFunctionPointerMethodPointer(const metadata::ResolveStandAloneMethodSig& method)
	{
		int32_t callConvention = method.flags & 0x7;
		char sigName[kMaxSignatureNameLength];
		sigName[0] = 'A' + callConvention;
		ComputeSignature(method.returnType, method.params, metadata::IsPrologHasThis(method.flags), sigName + 1, sizeof(sigName) - 1);
		auto it = s_managed2nativeFunctionPointers.find(sigName);
		return it != s_managed2nativeFunctionPointers.end() ? it->second : NotSupportManaged2NativeFunctionMethod;
	}

	static void RaiseExecutionEngineExceptionMethodIsNotFound(const MethodInfo* method)
	{
		if (il2cpp::vm::Method::GetClass(method))
			RaiseExecutionEngineException(il2cpp::vm::Method::GetFullName(method).c_str());
		else
			RaiseExecutionEngineException(il2cpp::vm::Method::GetNameWithGenericTypes(method).c_str());
	}

	#ifdef HOTC233_UNITY_2021_OR_NEW

	static int32_t GetNativeReturnClearSize(const MethodInfo* method)
	{
		if (!method || !method->return_type || metadata::IsVoidType(method->return_type))
		{
			return 0;
		}
		if (method->return_type->type == IL2CPP_TYPE_VAR || method->return_type->type == IL2CPP_TYPE_MVAR)
		{
			return 0;
		}
		return metadata::GetTypeValueSize(method->return_type);
	}

	static void ClearNativeReturnStorage(const MethodInfo* method, void* ret, bool hiddenReturnSlot)
	{
		if (!ret)
		{
			return;
		}

		int32_t clearSize = GetNativeReturnClearSize(method);
		if (clearSize <= 0)
		{
			return;
		}

		if (hiddenReturnSlot && clearSize <= (int32_t)sizeof(StackObject))
		{
			clearSize = sizeof(StackObject);
		}
		std::memset(ret, 0, (size_t)clearSize);
	}
	
	static void InterpreterInvoke(Il2CppMethodPointer methodPointer, const MethodInfo* method, void* __this, void** __args, void* __ret)
	{
		InterpMethodInfo* imi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
		bool isInstanceMethod = metadata::IsInstanceMethod(method);
		bool traceLinqLambda = method
			&& method->klass
			&& method->name
			&& (std::strstr(method->name, "VerifyJoin")
				|| std::strstr(method->name, "b__14"));
		if (traceLinqLambda)
		{
			std::printf("[hotc233][InterpreterInvokeJoinProbe] enter %s.%s::%s method=%p methodPtr=%p this=%p args=%p arg0=%p arg1=%p ret=%p retBefore=%llu returnType=%d pcount=%d instance=%d\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				method->name,
				(void*)method,
				(void*)methodPointer,
				__this,
				__args,
				__args && method->parameters_count > 0 ? __args[0] : nullptr,
				__args && method->parameters_count > 1 ? __args[1] : nullptr,
				__ret,
				__ret ? (unsigned long long)*(uint64_t*)__ret : 0ULL,
				method->return_type ? (int)method->return_type->type : -1,
				(int)method->parameters_count,
				isInstanceMethod ? 1 : 0);
			std::fflush(stdout);
		}
		StackObject* args = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
		if (isInstanceMethod)
		{
			if (IS_CLASS_VALUE_TYPE(method->klass))
			{
				__this = (Il2CppObject*)__this + (methodPointer != method->methodPointerCallByInterp);
			}
			args[0].ptr = __this;
		}
		
		MethodArgDesc* argDescs = imi->args + isInstanceMethod;
		ConvertInvokeArgs(args + isInstanceMethod, method, argDescs, __args);
		ClearNativeReturnStorage(method, __ret, false);
		Interpreter::Execute(method, args, __ret);
		if (traceLinqLambda)
		{
			std::printf("[hotc233][InterpreterInvokeJoinProbe] exit %s.%s::%s ret=%p retAfter=%llu retI4=%d arg0StackPtr=%p arg0StackU64=%llu\n",
				method->klass->namespaze ? method->klass->namespaze : "",
				method->klass->name ? method->klass->name : "",
				method->name,
				__ret,
				__ret ? (unsigned long long)*(uint64_t*)__ret : 0ULL,
				__ret ? *(int32_t*)__ret : 0,
				(void*)args[isInstanceMethod ? 1 : 0].ptr,
				(unsigned long long)args[isInstanceMethod ? 1 : 0].u64);
			std::fflush(stdout);
		}
	}

	static void InterpreterDelegateInvoke(Il2CppMethodPointer methodPointer, const MethodInfo* method, void* __this, void** __args, void* __ret)
	{
		Il2CppMulticastDelegate* del = (methodPointer != method->methodPointerCallByInterp && methodPointer != method->methodPointer)
			? (Il2CppMulticastDelegate*)methodPointer
			: (Il2CppMulticastDelegate*)__this;
		const MethodInfo* invokeMethod = del && del->delegate.object.klass
			? il2cpp::vm::Class::GetMethodFromName(del->delegate.object.klass, "Invoke", -1)
			: method;
		void* actualRet = __ret;
		if (!actualRet && invokeMethod && !metadata::IsReturnVoidMethod(invokeMethod))
		{
			actualRet = __args[invokeMethod->parameters_count];
			static int32_t s_invokerHiddenRetTraceCount = 0;
			if (actualRet && s_invokerHiddenRetTraceCount < 64)
			{
				++s_invokerHiddenRetTraceCount;
				std::printf("[hotc233][InvokerHiddenRetProbe] invoke=%s.%s::%s params=%u ret=%p retU64=%llu\n",
					invokeMethod->klass ? invokeMethod->klass->namespaze : "<null>",
					invokeMethod->klass ? invokeMethod->klass->name : "<null>",
					invokeMethod->name,
					invokeMethod->parameters_count,
					actualRet,
					(unsigned long long)*(uint64_t*)actualRet);
				std::fflush(stdout);
			}
		}
		Il2CppDelegate** firstSubDel;
		int32_t subDelCount;
		if (del->delegates)
		{
			firstSubDel = (Il2CppDelegate**)il2cpp::vm::Array::GetFirstElementAddress(del->delegates);
			subDelCount = il2cpp::vm::Array::GetLength(del->delegates);
		}
		else
		{
			firstSubDel = (Il2CppDelegate**)&del;
			firstSubDel[0] = &del->delegate;
			subDelCount = 1;
		}

		for (int32_t i = 0; i < subDelCount; i++)
		{
			Il2CppDelegate* cur = firstSubDel[i];
			const MethodInfo* curMethod = cur->method;
			Il2CppObject* curTarget = cur->target;
			if (curMethod->invoker_method == nullptr)
			{
				RaiseExecutionEngineExceptionMethodIsNotFound(curMethod);
			}
			if (!InitAndGetInterpreterDirectlyCallMethodPointer(curMethod))
			{
				RaiseAOTGenericMethodNotInstantiatedException(curMethod);
			}
			const bool boxReturnToExpectedSlot = actualRet && ShouldBoxDelegateTargetReturn(invokeMethod, curMethod);
			void* curRet = actualRet;
			if (boxReturnToExpectedSlot)
			{
				const int32_t retStorageSize = GetDelegateTargetReturnStorageSize(curMethod);
				curRet = alloca((size_t)retStorageSize);
				std::memset(curRet, 0, (size_t)retStorageSize);
			}
			switch ((int)(invokeMethod->parameters_count - curMethod->parameters_count))
			{
			case 0:
			{
				if (metadata::IsInstanceMethod(curMethod) && !curTarget)
				{
					il2cpp::vm::Exception::RaiseNullReferenceException();
				}
				curTarget += (IS_CLASS_VALUE_TYPE(curMethod->klass));
				ClearNativeReturnStorage(curMethod, curRet, true);
				curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, curTarget, __args, curRet);
				break;
			}
			case -1:
			{
				IL2CPP_ASSERT(!hotc233::metadata::IsInstanceMethod(curMethod));
				void** newArgs = (void**)alloca(sizeof(void*) * (size_t)(curMethod->parameters_count + 1));
				newArgs[0] = curTarget;
				for (int k = 0, endK = curMethod->parameters_count; k < endK; k++)
				{
					newArgs[k + 1] = __args[k];
				}
				ClearNativeReturnStorage(curMethod, curRet, true);
				curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, nullptr, newArgs, curRet);
				break;
			}
			case 1:
			{
				IL2CPP_ASSERT(hotc233::metadata::IsInstanceMethod(curMethod));
				curTarget = (Il2CppObject*)__args[0];
				if (!curTarget)
				{
					il2cpp::vm::Exception::RaiseNullReferenceException();
				}
				ClearNativeReturnStorage(curMethod, curRet, true);
				curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, curTarget, __args + 1, curRet);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("bad delegate method");
				break;
			}
			}
			if (boxReturnToExpectedSlot)
			{
				BoxDelegateTargetReturnToExpectedSlot(curMethod, curRet, actualRet);
			}
		}
	}
	#else
	static void* InterpreterInvoke(Il2CppMethodPointer methodPointer, const MethodInfo* method, void* __this, void** __args)
	{
		InterpMethodInfo* imi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
		StackObject* args = (StackObject*)alloca(sizeof(StackObject) * imi->argStackObjectSize);
		bool isInstanceMethod = metadata::IsInstanceMethod(method);
		if (isInstanceMethod)
		{
			if (IS_CLASS_VALUE_TYPE(method->klass))
			{
				__this = (Il2CppObject*)__this + (methodPointer != method->methodPointerCallByInterp);
			}
			args[0].ptr = __this;
		}
		MethodArgDesc* argDescs = imi->args + isInstanceMethod;
		ConvertInvokeArgs(args + isInstanceMethod, method, argDescs, __args);
		if (method->return_type->type == IL2CPP_TYPE_VOID)
		{
			Interpreter::Execute(method, args, nullptr);
			return nullptr;
		}
		else
		{
			StackObject* ret = (StackObject*)alloca(sizeof(StackObject) * imi->retStackObjectSize);
			Interpreter::Execute(method, args, ret);
			return TranslateNativeValueToBoxValue(method->return_type, ret);
		}
	}

	static void* InterpreterDelegateInvoke(Il2CppMethodPointer methodPointer, const MethodInfo* method, void* __this, void** __args)
	{
		Il2CppMulticastDelegate* del = (methodPointer != method->methodPointerCallByInterp && methodPointer != method->methodPointer)
			? (Il2CppMulticastDelegate*)methodPointer
			: (Il2CppMulticastDelegate*)__this;
		const MethodInfo* invokeMethod = del && del->delegate.object.klass
			? il2cpp::vm::Class::GetMethodFromName(del->delegate.object.klass, "Invoke", -1)
			: method;
		Il2CppDelegate** firstSubDel;
		int32_t subDelCount;
		if (del->delegates)
		{
			firstSubDel = (Il2CppDelegate**)il2cpp::vm::Array::GetFirstElementAddress(del->delegates);
			subDelCount = il2cpp::vm::Array::GetLength(del->delegates);
		}
		else
		{
			firstSubDel = (Il2CppDelegate**)&del;
			firstSubDel[0] = &del->delegate;
			subDelCount = 1;
		}
		void* ret = nullptr;
		for (int32_t i = 0; i < subDelCount; i++)
		{
			Il2CppDelegate* cur = firstSubDel[i];
			const MethodInfo* curMethod = cur->method;
			Il2CppObject* curTarget = cur->target;
			if (curMethod->invoker_method == nullptr)
			{
				RaiseExecutionEngineExceptionMethodIsNotFound(curMethod);
			}
			if (!InitAndGetInterpreterDirectlyCallMethodPointer(curMethod))
			{
				RaiseAOTGenericMethodNotInstantiatedException(curMethod);
			}
			switch ((int)(invokeMethod->parameters_count - curMethod->parameters_count))
			{
			case 0:
			{
				if (metadata::IsInstanceMethod(curMethod) && !curTarget)
				{
					il2cpp::vm::Exception::RaiseNullReferenceException();
				}
				curTarget += (IS_CLASS_VALUE_TYPE(curMethod->klass));
				ret = curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, curTarget, __args);
				break;
			}
			case -1:
			{
				IL2CPP_ASSERT(!hotc233::metadata::IsInstanceMethod(curMethod));
				void** newArgs = (void**)alloca(sizeof(void*) * (size_t)(curMethod->parameters_count + 1));
				newArgs[0] = curTarget;
				for (int k = 0, endK = curMethod->parameters_count; k < endK; k++)
				{
					newArgs[k + 1] = __args[k];
				}
				ret = curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, nullptr, newArgs);
				break;
			}
			case 1:
			{
				IL2CPP_ASSERT(hotc233::metadata::IsInstanceMethod(curMethod));
				curTarget = (Il2CppObject*)__args[0];
				if (!curTarget)
				{
					il2cpp::vm::Exception::RaiseNullReferenceException();
				}
				ret = curMethod->invoker_method(curMethod->methodPointerCallByInterp, curMethod, curTarget, __args + 1);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("bad delegate method");
				break;
			}
			}
		}
		return ret;
	}
	#endif

	InvokerMethod InterpreterModule::GetMethodInvoker(const Il2CppMethodDefinition* method)
	{
		Il2CppClass* klass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromTypeDefinitionIndex(method->declaringType);
		const char* methodName = il2cpp::vm::GlobalMetadata::GetStringFromIndex(method->nameIndex);
		// special for Delegate::DynamicInvoke
		return !klass || !metadata::IsChildTypeOfMulticastDelegate(klass) || strcmp(methodName, "Invoke") ? InterpreterInvoke : InterpreterDelegateInvoke;
	}

	InvokerMethod InterpreterModule::GetMethodInvoker(const MethodInfo* method)
	{
		Il2CppClass* klass = method->klass;
		return !klass || !metadata::IsChildTypeOfMulticastDelegate(klass) || strcmp(method->name, "Invoke") ? InterpreterInvoke : InterpreterDelegateInvoke;
	}

	bool InterpreterModule::IsImplementsByInterpreter(const MethodInfo* method)
	{
		return method->invoker_method == InterpreterDelegateInvoke || method->invoker_method == InterpreterInvoke;
	}

	InterpMethodInfo* InterpreterModule::GetInterpMethodInfo(const MethodInfo* methodInfo)
	{
		il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);

		if (methodInfo->interpData)
		{
			return (InterpMethodInfo*)methodInfo->interpData;
		}
		IL2CPP_ASSERT(methodInfo->isInterpterImpl);

		il2cpp::vm::Class::Init(methodInfo->klass);
		InterpMethodInfo* imi = transform::HiTransform::Transform(methodInfo);
		il2cpp::os::Atomic::FullMemoryBarrier();
		const_cast<MethodInfo*>(methodInfo)->interpData = imi;
		return imi;
	}
}
}

