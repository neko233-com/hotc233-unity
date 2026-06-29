#include "Il2CppCompatibleDef.h"

#include "vm/Runtime.h"
#include "vm/Class.h"

#include "metadata/MetadataModule.h"
#include "interpreter/InterpreterModule.h"


namespace hotc233
{
	Il2CppMethodPointer InitAndGetInterpreterDirectlyCallMethodPointerSlow(MethodInfo* method)
	{
		IL2CPP_ASSERT(!method->initInterpCallMethodPointer);
		method->initInterpCallMethodPointer = true;
		bool isAdjustorThunkMethod = IS_CLASS_VALUE_TYPE(method->klass) && hotc233::metadata::IsInstanceMethod(method);
		if (hotc233::metadata::MetadataModule::IsImplementedByInterpreter(method))
		{
			method->methodPointerCallByInterp = interpreter::InterpreterModule::GetMethodPointer(method);
			if (isAdjustorThunkMethod)
			{
				method->virtualMethodPointerCallByInterp = interpreter::InterpreterModule::GetAdjustThunkMethodPointer(method);
			}
			else
			{
				method->virtualMethodPointerCallByInterp = method->methodPointerCallByInterp;
			}
			if (method->invoker_method == nullptr
#if HOTC233_UNITY_2021_OR_NEW
				|| method->invoker_method == il2cpp::vm::Runtime::GetMissingMethodInvoker()
				|| method->has_full_generic_sharing_signature
#endif
				)
			{
				method->invoker_method = hotc233::interpreter::InterpreterModule::GetMethodInvoker(method);
			}
#if HOTC233_UNITY_2021_OR_NEW
			if (method->methodPointer == nullptr || method->has_full_generic_sharing_signature)
			{
				method->methodPointer = method->methodPointerCallByInterp;
			}
			if (method->virtualMethodPointer == nullptr || method->has_full_generic_sharing_signature)
			{
				method->virtualMethodPointer = method->virtualMethodPointerCallByInterp;
			}
#else
			if (method->methodPointer == nullptr)
			{
				method->methodPointer = method->virtualMethodPointerCallByInterp;
			}
#endif
			method->isInterpterImpl = true;
		}
		return method->methodPointerCallByInterp;
	}

	Il2CppMethodPointer InitAndGetInterpreterDelegateInvokeMethodPointer(Il2CppDelegate* delegate)
	{
		const MethodInfo* invokeMethod = il2cpp::vm::Class::GetMethodFromName(delegate->object.klass, "Invoke", -1);
		if (!invokeMethod)
		{
			RaiseExecutionEngineException("delegate Invoke method missing");
			return (Il2CppMethodPointer)interpreter::InterpreterModule::NotSupportNative2Managed;
		}
		return interpreter::InterpreterModule::GetMethodPointer(invokeMethod);
	}
}
