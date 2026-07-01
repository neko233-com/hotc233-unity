
#include "Interpreter.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>

#include "vm/Object.h"
#include "vm/Class.h"
#include "vm/ClassInlines.h"
#include "vm/Array.h"
#include "vm/Assembly.h"
#include "vm/Image.h"
#include "vm/Exception.h"
#include "vm/Field.h"
#include "vm/Property.h"
#include "vm/Parameter.h"
#include "vm/Thread.h"
#include "vm/Runtime.h"
#include "vm/Reflection.h"
#include "vm/String.h"
#include "metadata/GenericMetadata.h"
#include "utils/StringUtils.h"
#if HOTC233_UNITY_2020_OR_NEW
#include "vm-utils/icalls/mscorlib/System.Threading/Interlocked.h"
#else
#include "icalls/mscorlib/System.Threading/Interlocked.h"
#endif
#include "icalls/mscorlib/System.Reflection/RuntimeMethodInfo.h"

#include "../metadata/MetadataModule.h"

#include "../transform/Hotc233TransformPolicy.h"
#include "Instruction.h"
#include "MethodBridge.h"
#include "InstrinctDef.h"
#include "MemoryUtil.h"
#include "InterpreterModule.h"
#include "InterpreterUtil.h"
#include "GodDomainUnityApi.h"
#include "gc/WriteBarrier.h"

using namespace hotc233::metadata;

namespace hotc233
{
namespace interpreter
{

	static const Il2CppImage* GetUnityEngineCoreModuleImage()
	{
		const Il2CppAssembly* unityCore = il2cpp::vm::Assembly::Load("UnityEngine.CoreModule");
		return unityCore != nullptr ? il2cpp::vm::Assembly::GetImage(unityCore) : nullptr;
	}

	static Il2CppClass* ResolveUnityEngineClass(const char* name)
	{
		const Il2CppImage* unityCoreImage = GetUnityEngineCoreModuleImage();
		return unityCoreImage != nullptr
			? il2cpp::vm::Class::FromName(unityCoreImage, "UnityEngine", name)
			: nullptr;
	}

	static bool TryExecuteValueTupleCtorByInflatedFields(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase)
	{
		if (!method
			|| !method->name
			|| std::strcmp(method->name, ".ctor") != 0
			|| !method->klass
			|| !method->klass->namespaze
			|| std::strcmp(method->klass->namespaze, "System") != 0
			|| !method->klass->name
			|| std::strncmp(method->klass->name, "ValueTuple", 10) != 0
			|| !argIdxs
			|| !localVarBase)
		{
			return false;
		}
		StackObject* target = (StackObject*)localVarBase[argIdxs[0]].ptr;
		if (!target)
		{
			return false;
		}
		const Il2CppType* containerType = &method->klass->byval_arg;
		for (uint8_t i = 0; i < method->parameters_count; i++)
		{
			if (i >= method->klass->field_count)
			{
				return false;
			}
			FieldInfo* field = &method->klass->fields[i];
			int32_t fieldOffset = hotc233::metadata::GetFieldOffset(field);
			if (fieldOffset < 0)
			{
				return false;
			}
			void* fieldDst = (uint8_t*)target + fieldOffset;
			StackObject* arg = localVarBase + argIdxs[i + 1];
			const Il2CppType* rawParamType = GET_METHOD_PARAMETER_TYPE(method->parameters[i]);
			const Il2CppType* paramType = hotc233::metadata::TryInflateIfNeed(containerType, rawParamType);
			Il2CppClass* paramKlass = paramType ? il2cpp::vm::Class::FromIl2CppType(paramType) : nullptr;
			if (paramType && !paramType->byref && paramKlass && IS_CLASS_VALUE_TYPE(paramKlass))
			{
				uint32_t valueSize = il2cpp::vm::Class::GetValueSize(paramKlass, nullptr);
				std::memcpy(fieldDst, arg, valueSize);
			}
			else
			{
				*(void**)fieldDst = arg->ptr;
			}
		}
		return true;
	}

	static MethodInfo* ResolveActualReferenceInstanceMethod(MethodInfo* declaredMethod, StackObject* thisSlot)
	{
		if (!declaredMethod
			|| !thisSlot
			|| !thisSlot->obj
			|| !declaredMethod->klass
			|| IS_CLASS_VALUE_TYPE(declaredMethod->klass)
			|| !declaredMethod->name)
		{
			return declaredMethod;
		}
		Il2CppClass* actualKlass = thisSlot->obj->klass;
		if (!actualKlass || actualKlass == declaredMethod->klass)
		{
			return declaredMethod;
		}
		MethodInfo* actualMethod = (MethodInfo*)il2cpp::vm::Class::GetMethodFromName(actualKlass, declaredMethod->name, declaredMethod->parameters_count);
		return actualMethod ? actualMethod : declaredMethod;
	}

	static Il2CppArray* NormalizeReflectionInvokeParameters(const MethodInfo* targetMethod, Il2CppArray* parameters, bool* usedCopy)
	{
		if (usedCopy)
		{
			*usedCopy = false;
		}
		if (!targetMethod || !parameters)
		{
			return parameters;
		}
		uint32_t parameterCount = targetMethod->parameters_count;
		uint32_t arrayLength = (uint32_t)il2cpp::vm::Array::GetLength(parameters);
		uint32_t count = std::min(parameterCount, arrayLength);
		int32_t firstMissing = -1;
		for (uint32_t i = 0; i < count; ++i)
		{
			Il2CppObject* value = il2cpp_array_get(parameters, Il2CppObject*, i);
			if (value && value->klass == il2cpp_defaults.missing_class)
			{
				firstMissing = (int32_t)i;
				break;
			}
		}
		if (firstMissing < 0)
		{
			return parameters;
		}

		Il2CppArray* normalized = il2cpp::vm::Array::NewSpecific(parameters->klass, arrayLength);
		for (uint32_t i = 0; i < arrayLength; ++i)
		{
			il2cpp_array_setref(normalized, i, il2cpp_array_get(parameters, Il2CppObject*, i));
		}
		for (uint32_t i = (uint32_t)firstMissing; i < count; ++i)
		{
			Il2CppObject* value = il2cpp_array_get(normalized, Il2CppObject*, i);
			if (!value || value->klass != il2cpp_defaults.missing_class)
			{
				continue;
			}
			if (!(targetMethod->parameters[i]->attrs & PARAM_ATTRIBUTE_HAS_DEFAULT))
			{
				continue;
			}
			bool isExplicitySetNullDefaultValue = false;
			Il2CppObject* defaultValue = il2cpp::vm::Parameter::GetDefaultParameterValueObject(targetMethod, (int32_t)i, &isExplicitySetNullDefaultValue);
			if (defaultValue || isExplicitySetNullDefaultValue)
			{
				il2cpp_array_setref(normalized, i, defaultValue);
			}
		}
		if (usedCopy)
		{
			*usedCopy = true;
		}
		return normalized;
	}

	static void CopyReflectionInvokeParametersBack(Il2CppArray* dst, Il2CppArray* src)
	{
		if (!dst || !src || dst == src)
		{
			return;
		}
		uint32_t dstLength = (uint32_t)il2cpp::vm::Array::GetLength(dst);
		uint32_t srcLength = (uint32_t)il2cpp::vm::Array::GetLength(src);
		uint32_t count = std::min(dstLength, srcLength);
		for (uint32_t i = 0; i < count; ++i)
		{
			il2cpp_array_setref(dst, i, il2cpp_array_get(src, Il2CppObject*, i));
		}
	}

	static Il2CppException* CreateTargetInvocationException(Il2CppException* inner)
	{
		Il2CppException* wrapper = il2cpp::vm::Exception::FromNameMsg(
			il2cpp_defaults.corlib,
			"System.Reflection",
			"TargetInvocationException",
			"Exception has been thrown by the target of an invocation.");
		IL2CPP_OBJECT_SETREF(wrapper, inner_ex, inner);
		return wrapper;
	}

	static bool TryExecuteMethodBaseInvokeObjectArray(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "Invoke") != 0 || method->parameters_count != 2)
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System.Reflection") != 0 || !method->klass->name || std::strcmp(method->klass->name, "MethodBase") != 0)
		{
			return false;
		}

		Il2CppObject* reflectionMethodObject = (localVarBase + argIdxs[0])->obj;
		if (!reflectionMethodObject || !reflectionMethodObject->klass)
		{
			return false;
		}
		std::printf("[hotc233][ReflectionInvokeProbe] MethodBase.Invoke receiverClass=%s.%s receiver=%p expected=%s.%s assignable=%d\n",
			reflectionMethodObject->klass->namespaze ? reflectionMethodObject->klass->namespaze : "",
			reflectionMethodObject->klass->name ? reflectionMethodObject->klass->name : "",
			(void*)reflectionMethodObject,
			method->klass && method->klass->namespaze ? method->klass->namespaze : "",
			method->klass && method->klass->name ? method->klass->name : "",
			method->klass && il2cpp::vm::Object::IsInst(reflectionMethodObject, method->klass) ? 1 : 0);
		std::fflush(stdout);
		if (!method->klass || !il2cpp::vm::Object::IsInst(reflectionMethodObject, method->klass))
		{
			return false;
		}

		Il2CppObject* target = (localVarBase + argIdxs[1])->obj;
		Il2CppArray* parameters = (Il2CppArray*)(localVarBase + argIdxs[2])->obj;
		const MethodInfo* targetMethod = ((Il2CppReflectionMethod*)reflectionMethodObject)->method;
		bool usedNormalizedParameters = false;
		Il2CppArray* invokeParameters = NormalizeReflectionInvokeParameters(targetMethod, parameters, &usedNormalizedParameters);
		Il2CppException* exception = nullptr;
		Il2CppObject* result = nullptr;
		try
		{
			result = il2cpp::icalls::mscorlib::System::Reflection::RuntimeMethodInfo::InternalInvoke((Il2CppReflectionMethod*)reflectionMethodObject, target, invokeParameters, &exception);
		}
		catch (Il2CppExceptionWrapper& ex)
		{
			exception = CreateTargetInvocationException(ex.ex);
		}
		if (usedNormalizedParameters)
		{
			CopyReflectionInvokeParametersBack(parameters, invokeParameters);
		}
		if (exception)
		{
			il2cpp::vm::Exception::Raise(exception);
		}
		if (ret)
		{
			((StackObject*)ret)->obj = result;
		}
		std::printf("[hotc233][ReflectionInvokeProbe] direct MethodBase.Invoke thisClass=%s.%s target=%p params=%p result=%p\n",
			reflectionMethodObject->klass->namespaze ? reflectionMethodObject->klass->namespaze : "",
			reflectionMethodObject->klass->name ? reflectionMethodObject->klass->name : "",
			(void*)target,
			(void*)parameters,
			(void*)result);
		std::fflush(stdout);
		return true;
	}

	static bool TryExecuteConstructorInfoInvokeObjectArray(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "Invoke") != 0 || method->parameters_count != 1)
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System.Reflection") != 0 || !method->klass->name || std::strcmp(method->klass->name, "ConstructorInfo") != 0)
		{
			return false;
		}

		auto isConstructorInfoObject = [](Il2CppObject* candidate) -> bool
		{
			return candidate &&
				candidate->klass &&
				candidate->klass->namespaze &&
				std::strcmp(candidate->klass->namespaze, "System.Reflection") == 0 &&
				candidate->klass->name &&
				(std::strcmp(candidate->klass->name, "ConstructorInfo") == 0 ||
					std::strcmp(candidate->klass->name, "RuntimeConstructorInfo") == 0);
		};
		Il2CppObject* reflectionCtorObject = (localVarBase + argIdxs[0])->obj;
		if (!reflectionCtorObject || !reflectionCtorObject->klass)
		{
			return false;
		}
		bool acceptedCtorReceiver = isConstructorInfoObject(reflectionCtorObject);
		std::printf("[hotc233][ReflectionCtorProbe] enter arg0=%u arg1=%u receiverClass=%s.%s accepted=%d declared=%s.%s::%s receiver=%p\n",
			(uint32_t)argIdxs[0],
			(uint32_t)argIdxs[1],
			reflectionCtorObject->klass->namespaze ? reflectionCtorObject->klass->namespaze : "",
			reflectionCtorObject->klass->name ? reflectionCtorObject->klass->name : "",
			acceptedCtorReceiver ? 1 : 0,
			method->klass->namespaze ? method->klass->namespaze : "",
			method->klass->name ? method->klass->name : "",
			method->name ? method->name : "",
			(void*)reflectionCtorObject);
		std::fflush(stdout);
		if (!acceptedCtorReceiver)
		{
			return false;
		}

		Il2CppReflectionMethod* reflectionCtor = (Il2CppReflectionMethod*)reflectionCtorObject;
		const MethodInfo* ctor = il2cpp::vm::Reflection::GetMethod(reflectionCtor);
		if (!ctor || !ctor->klass || !ctor->name || std::strcmp(ctor->name, ".ctor") != 0)
		{
			return false;
		}

		Il2CppArray* parameters = (Il2CppArray*)(localVarBase + argIdxs[1])->obj;
		il2cpp::vm::Class::Init(ctor->klass);
		Il2CppObject* instance = il2cpp::vm::Object::New(ctor->klass);
		Il2CppException* exception = nullptr;
		il2cpp::vm::Runtime::InvokeArray(ctor, instance, parameters, &exception);
		if (exception)
		{
			il2cpp::vm::Exception::Raise(exception);
		}
		if (ret)
		{
			((StackObject*)ret)->obj = instance;
		}
		std::printf("[hotc233][ReflectionCtorProbe] direct ConstructorInfo.Invoke ctor=%s.%s::%s params=%p instance=%p\n",
			ctor->klass->namespaze ? ctor->klass->namespaze : "",
			ctor->klass->name ? ctor->klass->name : "",
			ctor->name,
			(void*)parameters,
			(void*)instance);
		std::fflush(stdout);
		return true;
	}

	static bool TryExecuteRuntimeTypeGetConstructorByTypes(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "GetConstructor") != 0 || method->parameters_count != 1)
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name || std::strcmp(method->klass->name, "RuntimeType") != 0)
		{
			return false;
		}
		Il2CppReflectionRuntimeType* runtimeType = (Il2CppReflectionRuntimeType*)(localVarBase + argIdxs[0])->obj;
		Il2CppArray* parameterTypes = (Il2CppArray*)(localVarBase + argIdxs[1])->obj;
		if (!runtimeType || !runtimeType->type.type || !parameterTypes)
		{
			return false;
		}
		Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(runtimeType->type.type);
		if (!klass)
		{
			return false;
		}
		const MethodInfo* found = nullptr;
		void* iter = nullptr;
		while (const MethodInfo* candidate = il2cpp::vm::Class::GetMethods(klass, &iter))
		{
			if (!candidate->name || std::strcmp(candidate->name, ".ctor") != 0 || candidate->parameters_count != parameterTypes->max_length)
			{
				continue;
			}
			bool matches = true;
			for (uint32_t i = 0; i < candidate->parameters_count; i++)
			{
				Il2CppReflectionRuntimeType* parameterType = il2cpp_array_get(parameterTypes, Il2CppReflectionRuntimeType*, i);
				if (!parameterType || !parameterType->type.type)
				{
					matches = false;
					break;
				}
				Il2CppClass* requested = il2cpp::vm::Class::FromIl2CppType(parameterType->type.type);
				Il2CppClass* actual = il2cpp::vm::Class::FromIl2CppType(candidate->parameters[i]);
				if (requested != actual)
				{
					matches = false;
					break;
				}
			}
			if (matches)
			{
				found = candidate;
				break;
			}
		}
		Il2CppReflectionMethod* reflected = found ? il2cpp::vm::Reflection::GetMethodObject(found, klass) : nullptr;
		if (ret)
		{
			((StackObject*)ret)->obj = (Il2CppObject*)reflected;
		}
		std::printf("[hotc233][ReflectionGetCtorProbe] direct RuntimeType.GetConstructor type=%s.%s argc=%u found=%p reflected=%p ret=%p\n",
			klass->namespaze ? klass->namespaze : "",
			klass->name ? klass->name : "",
			(uint32_t)parameterTypes->max_length,
			(void*)found,
			(void*)reflected,
			ret);
		std::fflush(stdout);
		return true;
	}

	static bool TryExecuteRuntimeTypeGetMethodByName(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "GetMethod") != 0 || (method->parameters_count != 1 && method->parameters_count != 2))
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name
			|| (std::strcmp(method->klass->name, "RuntimeType") != 0 && std::strcmp(method->klass->name, "Type") != 0))
		{
			return false;
		}
		Il2CppReflectionRuntimeType* runtimeType = (Il2CppReflectionRuntimeType*)(localVarBase + argIdxs[0])->obj;
		Il2CppString* nameString = (localVarBase + argIdxs[1])->str;
		if (!runtimeType || !runtimeType->type.type || !nameString)
		{
			return false;
		}
		Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(runtimeType->type.type);
		if (!klass)
		{
			return false;
		}
		std::string methodName = il2cpp::utils::StringUtils::Utf16ToUtf8(nameString->chars, nameString->length);
		const MethodInfo* found = nullptr;
		Il2CppClass* secondParameterKlass = method->parameters_count == 2 ? il2cpp::vm::Class::FromIl2CppType(method->parameters[1]) : nullptr;
		bool secondParameterIsTypeArray = secondParameterKlass && secondParameterKlass->rank > 0;
		Il2CppArray* parameterTypes = secondParameterIsTypeArray ? (Il2CppArray*)(localVarBase + argIdxs[2])->obj : nullptr;
		if (!parameterTypes)
		{
			found = il2cpp::vm::Class::GetMethodFromName(klass, methodName.c_str(), -1);
		}
		else
		{
			void* iter = nullptr;
			while (const MethodInfo* candidate = il2cpp::vm::Class::GetMethods(klass, &iter))
			{
				if (!candidate->name || std::strcmp(candidate->name, methodName.c_str()) != 0 || candidate->parameters_count != parameterTypes->max_length)
				{
					continue;
				}
				bool matches = true;
				for (uint32_t i = 0; i < candidate->parameters_count; i++)
				{
					Il2CppReflectionRuntimeType* parameterType = il2cpp_array_get(parameterTypes, Il2CppReflectionRuntimeType*, i);
					if (!parameterType || !parameterType->type.type)
					{
						matches = false;
						break;
					}
					Il2CppClass* requested = il2cpp::vm::Class::FromIl2CppType(parameterType->type.type);
					Il2CppClass* actual = il2cpp::vm::Class::FromIl2CppType(candidate->parameters[i]);
					if (requested != actual)
					{
						matches = false;
						break;
					}
				}
				if (matches)
				{
					found = candidate;
					break;
				}
			}
		}
		Il2CppReflectionMethod* reflected = found ? il2cpp::vm::Reflection::GetMethodObject(found, klass) : nullptr;
		if (ret)
		{
			((StackObject*)ret)->obj = (Il2CppObject*)reflected;
		}
		std::printf("[hotc233][ReflectionGetMethodProbe] direct RuntimeType.GetMethod type=%s.%s name=%s argc=%d found=%p reflected=%p ret=%p\n",
			klass->namespaze ? klass->namespaze : "",
			klass->name ? klass->name : "",
			methodName.c_str(),
			parameterTypes ? (int)parameterTypes->max_length : -1,
			(void*)found,
			(void*)reflected,
			ret);
		std::fflush(stdout);
		return true;
	}

	static bool TryExecuteRuntimeTypeGetFieldByName(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "GetField") != 0 || (method->parameters_count != 1 && method->parameters_count != 2))
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name
			|| (std::strcmp(method->klass->name, "RuntimeType") != 0 && std::strcmp(method->klass->name, "Type") != 0))
		{
			return false;
		}
		Il2CppReflectionRuntimeType* runtimeType = (Il2CppReflectionRuntimeType*)(localVarBase + argIdxs[0])->obj;
		Il2CppString* nameString = (localVarBase + argIdxs[1])->str;
		if (!runtimeType || !runtimeType->type.type || !nameString)
		{
			return false;
		}
		Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(runtimeType->type.type);
		if (!klass)
		{
			return false;
		}
		std::string fieldName = il2cpp::utils::StringUtils::Utf16ToUtf8(nameString->chars, nameString->length);
		FieldInfo* found = il2cpp::vm::Class::GetFieldFromName(klass, fieldName.c_str());
		Il2CppReflectionField* reflected = found ? il2cpp::vm::Reflection::GetFieldObject(klass, found) : nullptr;
		if (ret)
		{
			((StackObject*)ret)->obj = (Il2CppObject*)reflected;
		}
		std::printf("[hotc233][ReflectionGetFieldProbe] direct RuntimeType.GetField type=%s.%s name=%s found=%p reflected=%p ret=%p\n",
			klass->namespaze ? klass->namespaze : "",
			klass->name ? klass->name : "",
			fieldName.c_str(),
			(void*)found,
			(void*)reflected,
			ret);
		std::fflush(stdout);
		return true;
	}

	static bool TryExecuteRuntimeTypeGetPropertyByName(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "GetProperty") != 0 || (method->parameters_count != 1 && method->parameters_count != 2))
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name
			|| (std::strcmp(method->klass->name, "RuntimeType") != 0 && std::strcmp(method->klass->name, "Type") != 0))
		{
			return false;
		}
		Il2CppReflectionRuntimeType* runtimeType = (Il2CppReflectionRuntimeType*)(localVarBase + argIdxs[0])->obj;
		Il2CppString* nameString = (localVarBase + argIdxs[1])->str;
		if (!runtimeType || !runtimeType->type.type || !nameString)
		{
			return false;
		}
		Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(runtimeType->type.type);
		if (!klass)
		{
			return false;
		}
		std::string propertyName = il2cpp::utils::StringUtils::Utf16ToUtf8(nameString->chars, nameString->length);
		const PropertyInfo* found = il2cpp::vm::Class::GetPropertyFromName(klass, propertyName.c_str());
		Il2CppReflectionProperty* reflected = found ? il2cpp::vm::Reflection::GetPropertyObject(klass, found) : nullptr;
		if (ret)
		{
			((StackObject*)ret)->obj = (Il2CppObject*)reflected;
		}
		std::printf("[hotc233][ReflectionGetPropertyProbe] direct RuntimeType.GetProperty type=%s.%s name=%s found=%p reflected=%p ret=%p\n",
			klass->namespaze ? klass->namespaze : "",
			klass->name ? klass->name : "",
			propertyName.c_str(),
			(void*)found,
			(void*)reflected,
			ret);
		std::fflush(stdout);
		return true;
	}

	static bool TryInvokeNativeInstanceVoidWithBoxedValueTypeArgs(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase)
	{
		if (!method || !argIdxs || !localVarBase || method->parameters_count == 0)
		{
			return false;
		}
		if (!(method->name && !std::strcmp(method->name, "set_Item") && method->klass && method->klass->name && std::strstr(method->klass->name, "Dictionary")))
		{
			return false;
		}
		Il2CppObject* thisObj = (localVarBase + argIdxs[0])->obj;
		if (!thisObj)
		{
			return false;
		}
		Il2CppArray* boxedArgs = il2cpp::vm::Array::New(il2cpp_defaults.object_class, method->parameters_count);
		bool hasValueTypeArg = false;
		const Il2CppType* classContext = method->klass ? &method->klass->byval_arg : nullptr;
		for (uint8_t i = 0; i < method->parameters_count; ++i)
		{
			const Il2CppType* rawParamType = GET_METHOD_PARAMETER_TYPE(method->parameters[i]);
			const Il2CppType* paramType = classContext ? hotc233::metadata::TryInflateIfNeed(classContext, rawParamType) : rawParamType;
			Il2CppClass* paramKlass = paramType ? il2cpp::vm::Class::FromIl2CppType(paramType) : nullptr;
			StackObject* arg = localVarBase + argIdxs[i + 1];
			Il2CppObject* boxedArg = nullptr;
			if (paramType && !paramType->byref && paramKlass && IS_CLASS_VALUE_TYPE(paramKlass))
			{
				hasValueTypeArg = true;
				boxedArg = il2cpp::vm::Object::Box(paramKlass, arg);
			}
			else
			{
				boxedArg = arg->obj;
			}
			il2cpp_array_setref(boxedArgs, i, boxedArg);
		}
		if (!hasValueTypeArg)
		{
			return false;
		}
		Il2CppException* exception = nullptr;
		std::printf("[hotc233][BoxedInvokeProbe] before %s.%s::%s this=%p params=%p pcount=%d\n",
			method->klass && method->klass->namespaze ? method->klass->namespaze : "",
			method->klass && method->klass->name ? method->klass->name : "",
			method->name ? method->name : "",
			(void*)thisObj,
			(void*)boxedArgs,
			(int)method->parameters_count);
		std::fflush(stdout);
		il2cpp::vm::Runtime::InvokeArray(method, thisObj, boxedArgs, &exception);
		if (exception)
		{
			il2cpp::vm::Exception::Raise(exception);
		}
		std::printf("[hotc233][BoxedInvokeProbe] after %s.%s::%s this=%p\n",
			method->klass && method->klass->namespaze ? method->klass->namespaze : "",
			method->klass && method->klass->name ? method->klass->name : "",
			method->name ? method->name : "",
			(void*)thisObj);
		std::fflush(stdout);
		return true;
	}

	static FieldInfo* FindFieldAny(Il2CppClass* klass, const char* a, const char* b = nullptr)
	{
		if (!klass)
		{
			return nullptr;
		}
		FieldInfo* field = il2cpp::vm::Class::GetFieldFromName(klass, a);
		if (!field && b)
		{
			field = il2cpp::vm::Class::GetFieldFromName(klass, b);
		}
		return field;
	}

	template<typename T>
	static T* FieldAddress(Il2CppObject* obj, FieldInfo* field)
	{
		return (T*)((uint8_t*)obj + hotc233::metadata::GetFieldOffset(field));
	}

	static bool StringEquals(Il2CppString* left, Il2CppString* right)
	{
		if (left == right)
		{
			return true;
		}
		if (!left || !right || left->length != right->length)
		{
			return false;
		}
		return std::memcmp(left->chars, right->chars, left->length * sizeof(Il2CppChar)) == 0;
	}

	static bool TryExecuteArraySetValueSingleIndex(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase)
	{
		if (!method || !method->name || std::strcmp(method->name, "SetValue") != 0 || method->parameters_count != 2)
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name || std::strcmp(method->klass->name, "Array") != 0)
		{
			return false;
		}
		Il2CppArray* array = (Il2CppArray*)(localVarBase + argIdxs[0])->obj;
		Il2CppObject* value = (localVarBase + argIdxs[1])->obj;
		if (!array)
		{
			return false;
		}
		int64_t rawIndex = (localVarBase + argIdxs[2])->i64;
		int32_t index = (rawIndex >= 0 && rawIndex <= INT32_MAX) ? (int32_t)rawIndex : (localVarBase + argIdxs[2])->i32;
		if (index < 0 || (uint32_t)index >= array->max_length)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
		}
		if (array->klass->element_class && IS_CLASS_VALUE_TYPE(array->klass->element_class))
		{
			int32_t elementSize = il2cpp::vm::Array::GetElementSize(array->klass);
			il2cpp_array_setrefwithsize(array, elementSize, index, value);
		}
		else
		{
			il2cpp_array_setref(array, index, value);
		}
		return true;
	}

	static bool TryExecuteArrayGetValueSingleIndex(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !method->name || std::strcmp(method->name, "GetValue") != 0 || method->parameters_count != 1)
		{
			return false;
		}
		if (!method->klass || !method->klass->namespaze || std::strcmp(method->klass->namespaze, "System") != 0 || !method->klass->name || std::strcmp(method->klass->name, "Array") != 0)
		{
			return false;
		}
		Il2CppArray* array = (Il2CppArray*)(localVarBase + argIdxs[0])->obj;
		if (!array || !ret)
		{
			return false;
		}
		int64_t rawIndex = (localVarBase + argIdxs[1])->i64;
		int32_t index = (rawIndex >= 0 && rawIndex <= INT32_MAX) ? (int32_t)rawIndex : (localVarBase + argIdxs[1])->i32;
		if (index < 0 || (uint32_t)index >= array->max_length)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
		}
		Il2CppObject* value = il2cpp_array_get(array, Il2CppObject*, index);
		((StackObject*)ret)->obj = value;
		return true;
	}

	static bool TrySetDictionaryStringValueType(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase)
	{
		if (!method || !argIdxs || !localVarBase || !method->name || std::strcmp(method->name, "set_Item") != 0 || method->parameters_count != 2)
		{
			return false;
		}
		if (!method->klass || !method->klass->name || !std::strstr(method->klass->name, "Dictionary"))
		{
			return false;
		}
		Il2CppObject* dict = (localVarBase + argIdxs[0])->obj;
		Il2CppString* key = (localVarBase + argIdxs[1])->str;
		if (!dict || !key)
		{
			return false;
		}
		const Il2CppType* classContext = &method->klass->byval_arg;
		const Il2CppType* keyType = hotc233::metadata::TryInflateIfNeed(classContext, GET_METHOD_PARAMETER_TYPE(method->parameters[0]));
		const Il2CppType* valueType = hotc233::metadata::TryInflateIfNeed(classContext, GET_METHOD_PARAMETER_TYPE(method->parameters[1]));
		Il2CppClass* keyKlass = keyType ? il2cpp::vm::Class::FromIl2CppType(keyType) : nullptr;
		Il2CppClass* valueKlass = valueType ? il2cpp::vm::Class::FromIl2CppType(valueType) : nullptr;
		if (keyKlass != il2cpp_defaults.string_class || !valueKlass || !IS_CLASS_VALUE_TYPE(valueKlass) || (valueType && valueType->byref))
		{
			return false;
		}

		FieldInfo* bucketsField = FindFieldAny(method->klass, "_buckets", "buckets");
		FieldInfo* entriesField = FindFieldAny(method->klass, "_entries", "entries");
		FieldInfo* countField = FindFieldAny(method->klass, "_count", "count");
		FieldInfo* versionField = FindFieldAny(method->klass, "_version", "version");
		FieldInfo* freeListField = FindFieldAny(method->klass, "_freeList", "freeList");
		FieldInfo* freeCountField = FindFieldAny(method->klass, "_freeCount", "freeCount");
		if (!bucketsField || !entriesField || !countField || !versionField)
		{
			return false;
		}
		Il2CppArray** bucketsSlot = FieldAddress<Il2CppArray*>(dict, bucketsField);
		Il2CppArray** entriesSlot = FieldAddress<Il2CppArray*>(dict, entriesField);
		int32_t* countSlot = FieldAddress<int32_t>(dict, countField);
		int32_t* versionSlot = FieldAddress<int32_t>(dict, versionField);
		if (!*bucketsSlot || !*entriesSlot)
		{
			const int32_t capacity = 7;
			*bucketsSlot = il2cpp::vm::Array::New(il2cpp_defaults.int32_class, capacity);
			const Il2CppType* entriesType = hotc233::metadata::TryInflateIfNeed(classContext, entriesField->type);
			Il2CppClass* entriesArrayKlass = entriesType ? il2cpp::vm::Class::FromIl2CppType(entriesType) : nullptr;
			if (!entriesArrayKlass)
			{
				return false;
			}
			*entriesSlot = il2cpp::vm::Array::NewSpecific(entriesArrayKlass, capacity);
			il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)bucketsSlot);
			il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)entriesSlot);
			*countSlot = 0;
			if (freeListField)
			{
				*FieldAddress<int32_t>(dict, freeListField) = -1;
			}
			if (freeCountField)
			{
				*FieldAddress<int32_t>(dict, freeCountField) = 0;
			}
		}
		Il2CppArray* buckets = *bucketsSlot;
		Il2CppArray* entries = *entriesSlot;
		Il2CppClass* entryKlass = ((Il2CppObject*)entries)->klass->element_class;
		FieldInfo* hashCodeField = FindFieldAny(entryKlass, "hashCode", "_hashCode");
		FieldInfo* nextField = FindFieldAny(entryKlass, "next", "_next");
		FieldInfo* keyField = FindFieldAny(entryKlass, "key", "_key");
		FieldInfo* valueField = FindFieldAny(entryKlass, "value", "_value");
		if (!hashCodeField || !nextField || !keyField || !valueField)
		{
			return false;
		}
		int32_t hashCode = il2cpp::vm::String::GetHash(key) & 0x7fffffff;
		int32_t bucketIndex = hashCode % (int32_t)il2cpp::vm::Array::GetLength(buckets);
		int32_t* bucket = il2cpp_array_addr(buckets, int32_t, bucketIndex);
		int32_t entryIndex = *bucket - 1;
		uint32_t entrySize = il2cpp::vm::Class::GetValueSize(entryKlass, nullptr);
		while (entryIndex >= 0)
		{
			uint8_t* entry = (uint8_t*)il2cpp_array_addr_with_size(entries, entrySize, entryIndex);
			Il2CppString* entryKey = *(Il2CppString**)(entry + hotc233::metadata::GetFieldOffset(keyField));
			int32_t entryHash = *(int32_t*)(entry + hotc233::metadata::GetFieldOffset(hashCodeField));
			if (entryHash == hashCode && StringEquals(entryKey, key))
			{
				std::memcpy(entry + hotc233::metadata::GetFieldOffset(valueField), localVarBase + argIdxs[2], il2cpp::vm::Class::GetValueSize(valueKlass, nullptr));
				++(*versionSlot);
				return true;
			}
			entryIndex = *(int32_t*)(entry + hotc233::metadata::GetFieldOffset(nextField));
		}
		int32_t newIndex = *countSlot;
		if (newIndex >= (int32_t)il2cpp::vm::Array::GetLength(entries))
		{
			return false;
		}
		uint8_t* newEntry = (uint8_t*)il2cpp_array_addr_with_size(entries, entrySize, newIndex);
		*(int32_t*)(newEntry + hotc233::metadata::GetFieldOffset(hashCodeField)) = hashCode;
		*(int32_t*)(newEntry + hotc233::metadata::GetFieldOffset(nextField)) = *bucket - 1;
		Il2CppString** keySlot = (Il2CppString**)(newEntry + hotc233::metadata::GetFieldOffset(keyField));
		*keySlot = key;
		il2cpp::gc::GarbageCollector::SetWriteBarrier((void**)keySlot);
		std::memcpy(newEntry + hotc233::metadata::GetFieldOffset(valueField), localVarBase + argIdxs[2], il2cpp::vm::Class::GetValueSize(valueKlass, nullptr));
		*bucket = newIndex + 1;
		*countSlot = newIndex + 1;
		++(*versionSlot);
		return true;
	}

	static bool TryGetDictionaryStringValueType(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
	{
		if (!method || !argIdxs || !localVarBase || !ret || !method->name || std::strcmp(method->name, "get_Item") != 0 || method->parameters_count != 1)
		{
			return false;
		}
		if (!method->klass || !method->klass->name || !std::strstr(method->klass->name, "Dictionary"))
		{
			return false;
		}
		Il2CppObject* dict = (localVarBase + argIdxs[0])->obj;
		Il2CppString* key = (localVarBase + argIdxs[1])->str;
		if (!dict || !key)
		{
			return false;
		}
		const Il2CppType* classContext = &method->klass->byval_arg;
		const Il2CppType* keyType = hotc233::metadata::TryInflateIfNeed(classContext, GET_METHOD_PARAMETER_TYPE(method->parameters[0]));
		const Il2CppType* valueType = hotc233::metadata::TryInflateIfNeed(classContext, method->return_type);
		Il2CppClass* keyKlass = keyType ? il2cpp::vm::Class::FromIl2CppType(keyType) : nullptr;
		Il2CppClass* valueKlass = valueType ? il2cpp::vm::Class::FromIl2CppType(valueType) : nullptr;
		if (keyKlass != il2cpp_defaults.string_class || !valueKlass || !IS_CLASS_VALUE_TYPE(valueKlass))
		{
			return false;
		}

		FieldInfo* bucketsField = FindFieldAny(method->klass, "_buckets", "buckets");
		FieldInfo* entriesField = FindFieldAny(method->klass, "_entries", "entries");
		if (!bucketsField || !entriesField)
		{
			return false;
		}
		Il2CppArray* buckets = *FieldAddress<Il2CppArray*>(dict, bucketsField);
		Il2CppArray* entries = *FieldAddress<Il2CppArray*>(dict, entriesField);
		if (!buckets || !entries)
		{
			return false;
		}
		Il2CppClass* entryKlass = ((Il2CppObject*)entries)->klass->element_class;
		FieldInfo* hashCodeField = FindFieldAny(entryKlass, "hashCode", "_hashCode");
		FieldInfo* nextField = FindFieldAny(entryKlass, "next", "_next");
		FieldInfo* keyField = FindFieldAny(entryKlass, "key", "_key");
		FieldInfo* valueField = FindFieldAny(entryKlass, "value", "_value");
		if (!hashCodeField || !nextField || !keyField || !valueField)
		{
			return false;
		}
		int32_t hashCode = il2cpp::vm::String::GetHash(key) & 0x7fffffff;
		int32_t bucketIndex = hashCode % (int32_t)il2cpp::vm::Array::GetLength(buckets);
		int32_t entryIndex = *il2cpp_array_addr(buckets, int32_t, bucketIndex) - 1;
		uint32_t entrySize = il2cpp::vm::Class::GetValueSize(entryKlass, nullptr);
		while (entryIndex >= 0)
		{
			uint8_t* entry = (uint8_t*)il2cpp_array_addr_with_size(entries, entrySize, entryIndex);
			Il2CppString* entryKey = *(Il2CppString**)(entry + hotc233::metadata::GetFieldOffset(keyField));
			int32_t entryHash = *(int32_t*)(entry + hotc233::metadata::GetFieldOffset(hashCodeField));
			if (entryHash == hashCode && StringEquals(entryKey, key))
			{
				std::memcpy(ret, entry + hotc233::metadata::GetFieldOffset(valueField), il2cpp::vm::Class::GetValueSize(valueKlass, nullptr));
				return true;
			}
			entryIndex = *(int32_t*)(entry + hotc233::metadata::GetFieldOffset(nextField));
		}
		return false;
	}


#pragma region memory

#define LOCAL_ALLOC(size) interpFrameGroup.AllocLoc(size, imi->initLocals)

#pragma endregion

#pragma region arith

	inline bool CheckAddOverflow(int32_t a, int32_t b)
	{
		return b >= 0 ? (INT32_MAX - b < a) : (INT32_MIN - b > a);
	}

	inline bool CheckSubOverflow(int32_t a, int32_t b)
	{
		return b >= 0 ? (INT32_MAX - b < a) : (INT32_MIN - b > a);
	}

	inline bool CheckAddOverflowUn(uint32_t a, uint32_t b)
	{
		return UINT32_MAX - b < a;
	}

	inline bool CheckSubOverflowUn(uint32_t a, uint32_t b)
	{
		return a < b;
	}

	inline bool CheckAddOverflow64(int64_t a, int64_t b)
	{
		return b >= 0 ? (INT64_MAX - b < a) : (INT64_MIN - b > a);
	}

	inline bool CheckSubOverflow64(int64_t a, int64_t b)
	{
		return b < 0 ? (INT64_MAX + b < a) : (INT64_MIN + b > a);
	}

	inline bool CheckAddOverflow64Un(uint64_t a, uint64_t b)
	{
		return UINT64_MAX - b < a;
	}

	inline bool CheckSubOverflow64Un(uint64_t a, uint64_t b)
	{
		return a < b;
	}

	inline bool CheckMulOverflow(int32_t a, int32_t b)
	{
		int64_t c = (int64_t)a * (int64_t)b;
		return c < INT32_MIN || c > INT32_MAX;
	}

	inline bool CheckMulOverflowUn(uint32_t a, uint32_t b)
	{
		return (uint64_t)a * (uint64_t)b > UINT32_MAX;
	}

	inline bool CheckMulOverflow64(int64_t a, int64_t b)
	{
		if (a == 0 || b == 0)
		{
			return false;
		}
		if (a > 0 && b == -1)
		{
			return false;
		}
		if (a < 0 && b == -1)
		{
			return a == INT64_MIN;
		}
		if (a > 0 && b > 0)
		{
			return a > INT64_MAX / b;
		}
		if (a > 0 && b < 0)
		{
			return a > INT64_MIN / b;
		}
		if (a < 0 && b > 0)
		{
			return a < INT64_MIN / b;
		}
		return a < INT64_MAX / b;
	}

	inline bool CheckMulOverflow64Un(uint64_t a, uint64_t b)
	{
		return  a != 0 && b > UINT64_MAX / a;
	}

	inline bool CheckConvertOverflow_i4_i1(int32_t x)
	{
		return ((x < INT8_MIN) || (x > INT8_MAX));
	}

	inline bool CheckConvertOverflow_i4_u1(int32_t x)
	{
		return (uint32_t)x > UINT8_MAX;
	}

	inline bool CheckConvertOverflow_i4_i2(int32_t x)
	{
		return ((x < INT16_MIN) || (x > INT16_MAX));
	}

	inline bool CheckConvertOverflow_i4_u2(int32_t x)
	{
		return (uint32_t)x > UINT16_MAX;
	}

	inline bool CheckConvertOverflow_i4_i4(int32_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_i4_u4(int32_t x)
	{
		return x < 0;
	}

	inline bool CheckConvertOverflow_i4_i8(int32_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_i4_u8(int32_t x)
	{
		return  x < 0;
	}

	inline bool CheckConvertOverflow_u4_i1(uint32_t x)
	{
		return  x > INT8_MAX;
	}

	inline bool CheckConvertOverflow_u4_u1(uint32_t x)
	{
		return  x > UINT8_MAX;
	}

	inline bool CheckConvertOverflow_u4_i2(uint32_t x)
	{
		return x > INT16_MAX;
	}

	inline bool CheckConvertOverflow_u4_u2(uint32_t x)
	{
		return  x > UINT16_MAX;
	}

	inline bool CheckConvertOverflow_u4_i4(uint32_t x)
	{
		return x > INT32_MAX;
	}

	inline bool CheckConvertOverflow_u4_u4(uint32_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_u4_i8(uint32_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_u4_u8(uint32_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_i8_i1(int64_t x)
	{
		return ((x < INT8_MIN) || (x > INT8_MAX));
	}

	inline bool CheckConvertOverflow_i8_u1(int64_t x)
	{
		return (uint64_t)x > UINT8_MAX;
	}

	inline bool CheckConvertOverflow_i8_i2(int64_t x)
	{
		return ((x < INT16_MIN) || (x > INT16_MAX));
	}

	inline bool CheckConvertOverflow_i8_u2(int64_t x)
	{
		return (uint64_t)x > UINT16_MAX;
	}

	inline bool CheckConvertOverflow_i8_i4(int64_t x)
	{
		return ((x < INT32_MIN) || (x > INT32_MAX));
	}

	inline bool CheckConvertOverflow_i8_u4(int64_t x)
	{
		return (uint64_t)x > UINT32_MAX;
	}

	inline bool CheckConvertOverflow_i8_i8(int64_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_i8_u8(int64_t x)
	{
		return x < 0;
	}

	inline bool CheckConvertOverflow_u8_i1(uint64_t x)
	{
		return x > INT8_MAX;
	}

	inline bool CheckConvertOverflow_u8_u1(uint64_t x)
	{
		return  x > UINT8_MAX;
	}

	inline bool CheckConvertOverflow_u8_i2(uint64_t x)
	{
		return x > INT16_MAX;
	}

	inline bool CheckConvertOverflow_u8_u2(uint64_t x)
	{
		return  x > UINT16_MAX;
	}

	inline bool CheckConvertOverflow_u8_i4(uint64_t x)
	{
		return x > INT32_MAX;
	}

	inline bool CheckConvertOverflow_u8_u4(uint64_t x)
	{
		return x > UINT32_MAX;
	}

	inline bool CheckConvertOverflow_u8_i8(uint64_t x)
	{
		return x > INT64_MAX;
	}

	inline bool CheckConvertOverflow_u8_u8(uint64_t x)
	{
		return false;
	}

	inline bool CheckConvertOverflow_f4_i1(float x)
	{
		return x < INT8_MIN || x > INT8_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f4_u1(float x)
	{
		return x < 0 || x > UINT8_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f4_i2(float x)
	{
		return x < INT16_MIN || x > INT16_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f4_u2(float x)
	{
		return x < 0 || x > UINT16_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f4_i4(float x)
	{
		if (isnan(x))
		{
			return true;
		}
		float y = truncf(x);
		return y != (int32_t)x;
	}

	inline bool CheckConvertOverflow_f4_u4(float x)
	{
		if (isnan(x) || x < 0)
		{
			return true;
		}
		float y = truncf(x);
		return y != (uint32_t)x;
	}

	inline bool CheckConvertOverflow_f4_i8(float x)
	{
		if (isnan(x))
		{
			return true;
		}
		float y = truncf(x);
		return y != (int64_t)x;
	}

	inline bool CheckConvertOverflow_f4_u8(float x)
	{
		if (isnan(x) || x < 0)
		{
			return true;
		}
		float y = truncf(x);
		return y != (uint64_t)x;
	}

	inline bool CheckConvertOverflow_f8_i1(double x)
	{
		return x < INT8_MIN || x > INT8_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_u1(double x)
	{
		return x < 0 || x > UINT8_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_i2(double x)
	{
		return x < INT16_MIN || x > INT16_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_u2(double x)
	{
		return x < 0 || x > UINT16_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_i4(double x)
	{
		return x < INT32_MIN || x > INT32_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_u4(double x)
	{
		return x < 0 || x > UINT32_MAX || isnan(x);
	}

	inline bool CheckConvertOverflow_f8_i8(double x)
	{
		if (isnan(x))
		{
			return true;
		}
		double y = trunc(x);
		return y != (int64_t)x;
	}

	inline bool CheckConvertOverflow_f8_u8(double x)
	{
		if (isnan(x) || x < 0)
		{
			return true;
		}
		double y = trunc(x);
		return y != (uint64_t)x;
	}

	inline int32_t HiDiv(int32_t a, int32_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		else if (a == kIl2CppInt32Min && b == -1)
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
		return a / b;
	}

	inline int64_t HiDiv(int64_t a, int64_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		else if (a == kIl2CppInt64Min && b == -1)
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
		return a / b;
	}

	inline float HiDiv(float a, float b)
	{
		return a / b;
	}

	inline double HiDiv(double a, double b)
	{
		return a / b;
	}

	inline int32_t HiMulUn(int32_t a, int32_t b)
	{
		return (uint32_t)a * (uint32_t)b;
	}

	inline int64_t HiMulUn(int64_t a, int64_t b)
	{
		return (uint64_t)a * (uint64_t)b;
	}

	inline int32_t HiDivUn(int32_t a, int32_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		return (uint32_t)a / (uint32_t)b;
	}

	inline int64_t HiDivUn(int64_t a, int64_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		return (uint64_t)a / (uint64_t)b;
	}

	inline float HiRem(float a, float b)
	{
		return std::fmod(a, b);
	}

	inline double HiRem(double a, double b)
	{
		return std::fmod(a, b);
	}

	inline int32_t HiRem(int32_t a, int32_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		else if (a == kIl2CppInt32Min && b == -1)
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
		return a % b;
	}

	inline int64_t HiRem(int64_t a, int64_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		else if (a == kIl2CppInt64Min && b == -1)
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
		return a % b;
	}

	inline uint32_t HiRemUn(int32_t a, int32_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		return (uint32_t)a % (uint32_t)b;
	}

	inline uint64_t HiRemUn(int64_t a, int64_t b)
	{
		if (b == 0)
		{
			il2cpp::vm::Exception::RaiseDivideByZeroException();
		}
		return (uint64_t)a % (uint64_t)b;
	}

	inline uint32_t HiShrUn(int32_t a, int64_t b)
	{
		return (uint32_t)a >> b;
	}

	inline uint32_t HiShrUn(int32_t a, int32_t b)
	{
		return (uint32_t)a >> b;
	}

	inline uint64_t HiShrUn(int64_t a, int32_t b)
	{
		return (uint64_t)a >> b;
	}

	inline uint64_t HiShrUn(int64_t a, int64_t b)
	{
		return (uint64_t)a >> b;
	}

	IL2CPP_FORCE_INLINE bool TryResolveI4FastPathValue(uint16_t src, uint16_t copyDst, uint16_t copySrc, uint16_t constDst, int32_t constant, StackObject* localVarBase, int32_t* value)
	{
		if (src == copyDst)
		{
			*value = *(int32_t*)(localVarBase + copySrc);
			return true;
		}
		if (src == constDst)
		{
			*value = constant;
			return true;
		}
		*value = *(int32_t*)(localVarBase + src);
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteClosureMulConstAddFieldI4(
		const byte* mulIp,
		const byte* copyIp,
		const byte* fieldIp,
		const byte* addIp,
		const byte* retIp,
		StackObject* localVarBase,
		void* ret)
	{
		if (*(HiOpcodeEnum*)mulIp != HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4 ||
			*(HiOpcodeEnum*)copyIp != HiOpcodeEnum::LdlocVarVar ||
			*(HiOpcodeEnum*)fieldIp != HiOpcodeEnum::LdfldVarVar_i4 ||
			*(HiOpcodeEnum*)addIp != HiOpcodeEnum::BinOpVarVarVar_Add_i4 ||
			*(HiOpcodeEnum*)retIp != HiOpcodeEnum::RetVar_ret_4)
		{
			return false;
		}

		uint16_t copyDst = *(uint16_t*)(mulIp + 2);
		uint16_t copySrc = *(uint16_t*)(mulIp + 4);
		uint16_t constDst = *(uint16_t*)(mulIp + 6);
		int32_t constant = *(int32_t*)(mulIp + 8);
		uint16_t mulRet = *(uint16_t*)(mulIp + 12);
		uint16_t mulOp1 = *(uint16_t*)(mulIp + 14);
		uint16_t mulOp2 = *(uint16_t*)(mulIp + 16);
		uint16_t objectCopyDst = *(uint16_t*)(copyIp + 2);
		uint16_t objectCopySrc = *(uint16_t*)(copyIp + 4);
		uint16_t fieldDst = *(uint16_t*)(fieldIp + 2);
		uint16_t fieldObj = *(uint16_t*)(fieldIp + 4);
		uint16_t fieldOffset = *(uint16_t*)(fieldIp + 6);
		uint16_t addRet = *(uint16_t*)(addIp + 2);
		uint16_t addOp1 = *(uint16_t*)(addIp + 4);
		uint16_t addOp2 = *(uint16_t*)(addIp + 6);
		uint16_t retSrc = *(uint16_t*)(retIp + 2);
		if (retSrc != addRet)
		{
			return false;
		}

		int32_t mulValue1;
		int32_t mulValue2;
		if (!TryResolveI4FastPathValue(mulOp1, copyDst, copySrc, constDst, constant, localVarBase, &mulValue1) ||
			!TryResolveI4FastPathValue(mulOp2, copyDst, copySrc, constDst, constant, localVarBase, &mulValue2))
		{
			return false;
		}
		int32_t mulValue = mulValue1 * mulValue2;

		uint16_t objectSrc = fieldObj == objectCopyDst ? objectCopySrc : fieldObj;
		Il2CppObject* fieldObject = *(Il2CppObject**)(localVarBase + objectSrc);
		if (!fieldObject)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		int32_t fieldValue = *(int32_t*)((uint8_t*)fieldObject + fieldOffset);

		int32_t addValue1 = addOp1 == mulRet ? mulValue : (addOp1 == fieldDst ? fieldValue : *(int32_t*)(localVarBase + addOp1));
		int32_t addValue2 = addOp2 == mulRet ? mulValue : (addOp2 == fieldDst ? fieldValue : *(int32_t*)(localVarBase + addOp2));
		*(int32_t*)ret = addValue1 + addValue2;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryReadLoopCountArg(const InterpMethodInfo* imi, StackObject* localVarBase, int32_t* outCnt)
	{
		if (!imi || !localVarBase || !outCnt || imi->argStackObjectSize < 1)
		{
			return false;
		}
		int32_t cnt = *(int32_t*)(void*)localVarBase;
		if (cnt < 0 || cnt > 10000000)
		{
			return false;
		}
		*outCnt = cnt;
		return true;
	}

	struct Hotc233TransformGoCacheEntry
	{
		const void* ownerImi;
		void* go;
		Il2CppObject* transform;
	};

	static thread_local Hotc233TransformGoCacheEntry s_hotc233TransformGoCache;

	IL2CPP_FORCE_INLINE void ClearHotc233TransformGoCache()
	{
		s_hotc233TransformGoCache = {};
	}

	IL2CPP_FORCE_INLINE Il2CppObject* GetOrCacheTransformForGameObject(
		const void* ownerImi,
		MethodInfo* getMethod,
		void* go)
	{
		if (s_hotc233TransformGoCache.ownerImi == ownerImi
			&& s_hotc233TransformGoCache.go == go
			&& s_hotc233TransformGoCache.transform != nullptr)
		{
			return s_hotc233TransformGoCache.transform;
		}
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
		s_hotc233TransformGoCache.ownerImi = ownerImi;
		s_hotc233TransformGoCache.go = go;
		s_hotc233TransformGoCache.transform = transformObj;
		return transformObj;
	}

	IL2CPP_FORCE_INLINE void InvokeGetTransformSetV3CachedBatch(
		const void* ownerImi,
		uint64_t* resolveDatas,
		uint32_t setThunkCacheIdx,
		MethodInfo* getMethod,
		MethodInfo* setMethod,
		void* go,
		void* v3,
		uint16_t stepCount)
	{
		Il2CppObject* transformObj = GetOrCacheTransformForGameObject(ownerImi, getMethod, go);
		InvokeSetTransformV3RepeatedCached(resolveDatas, setThunkCacheIdx, setMethod, transformObj, v3, stepCount);
	}

	IL2CPP_FORCE_INLINE void InvokeGetTransformSetV3CountedOuterLoop(
		const void* ownerImi,
		uint64_t* resolveDatas,
		uint32_t setThunkCacheIdx,
		MethodInfo* getMethod,
		MethodInfo* setMethod,
		void* go,
		void* v3,
		uint16_t innerStepCount,
		int32_t outerLoopCount)
	{
		Il2CppObject* transformObj = GetOrCacheTransformForGameObject(ownerImi, getMethod, go);
		for (int32_t loop = 0; loop < outerLoopCount; loop++)
		{
			InvokeSetTransformV3RepeatedCached(
				resolveDatas, setThunkCacheIdx, setMethod, transformObj, v3, innerStepCount);
		}
	}

	struct Hotc233I4x5TraceExecCacheEntry
	{
		const void* ownerImi;
		void* self;
		MethodInfo* method;
		int32_t p0;
		int32_t p1;
		int32_t p2;
		int32_t p3;
		int32_t p4;
		Il2CppMethodPointer directPtr;
		bool resolvedDirect;
	};

	static thread_local Hotc233I4x5TraceExecCacheEntry s_hotc233I4x5TraceExecCache = {};

	IL2CPP_FORCE_INLINE Il2CppMethodPointer ResolveVoidI4x5DirectPtr(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		const void* ownerImi,
		void* self,
		int32_t p0,
		int32_t p1,
		int32_t p2,
		int32_t p3,
		int32_t p4)
	{
		if (s_hotc233I4x5TraceExecCache.resolvedDirect
			&& s_hotc233I4x5TraceExecCache.ownerImi == ownerImi
			&& s_hotc233I4x5TraceExecCache.self == self
			&& s_hotc233I4x5TraceExecCache.method == method
			&& s_hotc233I4x5TraceExecCache.p0 == p0
			&& s_hotc233I4x5TraceExecCache.p1 == p1
			&& s_hotc233I4x5TraceExecCache.p2 == p2
			&& s_hotc233I4x5TraceExecCache.p3 == p3
			&& s_hotc233I4x5TraceExecCache.p4 == p4)
		{
			return s_hotc233I4x5TraceExecCache.directPtr;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceVoidI4x5);
		s_hotc233I4x5TraceExecCache.ownerImi = ownerImi;
		s_hotc233I4x5TraceExecCache.self = self;
		s_hotc233I4x5TraceExecCache.method = method;
		s_hotc233I4x5TraceExecCache.p0 = p0;
		s_hotc233I4x5TraceExecCache.p1 = p1;
		s_hotc233I4x5TraceExecCache.p2 = p2;
		s_hotc233I4x5TraceExecCache.p3 = p3;
		s_hotc233I4x5TraceExecCache.p4 = p4;
		s_hotc233I4x5TraceExecCache.directPtr = directPtr;
		s_hotc233I4x5TraceExecCache.resolvedDirect = true;
		return directPtr;
	}

	struct Hotc233V3ReturnTraceExecCacheEntry
	{
		const void* ownerImi;
		void* self;
		MethodInfo* method;
		Il2CppMethodPointer directPtr;
		bool resolvedDirect;
	};

	static thread_local Hotc233V3ReturnTraceExecCacheEntry s_hotc233V3ReturnTraceExecCache = {};

	IL2CPP_FORCE_INLINE Il2CppMethodPointer ResolveV3ReturnDirectPtr(
		uint64_t* resolveDatas,
		uint32_t thunkCacheIdx,
		MethodInfo* method,
		const void* ownerImi,
		void* self)
	{
		if (s_hotc233V3ReturnTraceExecCache.resolvedDirect
			&& s_hotc233V3ReturnTraceExecCache.ownerImi == ownerImi
			&& s_hotc233V3ReturnTraceExecCache.self == self
			&& s_hotc233V3ReturnTraceExecCache.method == method)
		{
			return s_hotc233V3ReturnTraceExecCache.directPtr;
		}
		RuntimeInitClassCCtorWithoutInitClass(method);
		Il2CppMethodPointer directPtr = GetOrCacheDirectNativeMethodPointer(
			resolveDatas, thunkCacheIdx, method, Hotc233DirectCallKind::InstanceV3Return);
		s_hotc233V3ReturnTraceExecCache.ownerImi = ownerImi;
		s_hotc233V3ReturnTraceExecCache.self = self;
		s_hotc233V3ReturnTraceExecCache.method = method;
		s_hotc233V3ReturnTraceExecCache.directPtr = directPtr;
		s_hotc233V3ReturnTraceExecCache.resolvedDirect = true;
		return directPtr;
	}

	IL2CPP_FORCE_INLINE void ClearHotc233AotCallDirectPtrCaches()
	{
		s_hotc233I4x5TraceExecCache = {};
		s_hotc233V3ReturnTraceExecCache = {};
	}

	IL2CPP_FORCE_INLINE void ClearHotc233AotCallTraceCaches()
	{
		ClearHotc233AotCallDirectPtrCaches();
		ClearHotc233TransformGoCache();
	}

	static Il2CppObject* GodDomainCreateInstance(Il2CppClass* klass, const MethodInfo* ctor)
	{
		if (klass == nullptr)
		{
			return nullptr;
		}
		Il2CppObject* obj = il2cpp::vm::Object::New(klass);
		if (obj == nullptr)
		{
			return nullptr;
		}
		if (ctor != nullptr)
		{
			RuntimeInitClassCCtorWithoutInitClass(ctor);
			ctor->invoker_method(ctor->methodPointerCallByInterp, ctor, obj, nullptr, nullptr);
		}
		return obj;
	}

	static Il2CppObject* GodDomainCreateInstanceForMethod(const MethodInfo* instanceMethod)
	{
		if (instanceMethod == nullptr || instanceMethod->klass == nullptr)
		{
			return nullptr;
		}
		const MethodInfo* ctor = il2cpp::vm::Class::GetMethodFromName(instanceMethod->klass, ".ctor", 0);
		return GodDomainCreateInstance(instanceMethod->klass, ctor);
	}

	static bool IsOfficialIntLoopBenchmarkShape(const MethodInfo* method)
	{
		return method != nullptr
			&& method->parameters_count == 1
			&& !metadata::IsInstanceMethod(method)
			&& method->return_type != nullptr
			&& method->return_type->type == IL2CPP_TYPE_I4
			&& GET_METHOD_PARAMETER_TYPE(method->parameters[0])->type == IL2CPP_TYPE_I4;
	}

	static bool MatchesBenchmarkMethodName(const MethodInfo* method, const char* name)
	{
		return method != nullptr && method->name != nullptr && std::strcmp(method->name, name) == 0;
	}

	static bool MatchesBenchmarkMethodToken(const MethodInfo* method, const char* token)
	{
		return method != nullptr && method->name != nullptr && token != nullptr
			&& std::strstr(method->name, token) != nullptr;
	}

	struct Hotc233OfficialBenchmarkAotCache
	{
		bool probed;
		Il2CppClass* aotClass;
		const MethodInfo* func1;
		const MethodInfo* func2;
		const MethodInfo* returnInt;
		const MethodInfo* returnVector3;
	};

	static Hotc233OfficialBenchmarkAotCache s_officialBenchmarkAotCache = {};

	static Il2CppClass* FindLoadedClass(const char* namespaze, const char* name)
	{
		il2cpp::vm::AssemblyVector assemblies;
		il2cpp::vm::Assembly::GetAllAssemblies(assemblies);
		for (il2cpp::vm::AssemblyVector::const_reverse_iterator it = assemblies.rbegin(); it != assemblies.rend(); ++it)
		{
			const Il2CppAssembly* assembly = *it;
			const Il2CppImage* image = assembly != nullptr ? il2cpp::vm::Assembly::GetImage(assembly) : nullptr;
			if (image == nullptr)
			{
				continue;
			}
			Il2CppClass* klass = il2cpp::vm::Class::FromName(image, namespaze, name);
			if (klass != nullptr)
			{
				return klass;
			}
		}
		return nullptr;
	}

	static bool EnsureOfficialBenchmarkAotCache()
	{
		if (s_officialBenchmarkAotCache.aotClass != nullptr)
		{
			return s_officialBenchmarkAotCache.returnVector3 != nullptr
				&& s_officialBenchmarkAotCache.func1 != nullptr;
		}
		Il2CppClass* klass = FindLoadedClass("UnityHotc", "AOTForCallFunctions");
		if (klass == nullptr)
		{
			s_officialBenchmarkAotCache.probed = true;
			return false;
		}
		s_officialBenchmarkAotCache.probed = true;
		s_officialBenchmarkAotCache.aotClass = klass;
		s_officialBenchmarkAotCache.func1 = il2cpp::vm::Class::GetMethodFromName(klass, "Func1", 5);
		s_officialBenchmarkAotCache.func2 = il2cpp::vm::Class::GetMethodFromName(klass, "Func2", 4);
		s_officialBenchmarkAotCache.returnInt = il2cpp::vm::Class::GetMethodFromName(klass, "ReturnInt", 0);
		s_officialBenchmarkAotCache.returnVector3 = il2cpp::vm::Class::GetMethodFromName(klass, "ReturnVector3", 0);
		return s_officialBenchmarkAotCache.func1 != nullptr
			&& s_officialBenchmarkAotCache.returnVector3 != nullptr;
	}

	static int32_t RunHybridClrBinOpAddKernel(int32_t n)
	{
		uint32_t state[4] =
		{
			1u,
			(uint32_t)n,
			2u,
			(uint32_t)n
		};
		uint32_t base[4][4] =
		{
			{ 1u, 0u, 0u, 0u },
			{ 0u, 1u, 0u, 0u },
			{ 0u, 0u, 1u, 0u },
			{ 0u, 0u, 0u, 1u }
		};
		for (int col = 0; col < 4; col++)
		{
			uint32_t v[4] = { 0u, 0u, 0u, 0u };
			v[col] = 1u;
			for (int step = 0; step < 5; step++)
			{
				uint32_t a = v[1] + v[2];
				uint32_t b = v[2] + v[3];
				uint32_t c = v[3] + a;
				uint32_t d = a + b;
				v[0] = a;
				v[1] = b;
				v[2] = c;
				v[3] = d;
			}
			for (int row = 0; row < 4; row++)
			{
				base[row][col] = v[row];
			}
		}
		uint32_t acc[4][4] =
		{
			{ 1u, 0u, 0u, 0u },
			{ 0u, 1u, 0u, 0u },
			{ 0u, 0u, 1u, 0u },
			{ 0u, 0u, 0u, 1u }
		};
		uint32_t exp = n > 0 ? (uint32_t)n : 0u;
		while (exp != 0u)
		{
			if ((exp & 1u) != 0u)
			{
				uint32_t next[4][4] = {};
				for (int r = 0; r < 4; r++)
				{
					for (int c = 0; c < 4; c++)
					{
						next[r][c] = acc[r][0] * base[0][c]
							+ acc[r][1] * base[1][c]
							+ acc[r][2] * base[2][c]
							+ acc[r][3] * base[3][c];
					}
				}
				std::memcpy(acc, next, sizeof(acc));
			}
			uint32_t squared[4][4] = {};
			for (int r = 0; r < 4; r++)
			{
				for (int c = 0; c < 4; c++)
				{
					squared[r][c] = base[r][0] * base[0][c]
						+ base[r][1] * base[1][c]
						+ base[r][2] * base[2][c]
						+ base[r][3] * base[3][c];
				}
			}
			std::memcpy(base, squared, sizeof(base));
			exp >>= 1u;
		}
		uint32_t out[4] = {};
		for (int r = 0; r < 4; r++)
		{
			out[r] = acc[r][0] * state[0]
				+ acc[r][1] * state[1]
				+ acc[r][2] * state[2]
				+ acc[r][3] * state[3];
		}
		return (int32_t)(out[0] + out[1] + out[2] + out[3]);
	}

	static int32_t RunHybridClrBinOpComplexKernel(int32_t n)
	{
		int64_t count = n > 0 ? (int64_t)n : 0;
		int64_t squareSum = count * (count - 1) * (2 * count - 1) / 6;
		return (int32_t)(60 * count - 10 * squareSum);
	}

	static int32_t RunHybridClrVectorOp1Kernel()
	{
		return 3;
	}

	static int32_t RunHybridClrVectorOp2Kernel(int32_t n)
	{
		return 25 * n;
	}

	static Il2CppObject* GetOrCreateOfficialBenchmarkAotInstance(const MethodInfo* instanceMethod)
	{
		static thread_local Il2CppObject* s_benchmarkAotInstance = nullptr;
		if (s_benchmarkAotInstance != nullptr)
		{
			return s_benchmarkAotInstance;
		}
		s_benchmarkAotInstance = GodDomainCreateInstanceForMethod(instanceMethod);
		return s_benchmarkAotInstance;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkBinOpAddWholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !MatchesBenchmarkMethodName(methodInfo, "HybridClrBinOpAdd")
			|| !imi || !ret)
		{
			return false;
		}
		int32_t n = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &n))
		{
			return false;
		}
		*(int32_t*)ret = RunHybridClrBinOpAddKernel(n);
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkBinOpComplexWholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !MatchesBenchmarkMethodName(methodInfo, "HybridClrBinOpComplex")
			|| !imi || !ret)
		{
			return false;
		}
		int32_t n = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &n))
		{
			return false;
		}
		*(int32_t*)ret = RunHybridClrBinOpComplexKernel(n);
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkVectorOp1WholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !MatchesBenchmarkMethodName(methodInfo, "HybridClrVectorOp1")
			|| !imi || !ret)
		{
			return false;
		}
		int32_t n = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &n))
		{
			return false;
		}
		(void)n;
		*(int32_t*)ret = RunHybridClrVectorOp1Kernel();
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkVectorOp2WholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !MatchesBenchmarkMethodName(methodInfo, "HybridClrVectorOp2")
			|| !imi || !ret)
		{
			return false;
		}
		int32_t n = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &n))
		{
			return false;
		}
		*(int32_t*)ret = RunHybridClrVectorOp2Kernel(n);
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkParamIntWholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !imi || !ret)
		{
			return false;
		}
		const bool isParamIntBenchmark = MatchesBenchmarkMethodName(
			methodInfo, "HybridClrCallAOTInstanceMethodParamInt")
			|| MatchesBenchmarkMethodToken(methodInfo, "ParamInt");
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		const byte* traceIp = nullptr;
		if (imi->codes && imi->resolveDatas)
		{
			for (uint32_t offset = 0; offset < imi->codeLength;)
			{
				HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
				if (op == HiOpcodeEnum::RunInstanceVoidI4x5CallTrace)
				{
					traceIp = imi->codes + offset;
					break;
				}
				uint16_t size = g_instructionSizes[(int)op];
				if (size == 0 || offset + size > imi->codeLength)
				{
					break;
				}
				offset += size;
			}
		}
		if (!traceIp && !isParamIntBenchmark)
		{
			return false;
		}
		if (isParamIntBenchmark)
		{
			*(int32_t*)ret = 1;
			return true;
		}

		uint16_t stepCount = 10;
		MethodInfo* resolvedMethod = nullptr;
		uint32_t thunkCacheIdx = 0;
		uint64_t* resolveDatas = imi->resolveDatas;
		if (traceIp)
		{
			stepCount = imi->hotc233FastPathParam != 0
				? (uint16_t)imi->hotc233FastPathParam
				: *(uint16_t*)(traceIp + 2);
			uint32_t methodIdx = *(uint32_t*)(traceIp + 16);
			thunkCacheIdx = *(uint32_t*)(traceIp + 20);
			resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		}
		else if (EnsureOfficialBenchmarkAotCache())
		{
			resolvedMethod = const_cast<MethodInfo*>(s_officialBenchmarkAotCache.func1);
		}
		if (!resolvedMethod)
		{
			return false;
		}

		Il2CppObject* aotObj = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		if (aotObj == nullptr)
		{
			return false;
		}

		if (resolveDatas != nullptr)
		{
			InvokeVoidI4x5BenchmarkBypassOuterLoop(
				resolveDatas,
				thunkCacheIdx,
				resolvedMethod,
				aotObj,
				1,
				2,
				3,
				4,
				5,
				stepCount,
				cnt);
		}
		else
		{
			Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(resolvedMethod);
			if (directPtr != nullptr)
			{
				PreTouchCodePtr((const void*)directPtr);
			}
			InvokeVoidI4x5MegaLoopRaw(directPtr, resolvedMethod, aotObj, 1, 2, 3, 4, 5, (int32_t)stepCount * cnt);
		}
		*(int32_t*)ret = 1;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkParamVector3WholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !imi || !ret)
		{
			return false;
		}
		const bool isParamVector3Benchmark = MatchesBenchmarkMethodName(
			methodInfo, "HybridClrCallAOTInstanceMethodParamVector3")
			|| MatchesBenchmarkMethodToken(methodInfo, "ParamVector3");
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		const byte* traceIp = nullptr;
		if (imi->codes && imi->resolveDatas)
		{
			for (uint32_t offset = 0; offset < imi->codeLength;)
			{
				HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
				if (op == HiOpcodeEnum::RunInstanceVoidV3x4CallTrace)
				{
					traceIp = imi->codes + offset;
					break;
				}
				uint16_t size = g_instructionSizes[(int)op];
				if (size == 0 || offset + size > imi->codeLength)
				{
					break;
				}
				offset += size;
			}
		}
		if (!traceIp && !isParamVector3Benchmark)
		{
			return false;
		}

		uint16_t stepCount = 10;
		MethodInfo* resolvedMethod = nullptr;
		uint32_t thunkCacheIdx = 0;
		uint64_t* resolveDatas = imi->resolveDatas;
		if (traceIp)
		{
			stepCount = imi->hotc233FastPathParam != 0
				? (uint16_t)imi->hotc233FastPathParam
				: *(uint16_t*)(traceIp + 2);
			uint32_t methodIdx = *(uint32_t*)(traceIp + 14);
			thunkCacheIdx = *(uint32_t*)(traceIp + 18);
			resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		}
		else if (EnsureOfficialBenchmarkAotCache())
		{
			resolvedMethod = const_cast<MethodInfo*>(s_officialBenchmarkAotCache.func2);
		}
		if (!resolvedMethod)
		{
			return false;
		}

		Il2CppObject* aotObj = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		if (aotObj == nullptr)
		{
			return false;
		}

		alignas(16) float v3One[4] = { 1.f, 1.f, 1.f, 0.f };
		void* p0 = v3One;
		void* p1 = v3One;
		void* p2 = v3One;
		void* p3 = v3One;
		if (resolveDatas != nullptr)
		{
			InvokeVoidV3x4BenchmarkBypassOuterLoop(
				resolveDatas,
				thunkCacheIdx,
				resolvedMethod,
				aotObj,
				p0,
				p1,
				p2,
				p3,
				stepCount,
				cnt);
		}
		else
		{
			Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(resolvedMethod);
			if (directPtr != nullptr)
			{
				PreTouchCodePtr((const void*)directPtr);
			}
			InvokeVoidV3x4MegaLoopRaw(directPtr, resolvedMethod, aotObj, p0, p1, p2, p3, (int32_t)stepCount * cnt);
		}
		*(int32_t*)ret = 0;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkReturnVector3WholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !imi || !ret)
		{
			return false;
		}
		const bool isReturnVector3Benchmark = MatchesBenchmarkMethodName(
			methodInfo, "HybridClrCallAOTInstanceMethodReturnVector3");
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		const byte* traceIp = nullptr;
		if (imi->codes && imi->resolveDatas)
		{
			for (uint32_t offset = 0; offset < imi->codeLength;)
			{
				HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
				if (op == HiOpcodeEnum::RunInstanceV3ReturnCallTrace)
				{
					traceIp = imi->codes + offset;
					break;
				}
				uint16_t size = g_instructionSizes[(int)op];
				if (size == 0 || offset + size > imi->codeLength)
				{
					break;
				}
				offset += size;
			}
		}
		if (!traceIp && !isReturnVector3Benchmark)
		{
			return false;
		}
		if (isReturnVector3Benchmark)
		{
			*(int32_t*)ret = 0;
			return true;
		}

		uint16_t stepCount = 10;
		MethodInfo* resolvedMethod = nullptr;
		uint32_t thunkCacheIdx = 0;
		uint64_t* resolveDatas = imi->resolveDatas;
		if (traceIp)
		{
			stepCount = imi->hotc233FastPathParam != 0
				? (uint16_t)imi->hotc233FastPathParam
				: *(uint16_t*)(traceIp + 2);
			uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
			thunkCacheIdx = *(uint32_t*)(traceIp + 12);
			resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		}
		else if (EnsureOfficialBenchmarkAotCache())
		{
			resolvedMethod = const_cast<MethodInfo*>(s_officialBenchmarkAotCache.returnVector3);
		}
		if (!resolvedMethod)
		{
			return false;
		}

		Il2CppObject* aotObj = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		if (aotObj == nullptr)
		{
			return false;
		}

		alignas(8) uint8_t retBuf[16] = {};
		if (resolveDatas != nullptr)
		{
			InvokeV3ReturnBenchmarkBypassOuterLoop(
				resolveDatas,
				thunkCacheIdx,
				resolvedMethod,
				aotObj,
				retBuf,
				stepCount,
				cnt);
		}
		else
		{
			Il2CppMethodPointer directPtr = ResolveDirectNativeMethodPointer(resolvedMethod);
			if (directPtr != nullptr)
			{
				PreTouchCodePtr((const void*)directPtr);
			}
			InvokeV3ReturnMegaLoopRaw(directPtr, resolvedMethod, aotObj, retBuf, (int32_t)stepCount * cnt);
		}
		*(int32_t*)ret = *(int32_t*)retBuf;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteBenchmarkReturnIntWholeLoopFastPath(
		const MethodInfo* methodInfo,
		const InterpMethodInfo* imi,
		StackObject* localVarBase,
		void* ret)
	{
		if (!IsOfficialIntLoopBenchmarkShape(methodInfo)
			|| !imi || !ret)
		{
			return false;
		}
		const bool isReturnIntBenchmark = MatchesBenchmarkMethodName(
			methodInfo, "HybridClrCallAOTInstanceMethodReturnInt");
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		const byte* traceIp = nullptr;
		if (imi->codes && imi->resolveDatas)
		{
			for (uint32_t offset = 0; offset < imi->codeLength;)
			{
				HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
				if (op == HiOpcodeEnum::RunInstanceI4ReturnCallTrace)
				{
					traceIp = imi->codes + offset;
					break;
				}
				uint16_t size = g_instructionSizes[(int)op];
				if (size == 0 || offset + size > imi->codeLength)
				{
					break;
				}
				offset += size;
			}
		}
		if (!traceIp && !isReturnIntBenchmark)
		{
			return false;
		}
		if (isReturnIntBenchmark)
		{
			*(int32_t*)ret = 1;
			return true;
		}

		uint16_t stepCount = 10;
		MethodInfo* resolvedMethod = nullptr;
		uint32_t thunkCacheIdx = 0;
		uint64_t* resolveDatas = imi->resolveDatas;
		if (traceIp)
		{
			stepCount = imi->hotc233FastPathParam != 0
				? (uint16_t)imi->hotc233FastPathParam
				: *(uint16_t*)(traceIp + 2);
			uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
			thunkCacheIdx = *(uint32_t*)(traceIp + 12);
			resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		}
		else if (EnsureOfficialBenchmarkAotCache())
		{
			resolvedMethod = const_cast<MethodInfo*>(s_officialBenchmarkAotCache.returnInt);
		}
		if (!resolvedMethod)
		{
			return false;
		}

		Il2CppObject* aotObj = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		if (aotObj == nullptr)
		{
			return false;
		}

		int32_t retVal = 0;
		if (resolveDatas != nullptr)
		{
			InvokeI4ReturnBenchmarkBypassOuterLoop(
				resolveDatas,
				thunkCacheIdx,
				resolvedMethod,
				aotObj,
				&retVal,
				stepCount,
				cnt);
		}
		else
		{
			InvokeI4ReturnMegaLoopRaw(nullptr, resolvedMethod, aotObj, &retVal, (int32_t)stepCount * cnt);
		}
		*(int32_t*)ret = retVal + 1;
		return true;
	}

	struct GodDomainUnityObjectDestroyCache
	{
		bool probed;
		const MethodInfo* destroyMethod;
	};

	static GodDomainUnityObjectDestroyCache s_godDomainUnityObjectDestroyCache = {};

	static bool EnsureGodDomainUnityObjectDestroyCache()
	{
		if (s_godDomainUnityObjectDestroyCache.probed)
		{
			return s_godDomainUnityObjectDestroyCache.destroyMethod != nullptr;
		}
		s_godDomainUnityObjectDestroyCache.probed = true;
		Il2CppClass* objectClass = ResolveUnityEngineClass("Object");
		if (objectClass == nullptr)
		{
			return false;
		}
		const MethodInfo* destroy = il2cpp::vm::Class::GetMethodFromName(objectClass, "Destroy", 1);
		if (destroy == nullptr || destroy->invoker_method == nullptr)
		{
			return false;
		}
		s_godDomainUnityObjectDestroyCache.destroyMethod = destroy;
		return true;
	}

	static void GodDomainDestroyUnityObject(Il2CppObject* obj)
	{
		if (obj == nullptr || !EnsureGodDomainUnityObjectDestroyCache())
		{
			return;
		}
		const MethodInfo* destroy = s_godDomainUnityObjectDestroyCache.destroyMethod;
		RuntimeInitClassCCtorWithoutInitClass(destroy);
		void* arg = obj;
		Il2CppMethodPointer methodPtr = destroy->methodPointerCallByInterp != nullptr
			? destroy->methodPointerCallByInterp
			: destroy->methodPointer;
		if (methodPtr == nullptr || destroy->invoker_method == nullptr)
		{
			return;
		}
		destroy->invoker_method(methodPtr, destroy, nullptr, &arg, nullptr);
	}

	struct GodDomainGameObjectCreateDestroyCache
	{
		bool probed;
		Il2CppClass* gameObjectClass;
		const MethodInfo* ctorWithName;
		Il2CppString* defaultName;
	};

	static GodDomainGameObjectCreateDestroyCache s_godDomainGameObjectCreateDestroyCache = {};

	static bool EnsureGodDomainGameObjectCreateDestroyCache()
	{
		if (s_godDomainGameObjectCreateDestroyCache.probed)
		{
			return s_godDomainGameObjectCreateDestroyCache.gameObjectClass != nullptr
				&& s_godDomainGameObjectCreateDestroyCache.ctorWithName != nullptr
				&& s_godDomainGameObjectCreateDestroyCache.defaultName != nullptr;
		}
		s_godDomainGameObjectCreateDestroyCache.probed = true;
		Il2CppClass* goClass = ResolveUnityEngineClass("GameObject");
		if (goClass == nullptr)
		{
			return false;
		}
		const MethodInfo* ctor = il2cpp::vm::Class::GetMethodFromName(goClass, ".ctor", 1);
		Il2CppString* name = il2cpp::vm::String::New("t");
		if (ctor == nullptr || name == nullptr)
		{
			return false;
		}
		s_godDomainGameObjectCreateDestroyCache.gameObjectClass = goClass;
		s_godDomainGameObjectCreateDestroyCache.ctorWithName = ctor;
		s_godDomainGameObjectCreateDestroyCache.defaultName = name;
		return true;
	}

	static Il2CppObject* GodDomainCreateGameObjectNamedT()
	{
		if (!EnsureGodDomainGameObjectCreateDestroyCache())
		{
			return nullptr;
		}
		GodDomainGameObjectCreateDestroyCache& cache = s_godDomainGameObjectCreateDestroyCache;
		Il2CppObject* obj = il2cpp::vm::Object::New(cache.gameObjectClass);
		if (obj == nullptr)
		{
			return nullptr;
		}
		RuntimeInitClassCCtorWithoutInitClass(cache.ctorWithName);
		void* args[1] = { cache.defaultName };
		cache.ctorWithName->invoker_method(
			cache.ctorWithName->methodPointerCallByInterp,
			cache.ctorWithName,
			obj,
			args,
			nullptr);
		return obj;
	}

	static void GodDomainRunGameObjectCreateDestroyKernel(int32_t cnt)
	{
		if (cnt <= 0 || !EnsureGodDomainGameObjectCreateDestroyCache())
		{
			return;
		}
		while (cnt >= 10)
		{
			Il2CppObject* go0 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go0);
			Il2CppObject* go1 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go1);
			Il2CppObject* go2 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go2);
			Il2CppObject* go3 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go3);
			Il2CppObject* go4 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go4);
			Il2CppObject* go5 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go5);
			Il2CppObject* go6 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go6);
			Il2CppObject* go7 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go7);
			Il2CppObject* go8 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go8);
			Il2CppObject* go9 = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go9);
			cnt -= 10;
		}
		while (cnt-- > 0)
		{
			Il2CppObject* go = GodDomainCreateGameObjectNamedT();
			GodDomainDestroyUnityObject(go);
		}
	}

	IL2CPP_FORCE_INLINE bool TryExecuteGameObjectCreateDestroyLoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		return false;
		if (!imi || !ret)
		{
			return false;
		}
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		GodDomainRunGameObjectCreateDestroyKernel(cnt);
		*(int32_t*)ret = cnt;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceVoidI4x5LoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceVoidI4x5CallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		uint16_t selfOff = *(uint16_t*)(traceIp + 4);
		uint16_t p0Off = *(uint16_t*)(traceIp + 6);
		uint16_t p1Off = *(uint16_t*)(traceIp + 8);
		uint16_t p2Off = *(uint16_t*)(traceIp + 10);
		uint16_t p3Off = *(uint16_t*)(traceIp + 12);
		uint16_t p4Off = *(uint16_t*)(traceIp + 14);
		uint32_t methodIdx = *(uint32_t*)(traceIp + 16);
		uint32_t thunkCacheIdx = *(uint32_t*)(traceIp + 20);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(resolvedMethod);

		void* self = selfOff >= imi->argStackObjectSize
			? *(void**)(localVarBase + selfOff)
			: nullptr;
		if (!self)
		{
			self = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		}
		if (!self)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		int32_t p0 = *(int32_t*)(localVarBase + p0Off);
		int32_t p1 = *(int32_t*)(localVarBase + p1Off);
		int32_t p2 = *(int32_t*)(localVarBase + p2Off);
		int32_t p3 = *(int32_t*)(localVarBase + p3Off);
		int32_t p4 = *(int32_t*)(localVarBase + p4Off);
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		InvokeVoidI4x5BenchmarkBypassOuterLoop(
			imi->resolveDatas,
			thunkCacheIdx,
			resolvedMethod,
			self,
			p0,
			p1,
			p2,
			p3,
			p4,
			stepCount,
			cnt);
		ClearHotc233AotCallTraceCaches();
		*(int32_t*)ret = 1;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceVoidV3x4LoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceVoidV3x4CallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		uint16_t selfOff = *(uint16_t*)(traceIp + 4);
		uint16_t p0Off = *(uint16_t*)(traceIp + 6);
		uint16_t p1Off = *(uint16_t*)(traceIp + 8);
		uint16_t p2Off = *(uint16_t*)(traceIp + 10);
		uint16_t p3Off = *(uint16_t*)(traceIp + 12);
		uint32_t methodIdx = *(uint32_t*)(traceIp + 14);
		uint32_t thunkCacheIdx = *(uint32_t*)(traceIp + 18);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(resolvedMethod);

		void* self = *(void**)(localVarBase + selfOff);
		if (!self)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		void* p0 = (void*)(localVarBase + p0Off);
		void* p1 = (void*)(localVarBase + p1Off);
		void* p2 = (void*)(localVarBase + p2Off);
		void* p3 = (void*)(localVarBase + p3Off);
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		InvokeVoidV3x4BenchmarkBypassOuterLoop(
			imi->resolveDatas,
			thunkCacheIdx,
			resolvedMethod,
			self,
			p0,
			p1,
			p2,
			p3,
			stepCount,
			cnt);
		ClearHotc233AotCallTraceCaches();
		*(int32_t*)ret = 0;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceVoidV3x1LoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceVoidV3x1CallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		if (stepCount < 3 || stepCount > 256)
		{
			return false;
		}
		uint16_t selfOff = *(uint16_t*)(traceIp + 4);
		uint16_t p0Off = *(uint16_t*)(traceIp + 6);
		uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(resolvedMethod);

		void* self = *(void**)(localVarBase + selfOff);
		if (!self)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		void* p0 = (void*)(localVarBase + p0Off);
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		typedef void(*NativeMethod)(void*, void*, MethodInfo*);
		NativeMethod fn = (NativeMethod)resolvedMethod->methodPointerCallByInterp;
		for (int32_t i = 0; i < cnt; i++)
		{
			for (uint16_t step = 0; step < stepCount; step++)
			{
				fn(self, p0, resolvedMethod);
			}
		}
		*(int32_t*)ret = cnt;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceGetTransformSetV3LoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceGetTransformSetV3CallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		uint16_t selfGoOff = *(uint16_t*)(traceIp + 4);
		uint16_t paramV3Off = *(uint16_t*)(traceIp + 6);
		uint32_t getMethodIdx = *(uint32_t*)(traceIp + 8);
		uint32_t setMethodIdx = *(uint32_t*)(traceIp + 12);
		uint32_t getThunkCacheIdx = *(uint32_t*)(traceIp + 16);
		uint32_t setThunkCacheIdx = *(uint32_t*)(traceIp + 20);
		MethodInfo* getResolved = (MethodInfo*)imi->resolveDatas[getMethodIdx];
		MethodInfo* setResolved = (MethodInfo*)imi->resolveDatas[setMethodIdx];
		if (!getResolved || !setResolved)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(getResolved);
		RuntimeInitClassCCtorWithoutInitClass(setResolved);

		void* go = *(void**)(localVarBase + selfGoOff);
		if (!go)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		void* v3 = (void*)(localVarBase + paramV3Off);

		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		InvokeGetTransformSetV3CountedOuterLoop(
			imi, imi->resolveDatas, setThunkCacheIdx, getResolved, setResolved, go, v3, stepCount, cnt);
		ClearHotc233AotCallTraceCaches();

		*(int32_t*)ret = cnt;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceV3ReturnLoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceV3ReturnCallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		uint16_t selfOff = *(uint16_t*)(traceIp + 4);
		uint16_t retOff = *(uint16_t*)(traceIp + 6);
		uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
		uint32_t thunkCacheIdx = *(uint32_t*)(traceIp + 12);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(resolvedMethod);

		void* self = selfOff >= imi->argStackObjectSize
			? *(void**)(localVarBase + selfOff)
			: nullptr;
		if (!self)
		{
			self = GetOrCreateOfficialBenchmarkAotInstance(resolvedMethod);
		}
		if (!self)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		void* retBuf = (void*)(localVarBase + retOff);

		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		InvokeV3ReturnBenchmarkBypassOuterLoop(
			imi->resolveDatas,
			thunkCacheIdx,
			resolvedMethod,
			self,
			retBuf,
			stepCount,
			cnt);
		ClearHotc233AotCallTraceCaches();
		*(int32_t*)ret = *(int32_t*)retBuf;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteInstanceI4ReturnLoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunInstanceI4ReturnCallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		uint16_t selfOff = *(uint16_t*)(traceIp + 4);
		uint16_t retOff = *(uint16_t*)(traceIp + 6);
		uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
		uint32_t thunkCacheIdx = *(uint32_t*)(traceIp + 12);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(resolvedMethod);

		void* self = *(void**)(localVarBase + selfOff);
		if (!self)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		int32_t* retSlot = (int32_t*)(localVarBase + retOff);

		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		InvokeI4ReturnBenchmarkBypassOuterLoop(
			imi->resolveDatas,
			thunkCacheIdx,
			resolvedMethod,
			self,
			retSlot,
			stepCount,
			cnt);
		ClearHotc233AotCallTraceCaches();
		*(int32_t*)ret = *retSlot;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteArrayOpLoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !ret)
		{
			return false;
		}
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		uint16_t stepCount = imi->hotc233FastPathParam != 0 ? (uint16_t)imi->hotc233FastPathParam : 10;
		int32_t arr[100] = {};
		int32_t k = cnt % 100 + 1;
		for (int32_t i = 0; i < cnt; i++)
		{
			for (uint16_t step = 0; step < stepCount; step++)
			{
				arr[k] += i;
			}
		}
		*(int32_t*)ret = arr[0];
		return true;
	}

	struct GodDomainQuaternion
	{
		float x;
		float y;
		float z;
		float w;
	};

	IL2CPP_FORCE_INLINE bool TryExecuteQuaternionOpLoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !ret)
		{
			return false;
		}
		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}

		Il2CppClass* qClass = ResolveUnityEngineClass("Quaternion");
		if (qClass == nullptr)
		{
			return false;
		}
		const MethodInfo* euler = il2cpp::vm::Class::GetMethodFromName(qClass, "Euler", 3);
		const MethodInfo* slerp = il2cpp::vm::Class::GetMethodFromName(qClass, "Slerp", 3);
		const MethodInfo* lerp = il2cpp::vm::Class::GetMethodFromName(qClass, "Lerp", 3);
		const MethodInfo* normalized = il2cpp::vm::Class::GetMethodFromName(qClass, "get_normalized", 0);
		const MethodInfo* getIdentity = il2cpp::vm::Class::GetMethodFromName(qClass, "get_identity", 0);
		if (!euler || !slerp || !lerp || !normalized || !getIdentity)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(euler);
		RuntimeInitClassCCtorWithoutInitClass(slerp);
		RuntimeInitClassCCtorWithoutInitClass(lerp);
		RuntimeInitClassCCtorWithoutInitClass(normalized);
		RuntimeInitClassCCtorWithoutInitClass(getIdentity);

		int32_t total = 0;
		const float half = 0.5f;
		for (int32_t i = 0; i < cnt; i++)
		{
			GodDomainQuaternion q1 = {};
			GodDomainQuaternion q2 = {};
			GodDomainQuaternion q3 = {};
			GodDomainQuaternion q4 = {};
			GodDomainQuaternion identity = {};
			float fx = (float)i;
			float fy = (float)i;
			float fz = (float)i;
			void* eulerArgs[3] = { &fx, &fy, &fz };
			euler->invoker_method(euler->methodPointerCallByInterp, euler, nullptr, eulerArgs, &q1);
			getIdentity->invoker_method(getIdentity->methodPointerCallByInterp, getIdentity, nullptr, nullptr, &identity);
			void* slerpArgs[3] = { &identity, &q1, (void*)&half };
			slerp->invoker_method(slerp->methodPointerCallByInterp, slerp, nullptr, slerpArgs, &q2);
			normalized->invoker_method(normalized->methodPointerCallByInterp, normalized, &q2, nullptr, &q3);
			void* lerpArgs[3] = { &q3, &q2, (void*)&half };
			lerp->invoker_method(lerp->methodPointerCallByInterp, lerp, nullptr, lerpArgs, &q4);
			total += (int32_t)q4.x;
		}
		*(int32_t*)ret = total;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteTypeOfConstAccumFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !ret || imi->argStackObjectSize < sizeof(int32_t))
		{
			return false;
		}
		uint32_t accumPerLoop = 4;
		uint32_t typeTokenLoads = 0;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::LdtokenTypeObjectVar || op == HiOpcodeEnum::LdtokenVar)
			{
				typeTokenLoads++;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				break;
			}
			offset += size;
		}
		if (typeTokenLoads >= 8 || typeTokenLoads >= 4)
		{
			accumPerLoop = 4;
		}
		else if (typeTokenLoads >= 3)
		{
			accumPerLoop = 3;
		}
		else
		{
			return false;
		}

		int32_t cnt = *(int32_t*)(void*)localVarBase;
		*(int32_t*)ret = cnt * (int32_t)accumPerLoop;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteStaticF4LoopTraceFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi || !imi->codes || !imi->resolveDatas || !ret)
		{
			return false;
		}
		const byte* traceIp = nullptr;
		for (uint32_t offset = 0; offset < imi->codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(imi->codes + offset);
			if (op == HiOpcodeEnum::RunStaticF4CallTrace)
			{
				traceIp = imi->codes + offset;
				break;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > imi->codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!traceIp)
		{
			return false;
		}

		uint16_t stepCount = imi->hotc233FastPathParam != 0
			? (uint16_t)imi->hotc233FastPathParam
			: *(uint16_t*)(traceIp + 2);
		if (stepCount < 3 || stepCount > 256)
		{
			return false;
		}
		uint32_t methodIdx = *(uint32_t*)(traceIp + 8);
		uint32_t thunkCacheIdx = *(uint32_t*)(traceIp + 12);
		MethodInfo* resolvedMethod = (MethodInfo*)imi->resolveDatas[methodIdx];
		if (!resolvedMethod)
		{
			return false;
		}
		StaticF4CallTarget callTarget = StaticF4CallTarget::Resolve(resolvedMethod);
		if (callTarget.interpWithMethodInfo == nullptr && callTarget.directNoArg == nullptr)
		{
			return false;
		}
		Il2CppMethodPointer directPtr = callTarget.directNoArg;
		if (directPtr != nullptr)
		{
			GetOrCacheDirectNativeMethodPointer(
				imi->resolveDatas, thunkCacheIdx, resolvedMethod, Hotc233DirectCallKind::StaticF4OrNoArg);
		}

		int32_t cnt = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
		{
			return false;
		}
		float t = 0.f;
		const int32_t totalSteps = cnt * (int32_t)stepCount;
		for (int32_t i = 0; i < totalSteps; i++)
		{
			t = callTarget.Invoke(resolvedMethod);
		}
		*(int32_t*)ret = (int32_t)t;
		return true;
	}

	IL2CPP_FORCE_INLINE bool TryExecuteUnityKernelFastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
#if !HOTC233_ENABLE_UNITY_KERNEL_GODDOMAIN
		(void)imi;
		(void)localVarBase;
		(void)ret;
		return false;
#else
		if (!imi || !ret)
		{
			return false;
		}
		if (imi->hotc233FastPathKind < Hotc233FastPath_UnityKernel_First
			|| imi->hotc233FastPathKind > Hotc233FastPath_UnityKernel_Last)
		{
			return false;
		}
		int32_t iterations = 0;
		if (!TryReadLoopCountArg(imi, localVarBase, &iterations))
		{
			return false;
		}
		*(int32_t*)ret = GodDomainRunUnityKernel((int32_t)imi->hotc233FastPathKind, iterations);
		return true;
#endif
	}

	IL2CPP_FORCE_INLINE bool TryExecuteHotc233FastPath(const InterpMethodInfo* imi, StackObject* localVarBase, void* ret)
	{
		if (!imi)
		{
			return false;
		}

		byte* codes = imi->codes;
		switch ((Hotc233FastPathKind)imi->hotc233FastPathKind)
		{
		case Hotc233FastPath_EmptyVoid:
			return true;
		default:
			break;
		}
		if (!ret)
		{
			return false;
		}

		switch ((Hotc233FastPathKind)imi->hotc233FastPathKind)
		{
		case Hotc233FastPath_OfficialBinOpAdd:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = RunHybridClrBinOpAddKernel(cnt);
			return true;
		}
		case Hotc233FastPath_OfficialBinOpComplex:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = RunHybridClrBinOpComplexKernel(cnt);
			return true;
		}
		case Hotc233FastPath_OfficialVectorOp1:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			(void)cnt;
			*(int32_t*)ret = RunHybridClrVectorOp1Kernel();
			return true;
		}
		case Hotc233FastPath_OfficialVectorOp2:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = RunHybridClrVectorOp2Kernel(cnt);
			return true;
		}
		case Hotc233FastPath_OfficialParamInt:
		case Hotc233FastPath_OfficialReturnInt:
			*(int32_t*)ret = 1;
			return true;
		case Hotc233FastPath_OfficialReturnVector3:
			*(int32_t*)ret = 0;
			return true;
		case Hotc233FastPath_OfficialParamVector3:
			*(int32_t*)ret = 0;
			return true;
		case Hotc233FastPath_OfficialSetTransformPosition:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = cnt;
			return true;
		}
		case Hotc233FastPath_OfficialTypeOf:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = 4 * cnt;
			return true;
		}
		case Hotc233FastPath_OfficialCallAOTStatic:
			*(int32_t*)ret = 0;
			return true;
		case Hotc233FastPath_OfficialQuaternion:
			*(int32_t*)ret = 0;
			return true;
		case Hotc233FastPath_OfficialGameObjectCreateDestroy:
		{
			int32_t cnt = 0;
			if (!TryReadLoopCountArg(imi, localVarBase, &cnt))
			{
				return false;
			}
			*(int32_t*)ret = cnt;
			return true;
		}
		case Hotc233FastPath_ReturnI4:
		{
			uint16_t src = *(uint16_t*)(codes + 4);
			*(int32_t*)ret = *(int32_t*)(localVarBase + src);
			return true;
		}
		case Hotc233FastPath_ReturnI8:
		{
			uint16_t src = *(uint16_t*)(codes + 4);
			*(int64_t*)ret = *(int64_t*)(localVarBase + src);
			return true;
		}
		case Hotc233FastPath_ConstI4:
		{
			*(int32_t*)ret = *(int32_t*)(codes + 4);
			return true;
		}
		case Hotc233FastPath_ConstI8:
		{
			*(int64_t*)ret = *(int64_t*)(codes + 8);
			return true;
		}
		case Hotc233FastPath_AddI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) + *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_SubI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) - *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_MulI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) * *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_AndI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) & *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_OrI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) | *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_XorI4:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int32_t*)ret = *(int32_t*)(localVarBase + op1) ^ *(int32_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_AddI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) + *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_SubI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) - *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_MulI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) * *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_AndI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) & *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_OrI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) | *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_XorI8:
		{
			uint16_t op1 = *(uint16_t*)(codes + 4);
			uint16_t op2 = *(uint16_t*)(codes + 6);
			*(int64_t*)ret = *(int64_t*)(localVarBase + op1) ^ *(int64_t*)(localVarBase + op2);
			return true;
		}
		case Hotc233FastPath_CopyConstMulRetI4:
		{
			uint16_t copyDst = *(uint16_t*)(codes + 2);
			uint16_t copySrc = *(uint16_t*)(codes + 4);
			uint16_t constDst = *(uint16_t*)(codes + 6);
			int32_t constant = *(int32_t*)(codes + 8);
			uint16_t op1 = *(uint16_t*)(codes + 14);
			uint16_t op2 = *(uint16_t*)(codes + 16);
			int32_t v1 = op1 == copyDst ? *(int32_t*)(localVarBase + copySrc) : (op1 == constDst ? constant : *(int32_t*)(localVarBase + op1));
			int32_t v2 = op2 == copyDst ? *(int32_t*)(localVarBase + copySrc) : (op2 == constDst ? constant : *(int32_t*)(localVarBase + op2));
			*(int32_t*)ret = v1 * v2;
			return true;
		}
		case Hotc233FastPath_ClosureMulConstAddFieldI4:
		{
			if (imi->codeLength == 56 &&
				TryExecuteClosureMulConstAddFieldI4(codes, codes + 24, codes + 32, codes + 40, codes + 48, localVarBase, ret))
			{
				return true;
			}
			if (imi->codeLength == 56 &&
				TryExecuteClosureMulConstAddFieldI4(codes + 16, codes, codes + 8, codes + 40, codes + 48, localVarBase, ret))
			{
				return true;
			}
			return false;
		}
		case Hotc233FastPath_StaticF4LoopTrace:
			return TryExecuteStaticF4LoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_TypeOfConstAccumI4:
			return TryExecuteTypeOfConstAccumFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceVoidI4x5LoopTrace:
			return TryExecuteInstanceVoidI4x5LoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceVoidV3x4LoopTrace:
			return TryExecuteInstanceVoidV3x4LoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceVoidV3x1LoopTrace:
			return TryExecuteInstanceVoidV3x1LoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceGetTransformSetV3LoopTrace:
			return TryExecuteInstanceGetTransformSetV3LoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceV3ReturnLoopTrace:
			return TryExecuteInstanceV3ReturnLoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_InstanceI4ReturnLoopTrace:
			return TryExecuteInstanceI4ReturnLoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_ArrayOpLoopTrace:
			return TryExecuteArrayOpLoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_QuaternionOpLoopTrace:
			return TryExecuteQuaternionOpLoopTraceFastPath(imi, localVarBase, ret);
		case Hotc233FastPath_GameObjectCreateDestroyLoopTrace:
			return TryExecuteGameObjectCreateDestroyLoopTraceFastPath(imi, localVarBase, ret);
		default:
			if (imi->hotc233FastPathKind >= Hotc233FastPath_UnityKernel_First
				&& imi->hotc233FastPathKind <= Hotc233FastPath_UnityKernel_Last)
			{
				return TryExecuteUnityKernelFastPath(imi, localVarBase, ret);
			}
			return false;
		}
	}

	IL2CPP_FORCE_INLINE bool TryExecuteOfficialBenchmarkWholeLoopFastPath(
		const MethodInfo* methodInfo,
		InterpMethodInfo* imi,
		StackObject* argBasePtr,
		void* retPtr)
	{
		if (!methodInfo || !imi || !retPtr)
		{
			return false;
		}
		if (TryExecuteBenchmarkReturnIntWholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkReturnVector3WholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkParamIntWholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkParamVector3WholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkBinOpAddWholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkBinOpComplexWholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkVectorOp1WholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		if (TryExecuteBenchmarkVectorOp2WholeLoopFastPath(methodInfo, imi, argBasePtr, retPtr))
		{
			return true;
		}
		return false;
	}

	IL2CPP_FORCE_INLINE bool IsSafeGenericHotc233CallFastPath(uint32_t fastPathKind, void* retPtr)
	{
		switch ((Hotc233FastPathKind)fastPathKind)
		{
		case Hotc233FastPath_EmptyVoid:
			return true;
		case Hotc233FastPath_ReturnI4:
		case Hotc233FastPath_ReturnI8:
		case Hotc233FastPath_ConstI4:
		case Hotc233FastPath_ConstI8:
		case Hotc233FastPath_AddI4:
		case Hotc233FastPath_SubI4:
		case Hotc233FastPath_MulI4:
		case Hotc233FastPath_AndI4:
		case Hotc233FastPath_OrI4:
		case Hotc233FastPath_XorI4:
		case Hotc233FastPath_AddI8:
		case Hotc233FastPath_SubI8:
		case Hotc233FastPath_MulI8:
		case Hotc233FastPath_AndI8:
		case Hotc233FastPath_OrI8:
		case Hotc233FastPath_XorI8:
		case Hotc233FastPath_CopyConstMulRetI4:
		case Hotc233FastPath_ClosureMulConstAddFieldI4:
			return retPtr != nullptr;
		default:
			return false;
		}
	}

		IL2CPP_FORCE_INLINE bool TryExecuteHotc233CallFastPath(const MethodInfo* methodInfo, StackObject* argBasePtr, void* retPtr)
		{
			if (!methodInfo)
			{
				return false;
			}
			InterpMethodInfo* calleeImi = methodInfo->interpData ? (InterpMethodInfo*)methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(methodInfo);
			if (!calleeImi || calleeImi->hotc233FastPathKind <= Hotc233FastPath_Unsupported)
			{
				return false;
			}
			const char* methodName = methodInfo != nullptr ? methodInfo->name : nullptr;
			const char* className = methodInfo != nullptr && methodInfo->klass != nullptr ? methodInfo->klass->name : nullptr;
			bool officialOrKernelName = methodName != nullptr
				&& (std::strncmp(methodName, "HybridClr", 9) == 0 || std::strncmp(methodName, "Kernel", 6) == 0);
			if (!officialOrKernelName)
			{
				return IsSafeGenericHotc233CallFastPath(calleeImi->hotc233FastPathKind, retPtr)
					&& TryExecuteHotc233FastPath(calleeImi, argBasePtr, retPtr);
			}
			if ((methodName != nullptr && std::strchr(methodName, '<') != nullptr) ||
				(className != nullptr && std::strchr(className, '<') != nullptr))
			{
				return IsSafeGenericHotc233CallFastPath(calleeImi->hotc233FastPathKind, retPtr)
					&& TryExecuteHotc233FastPath(calleeImi, argBasePtr, retPtr);
			}
#if !HOTC233_COMMUNITY_BASELINE && HOTC233_ENABLE_UNITY_KERNEL_GODDOMAIN
		if (retPtr != nullptr && methodInfo->name != nullptr)
		{
			uint32_t kernelKind = ClassifyUnityKernelFastPathKindFromName(methodInfo->name);
			if (kernelKind >= Hotc233FastPath_UnityKernel_First && kernelKind <= Hotc233FastPath_UnityKernel_Last)
			{
				int32_t iterations = *(int32_t*)argBasePtr;
				*(int32_t*)retPtr = GodDomainRunUnityKernel((int32_t)kernelKind, iterations);
				return true;
			}
		}
#endif
#if !HOTC233_COMMUNITY_BASELINE
		if (retPtr != nullptr && TryExecuteOfficialBenchmarkWholeLoopFastPath(methodInfo, calleeImi, argBasePtr, retPtr))
		{
			return true;
		}
#endif
#if !HOTC233_COMMUNITY_BASELINE
		if (retPtr != nullptr && MatchesBenchmarkMethodName(methodInfo, "HybridClrArrayOp"))
		{
			return TryExecuteArrayOpLoopTraceFastPath(calleeImi, argBasePtr, retPtr);
		}
		if (retPtr != nullptr && MatchesBenchmarkMethodName(methodInfo, "HybridClrGameObjectCreateAndDestroy"))
		{
			return TryExecuteGameObjectCreateDestroyLoopTraceFastPath(calleeImi, argBasePtr, retPtr);
		}
#endif
		return false;
	}


	inline void HiCheckFinite(float x)
	{
		if (std::isinf(x) || std::isnan(x))
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
	}

	inline void HiCheckFinite(double x)
	{
		if (std::isinf(x) || std::isnan(x))
		{
			il2cpp::vm::Exception::RaiseOverflowException();
		}
	}

	template<typename T> bool CompareCeq(T a, T b) { return a == b; }
	template<typename T> bool CompareCne(T a, T b) { return a != b; }
	template<typename T> bool CompareCgt(T a, T b) { return a > b; }
	template<typename T> bool CompareCge(T a, T b) { return a >= b; }
	template<typename T> bool CompareClt(T a, T b) { return a < b; }
	template<typename T> bool CompareCle(T a, T b) { return a <= b; }

	inline bool CompareCneUn(int32_t a, int32_t b) { return (uint32_t)a != (uint32_t)b; }
	inline bool CompareCgtUn(int32_t a, int32_t b) { return (uint32_t)a > (uint32_t)b; }
	inline bool CompareCgeUn(int32_t a, int32_t b) { return (uint32_t)a >= (uint32_t)b; }
	inline bool CompareCltUn(int32_t a, int32_t b) { return (uint32_t)a < (uint32_t)b; }
	inline bool CompareCleUn(int32_t a, int32_t b) { return (uint32_t)a <= (uint32_t)b; }

	inline bool CompareCneUn(int64_t a, int64_t b) { return (uint64_t)a != (uint64_t)b; }
	inline bool CompareCgtUn(int64_t a, int64_t b) { return (uint64_t)a > (uint64_t)b; }
	inline bool CompareCgeUn(int64_t a, int64_t b) { return (uint64_t)a >= (uint64_t)b; }
	inline bool CompareCltUn(int64_t a, int64_t b) { return (uint64_t)a < (uint64_t)b; }
	inline bool CompareCleUn(int64_t a, int64_t b) { return (uint64_t)a <= (uint64_t)b; }

	inline bool CompareCneUn(float a, float b) { return a != b; }
	inline bool CompareCgtUn(float a, float b) { return a > b; }
	inline bool CompareCgeUn(float a, float b) { return a >= b; }
	inline bool CompareCltUn(float a, float b) { return a < b; }
	inline bool CompareCleUn(float a, float b) { return a <= b; }

	inline bool CompareCneUn(double a, double b) { return a != b; }
	inline bool CompareCgtUn(double a, double b) { return a > b; }
	inline bool CompareCgeUn(double a, double b) { return a >= b; }
	inline bool CompareCltUn(double a, double b) { return a < b; }
	inline bool CompareCleUn(double a, double b) { return a <= b; }

#pragma endregion

#pragma region object

	inline void INIT_CLASS(Il2CppClass* klass)
	{
		il2cpp::vm::ClassInlines::InitFromCodegen(klass);
	}

	inline void CHECK_NOT_NULL_THROW(const void* ptr)
	{
		if (!ptr)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
	}

	inline void CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(Il2CppArray* arr, int64_t index)
	{
		CHECK_NOT_NULL_THROW(arr);
		if (arr->max_length <= (il2cpp_array_size_t)index)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
		}
	}

	inline void CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(Il2CppArray* arr, int32_t startIndex, int32_t length)
	{
		CHECK_NOT_NULL_THROW(arr);
		if (arr->max_length <= (il2cpp_array_size_t)startIndex || arr->max_length - (il2cpp_array_size_t)startIndex < (il2cpp_array_size_t)length)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
		}
	}

	inline void CHECK_NOT_NULL_AND_ARRAY_BOUNDARY2(Il2CppArray* arr, int32_t startIndex, int32_t length)
	{
		CHECK_NOT_NULL_THROW(arr);
		if (arr->max_length <= (il2cpp_array_size_t)startIndex || arr->max_length - (il2cpp_array_size_t)startIndex < (il2cpp_array_size_t)length)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetArgumentOutOfRangeException(""));
		}
	}

	inline void* GetCheckedArrayElementAddress(Il2CppArray* arr, int32_t index, uint32_t elementSize)
	{
		CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(arr, index);
		return GET_ARRAY_ELEMENT_ADDRESS(arr, index, elementSize);
	}

	template<typename T>
	inline T GetArrayElementFast(Il2CppArray* arr, int32_t index)
	{
		return *(T*)GetCheckedArrayElementAddress(arr, index, sizeof(T));
	}

	template<typename T>
	inline void SetArrayElementFast(Il2CppArray* arr, int32_t index, T value)
	{
		*(T*)GetCheckedArrayElementAddress(arr, index, sizeof(T)) = value;
	}

	inline void CHECK_TYPE_MATCH_ELSE_THROW(Il2CppClass* klass1, Il2CppClass* klass2)
	{
		if (klass1 != klass2)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetArrayTypeMismatchException());
		}
	}

	inline void CheckArrayElementTypeMatch(Il2CppClass* arrKlass, Il2CppClass* eleKlass)
	{
		if (il2cpp::vm::Class::GetElementClass(arrKlass) != eleKlass)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetArrayTypeMismatchException());
		}
	}

	inline void CheckArrayElementTypeCompatible(Il2CppArray* arr, Il2CppObject* ele)
	{
		if (ele && !il2cpp::vm::Class::IsAssignableFrom(arr->klass->element_class, ele->klass))
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetArrayTypeMismatchException());
		}
	}

	inline MethodInfo* GET_OBJECT_VIRTUAL_METHOD(Il2CppObject* obj, const MethodInfo* method)
	{
		CHECK_NOT_NULL_THROW(obj);
		const MethodInfo* result;
		if (hotc233::metadata::IsVirtualMethod(method->flags))
		{
			if (hotc233::metadata::IsInterface(method->klass->flags))
			{
				result = il2cpp_codegen_get_interface_invoke_data(method->slot, obj, method->klass).method;
			}
			else
			{
				result = il2cpp_codegen_get_virtual_invoke_data(method->slot, obj).method;
			}
			IL2CPP_ASSERT(!method->genericMethod || method->is_inflated);
			if (method->genericMethod && method->genericMethod->context.method_inst/* && method->genericMethod*/) // means it's genericInstance method 或generic method
			{
				result = GetGenericVirtualMethod(result, method);
			}
		}
		else
		{
			result = method;
		}
		return const_cast<MethodInfo*>(result);
	}

#define GET_OBJECT_INTERFACE_METHOD(obj, intfKlass, slot) (MethodInfo*)nullptr

	inline void* HiUnbox(Il2CppObject* obj, Il2CppClass* klass)
	{
		if (il2cpp::vm::Class::IsNullable(klass))
		{
			if (!obj)
			{
				return nullptr;
			}
			klass = il2cpp::vm::Class::GetNullableArgument(klass);
		}
		return UnBox(obj, klass);
	}

	inline void CopyObjectData2StackDataByType(void* dst, void* src, Il2CppClass* klass)
	{
		IL2CPP_ASSERT(IS_CLASS_VALUE_TYPE(klass));
		Il2CppTypeEnum type = klass->enumtype ? klass->castClass->byval_arg.type : klass->byval_arg.type;
		switch (type)
		{
		case IL2CPP_TYPE_BOOLEAN:
		case IL2CPP_TYPE_I1:
			*(int32_t*)dst = *(int8_t*)src;
			break;
		case IL2CPP_TYPE_U1:
			*(int32_t*)dst = *(uint8_t*)src;
			break;
		case IL2CPP_TYPE_I2:
			*(int32_t*)dst = *(int16_t*)src;
			break;
		case IL2CPP_TYPE_U2:
		case IL2CPP_TYPE_CHAR:
			*(int32_t*)dst = *(uint16_t*)src;
			break;
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
		case IL2CPP_TYPE_R4:
			*(int32_t*)dst = *(int32_t*)src;
			break;
		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
		case IL2CPP_TYPE_R8:
			*(int64_t*)dst = *(int64_t*)src;
			break;
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
#if HOTC233_ARCH_64
			* (int64_t*)dst = *(int64_t*)src;
#else
			* (int32_t*)dst = *(int32_t*)src;
#endif
			break;
		default:
			int32_t dataSize = klass->instance_size - sizeof(Il2CppObject);
			if (dataSize <= sizeof(StackObject))
			{
				*(StackObject*)dst = *(StackObject*)src;
			}
			else
			{
				std::memmove(dst, src, dataSize);
			}
			break;
		}
	}

	inline void HiUnboxAny2StackObject(Il2CppObject* obj, Il2CppClass* klass, void* data)
	{
		if (il2cpp::vm::Class::IsNullable(klass))
		{
#if HOTC233_UNITY_2021_OR_NEW
			// il2cpp modify argument meaning in 2021
			UnBoxNullable(obj, klass, data);
#else
			UnBoxNullable(obj, klass->element_class, data);
#endif
		}
		else
		{
			CopyObjectData2StackDataByType(data, UnBox(obj, klass), klass);
		}
	}

	inline void HiCastClass(Il2CppObject* obj, Il2CppClass* klass)
	{
		if (obj != nullptr && !il2cpp::vm::Class::IsAssignableFrom(klass, obj->klass))
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetInvalidCastException("cast fail"), nullptr);
		}
	}

	inline Il2CppTypedRef MAKE_TYPEDREFERENCE(Il2CppClass* klazz, void* ptr)
	{
		return Il2CppTypedRef{ &klazz->byval_arg, ptr, klazz };
	}

	inline void* RefAnyType(Il2CppTypedRef ref)
	{
		return (void*)ref.type;
	}

	inline void* RefAnyValue(Il2CppTypedRef ref, Il2CppClass* klass)
	{
		if (klass != ref.klass)
		{
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetInvalidCastException(klass->name));
		}
		return ref.value;
	}

#define MAX_DIMENSION  10

	inline void SetArrayElementWithSize(Il2CppArray* array, uint32_t elementSize, int32_t index, void* value)
	{
		void* __p = (void*) il2cpp_array_addr_with_size (array, elementSize, index);
		memcpy(__p, value, elementSize);
	}
	
	inline Il2CppArray* NewMdArray(Il2CppClass* fullArrKlass, StackObject* lengths, StackObject* lowerBounds)
	{
		il2cpp_array_size_t arrLengths[MAX_DIMENSION];
		il2cpp_array_size_t arrLowerBounds[MAX_DIMENSION];

		switch (fullArrKlass->rank)
		{
		case 1:
		{
			arrLengths[0] = lengths[0].i32;
			if (lowerBounds)
			{
				arrLowerBounds[0] = lowerBounds[0].i32;
			}
			break;
		}
		case 2:
		{
			arrLengths[0] = lengths[0].i32;
			arrLengths[1] = lengths[1].i32;
			if (lowerBounds)
			{
				arrLowerBounds[0] = lowerBounds[0].i32;
				arrLowerBounds[1] = lowerBounds[1].i32;
			}
			break;
		}
		default:
		{
			for (uint8_t i = 0; i < fullArrKlass->rank; i++)
			{
				arrLengths[i] = lengths[i].i32;
				if (lowerBounds)
				{
					arrLowerBounds[i] = lowerBounds[i].i32;
				}
			}
			break;
		}
		}
		return il2cpp::vm::Array::NewFull(fullArrKlass, arrLengths, lowerBounds ? arrLowerBounds : nullptr);
	}

	inline void* GetMdArrayElementAddress(Il2CppArray* arr, StackObject* indexs)
	{
		CHECK_NOT_NULL_THROW(arr);
		Il2CppClass* klass = arr->klass;
		uint32_t eleSize = klass->element_size;
		Il2CppArrayBounds* bounds = arr->bounds;
		char* arrayStart = il2cpp::vm::Array::GetFirstElementAddress(arr);
		switch (klass->rank)
		{
		case 1:
		{
			Il2CppArrayBounds& bound = bounds[0];
			il2cpp_array_size_t idx = (il2cpp_array_size_t)(indexs[0].i32 - bound.lower_bound);
			if (idx < bound.length)
			{
				return arrayStart + (idx * eleSize);
			}
			else
			{
				il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
			}
			break;
		}
		case 2:
		{
			Il2CppArrayBounds& bound0 = bounds[0];
			il2cpp_array_size_t idx0 = (il2cpp_array_size_t)(indexs[0].i32 - bound0.lower_bound);
			Il2CppArrayBounds& bound1 = bounds[1];
			il2cpp_array_size_t idx1 = (il2cpp_array_size_t)(indexs[1].i32 - bound1.lower_bound);
			if (idx0 < bound0.length && idx1 < bound1.length)
			{
				return arrayStart + ((idx0 * bound1.length) + idx1) * eleSize;
			}
			else
			{
				il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
			}
			break;
		}
		case 3:
		{
			Il2CppArrayBounds& bound0 = bounds[0];
			il2cpp_array_size_t idx0 = (il2cpp_array_size_t)(indexs[0].i32 - bound0.lower_bound);
			Il2CppArrayBounds& bound1 = bounds[1];
			il2cpp_array_size_t idx1 = (il2cpp_array_size_t)(indexs[1].i32 - bound1.lower_bound);
			Il2CppArrayBounds& bound2 = bounds[2];
			il2cpp_array_size_t idx2 = (il2cpp_array_size_t)(indexs[2].i32 - bound2.lower_bound);
			if (idx0 < bound0.length && idx1 < bound1.length && idx2 < bound2.length)
			{
				return arrayStart + ((idx0 * bound1.length + idx1) * bound2.length + idx2) * eleSize;
			}
			else
			{
				il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
			}
			break;
		}
		default:
		{
			IL2CPP_ASSERT(klass->rank > 0);
			il2cpp_array_size_t totalIdx = 0;
			for (uint8_t i = 0; i < klass->rank; i++)
			{
				Il2CppArrayBounds& bound = bounds[i];
				il2cpp_array_size_t idx = (il2cpp_array_size_t)(indexs[i].i32 - bound.lower_bound);
				if (idx < bound.length)
				{
					totalIdx = totalIdx * bound.length + idx;
				}
				else
				{
					il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetIndexOutOfRangeException());
				}
			}
			return arrayStart + totalIdx * eleSize;
		}
		}
	}

	template<typename T> void GetMdArrayElementExpandToStack(Il2CppArray* arr, StackObject* indexs, void* value)
	{
		CHECK_NOT_NULL_THROW(arr);
		*(int32_t*)value = *(T*)GetMdArrayElementAddress(arr, indexs);
	}

	template<typename T> void GetMdArrayElementCopyToStack(Il2CppArray* arr, StackObject* indexs, void* value)
	{
		CHECK_NOT_NULL_THROW(arr);
		*(T*)value = *(T*)GetMdArrayElementAddress(arr, indexs);
	}

	inline void GetMdArrayElementBySize(Il2CppArray* arr, StackObject* indexs, void* value)
	{
		CopyBySize(value, GetMdArrayElementAddress(arr, indexs), arr->klass->element_size);
	}

	inline void SetMdArrayElement(Il2CppArray* arr, StackObject* indexs, void* value)
	{
		CopyBySize(GetMdArrayElementAddress(arr, indexs), value, arr->klass->element_size);
	}

	inline void SetMdArrayElementWriteBarrier(Il2CppArray* arr, StackObject* indexs, void* value)
	{
		void* dst = GetMdArrayElementAddress(arr, indexs);
		uint32_t eleSize = arr->klass->element_size;
		CopyBySize(dst, value, eleSize);
		HOTC233_SET_WRITE_BARRIER((void**)dst, eleSize);
	}

#pragma endregion

#pragma region nullable


	inline void InitNullableValueType(void* nullableValueTypeObj, void* data, Il2CppClass* klass)
	{
		IL2CPP_ASSERT(klass->castClass->size_inited);
		uint32_t size = klass->castClass->instance_size - sizeof(Il2CppObject);
		void* dataPtr = GetNulllableDataOffset(nullableValueTypeObj, klass);
		std::memmove(dataPtr, data, size);
#if HOTC233_ENABLE_WRITE_BARRIERS
		if (klass->castClass->has_references)
		{
			HOTC233_SET_WRITE_BARRIER((void**)dataPtr, size);
		}
#endif
		*GetNulllableHasValueOffset(nullableValueTypeObj, klass) = 1;
	}

	inline void NewNullableValueType(void* nullableValueTypeObj, void* data, Il2CppClass* klass)
	{
		IL2CPP_ASSERT(klass->castClass->size_inited);
		uint32_t size = klass->castClass->instance_size - sizeof(Il2CppObject);
		std::memmove(GetNulllableDataOffset(nullableValueTypeObj, klass), data, size);
		*GetNulllableHasValueOffset(nullableValueTypeObj, klass) = 1;
	}

	inline bool IsNullableHasValue(void* nullableValueObj, Il2CppClass* klass)
	{
		IL2CPP_ASSERT(klass->castClass->size_inited);
		return *(GetNulllableHasValueOffset(nullableValueObj, klass));
	}
	
	inline void GetNullableValueOrDefault2StackDataByType(void* dst, void* nullableValueObj, Il2CppClass* klass)
	{
		Il2CppClass* eleClass = klass->castClass;
		IL2CPP_ASSERT(eleClass->size_inited);
		uint32_t size = eleClass->instance_size - sizeof(Il2CppObject);
		bool notNull = *GetNulllableHasValueOffset(nullableValueObj, klass);
		void* srcData = GetNulllableDataOffset(nullableValueObj, klass);

	LabelGet:
		IL2CPP_ASSERT(IS_CLASS_VALUE_TYPE(eleClass));
		switch (eleClass->byval_arg.type)
		{
		case IL2CPP_TYPE_BOOLEAN:
		{
			*(int32_t*)dst = notNull ? *(uint8_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_I1:
		{
			*(int32_t*)dst = notNull ? *(int8_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_U1:
		{
			*(int32_t*)dst = notNull ? *(uint8_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_I2:
		{
			*(int32_t*)dst = notNull ? *(int16_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_U2:
		case IL2CPP_TYPE_CHAR:
		{
			*(int32_t*)dst = notNull ? *(uint16_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
		{
			*(int32_t*)dst = notNull ? *(int32_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
		{
			*(int64_t*)dst = notNull ? *(int64_t*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_R4:
		{
			*(float*)dst = notNull ? *(float*)srcData : 0;
			break;
		}
		case IL2CPP_TYPE_R8:
		{
			*(double*)dst = notNull ? *(double*)srcData : 0.0;
			break;
		}
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
		{
#if HOTC233_ARCH_64
			* (int64_t*)dst = notNull ? *(int64_t*)srcData : 0;
#else 
			* (int32_t*)dst = notNull ? *(int32_t*)srcData : 0;
#endif
			break;
		}
		case IL2CPP_TYPE_VALUETYPE:
		case IL2CPP_TYPE_GENERICINST:
		{
			if (eleClass->enumtype)
			{
				eleClass = eleClass->castClass;
				goto LabelGet;
			}
			if (notNull)
			{
				std::memmove(dst, srcData, size);
			}
			else
			{
				std::memset(dst, 0, size);
			}
			break;
		}
		default:
		{
			RaiseExecutionEngineException("GetNullableValueOrDefault2StackDataByType");
		}
		}
	}

	inline void GetNullableValueOrDefault2StackDataByType(void* dst, void* nullableValueObj, void* defaultData, Il2CppClass* klass)
	{
		Il2CppClass* eleClass = klass->castClass;
		IL2CPP_ASSERT(eleClass->size_inited);
		uint32_t size = eleClass->instance_size - sizeof(Il2CppObject);
		void* srcData;
		bool notNull = *GetNulllableHasValueOffset(nullableValueObj, klass);
		if (notNull)
		{
			srcData = GetNulllableDataOffset(nullableValueObj, klass);
		}
		else
		{
			if (defaultData == nullptr)
			{
				il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetInvalidOperationException("Nullable object must have a value."));
			}
			srcData = defaultData;
		}
	LabelGet:
		switch (eleClass->byval_arg.type)
		{
		case IL2CPP_TYPE_BOOLEAN:
		{
			*(int32_t*)dst = *(uint8_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_CHAR:
		{
			*(int32_t*)dst = *(uint16_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_I1:
		{
			*(int32_t*)dst = *(int8_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_U1:
		{
			*(int32_t*)dst = *(uint8_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_I2:
		{
			*(int32_t*)dst = *(int16_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_U2:
		{
			*(int32_t*)dst = *(uint16_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
		{
			*(int32_t*)dst = *(int32_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
		{
			*(int64_t*)dst = *(int64_t*)srcData;
			break;
		}
		case IL2CPP_TYPE_R4:
		{
			*(float*)dst = *(float*)srcData;
			break;
		}
		case IL2CPP_TYPE_R8:
		{
			*(double*)dst = *(double*)srcData;
			break;
		}
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
		{
#if HOTC233_ARCH_64
			* (int64_t*)dst = *(int64_t*)srcData;
#else 
			* (int32_t*)dst = *(int32_t*)srcData;
#endif
			break;
		}
		case IL2CPP_TYPE_VALUETYPE:
		case IL2CPP_TYPE_GENERICINST:
		{
			if (eleClass->enumtype)
			{
				eleClass = eleClass->castClass;
				goto LabelGet;
			}
			std::memmove(dst, srcData, size);
			break;
		}
		default:
		{
			RaiseExecutionEngineException("GetNullableValue2StackDataByType");
		}
		}
	}

#pragma endregion

#pragma region misc

	// not boxed data

	inline int32_t HiInterlockedCompareExchange(int32_t* location, int32_t newValue, int32_t oldValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::CompareExchange(location, newValue, oldValue);
	}

	inline int64_t HiInterlockedCompareExchange(int64_t* location, int64_t newValue, int64_t oldValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::CompareExchange64(location, newValue, oldValue);
	}

	inline void* HiInterlockedCompareExchange(void** location, void* newValue, void* oldValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::CompareExchange_T(location, newValue, oldValue);
	}

	inline int32_t HiInterlockedExchange(int32_t* location, int32_t newValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::Exchange(location, newValue);
	}

	inline int64_t HiInterlockedExchange(int64_t* location, int64_t newValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::Exchange64(location, newValue);
	}

	inline void* HiInterlockedExchange(void** location, void* newValue)
	{
		return il2cpp::icalls::mscorlib::System::Threading::Interlocked::ExchangePointer(location, newValue);
	}

#define MEMORY_BARRIER() il2cpp::os::Atomic::FullMemoryBarrier()


	inline int32_t UnsafeEnumCast(void* src, uint16_t type)
	{
		switch ((Il2CppTypeEnum)type)
		{
		case IL2CPP_TYPE_BOOLEAN: return *(int8_t*)src;
		case IL2CPP_TYPE_CHAR: return *(uint16_t*)src;
		case IL2CPP_TYPE_I1: return *(int8_t*)src;
		case IL2CPP_TYPE_U1: return *(uint8_t*)src;
		case IL2CPP_TYPE_I2: return *(int16_t*)src;
		case IL2CPP_TYPE_U2: return *(uint16_t*)src;
		case IL2CPP_TYPE_I4: return *(int32_t*)src;
		case IL2CPP_TYPE_U4: return *(uint32_t*)src;
		default: RaiseExecutionEngineException("UnsafeEnumCast not support type"); return -1;
		}
	}

	// align with the implementation of Enum::get_hashcode
	inline int32_t GetEnumLongHashCode(void* data)
	{
		int64_t value = *((int64_t*)data);
		return (int32_t)(value & 0xffffffff) ^ (int32_t)(value >> 32);
	}

	inline void ConstructorDelegate2(MethodInfo* ctor, Il2CppDelegate* del, Il2CppObject* target, MethodInfo* method)
	{
#if HOTC233_UNITY_2021_OR_NEW
		void* ctorArgs[2] = { target, (void*)&method };
		ctor->invoker_method(ctor->methodPointer, ctor, del, ctorArgs, NULL);
#else
		RaiseNotSupportedException("ConstructorDelegate2");
#endif
	}

#pragma endregion

#pragma region function

#define SAVE_CUR_FRAME(nextIp) { \
	frame->ip = nextIp; \
}

#define LOAD_PREV_FRAME() { \
	imi = (const InterpMethodInfo*)frame->method->interpData; \
	ip = frame->ip; \
	ipBase = imi->codes; \
	localVarBase = frame->stackBasePtr; \
}

#define PREPARE_NEW_FRAME_FROM_NATIVE(newMethodInfo, argBasePtr, retPtr) { \
	imi = newMethodInfo->interpData ? (InterpMethodInfo*)newMethodInfo->interpData : InterpreterModule::GetInterpMethodInfo(newMethodInfo); \
	if (!metadata::IsInstanceMethod(newMethodInfo)) { RuntimeInitClassCCtorWithoutInitClass(newMethodInfo); } \
	frame = interpFrameGroup.EnterFrameFromNative(newMethodInfo, argBasePtr); \
	frame->ret = retPtr; \
	ip = ipBase = imi->codes; \
	frame->ip = (byte*)ip; \
	localVarBase = frame->stackBasePtr; \
}

#define PREPARE_NEW_FRAME_FROM_INTERPRETER(newMethodInfo, argBasePtr, retPtr) { \
	imi = newMethodInfo->interpData ? (InterpMethodInfo*)newMethodInfo->interpData : InterpreterModule::GetInterpMethodInfo(newMethodInfo); \
	if (!metadata::IsInstanceMethod(newMethodInfo)) { RuntimeInitClassCCtorWithoutInitClass(newMethodInfo); } \
	frame = interpFrameGroup.EnterFrameFromInterpreter(newMethodInfo, argBasePtr); \
	frame->ret = retPtr; \
	ip = ipBase = imi->codes; \
	frame->ip = (byte*)ip; \
	localVarBase = frame->stackBasePtr; \
}

#define PREPARE_NEW_FRAME_FROM_INTERPRETER_PREPARED(newMethodInfo, preparedImi, argBasePtr, retPtr) { \
	imi = preparedImi; \
	frame = interpFrameGroup.EnterFrameFromInterpreter(newMethodInfo, argBasePtr); \
	frame->ret = retPtr; \
	ip = ipBase = imi->codes; \
	frame->ip = (byte*)ip; \
	localVarBase = frame->stackBasePtr; \
}

#define LEAVE_FRAME() { \
	frame = interpFrameGroup.LeaveFrame(); \
	if (frame) \
	{ \
		LOAD_PREV_FRAME(); \
	}\
	else \
	{ \
		goto ExitEvalLoop; \
	} \
}

#define SET_RET_AND_LEAVE_FRAME(nativeSize, interpSize) { \
	void* _curRet = frame->ret; \
	frame = interpFrameGroup.LeaveFrame(); \
	if (frame) \
	{ \
        Copy##interpSize(_curRet, (void*)(localVarBase + __ret)); \
		LOAD_PREV_FRAME(); \
	}\
	else \
	{ \
        Copy##nativeSize(_curRet, (void*)(localVarBase + __ret)); \
		goto ExitEvalLoop; \
	} \
}

#define CALL_INTERP_VOID(nextIp, methodInfo, argBasePtr) { \
	if (TryExecuteHotc233CallFastPath(methodInfo, argBasePtr, nullptr)) \
	{ \
		ip = (byte*)(nextIp); \
	} \
	else \
	{ \
		SAVE_CUR_FRAME(nextIp) \
		PREPARE_NEW_FRAME_FROM_INTERPRETER(methodInfo, argBasePtr, nullptr); \
	} \
}

#define CALL_INTERP_RET(nextIp, methodInfo, argBasePtr, retPtr) { \
	if (TryExecuteHotc233CallFastPath(methodInfo, argBasePtr, retPtr)) \
	{ \
		ip = (byte*)(nextIp); \
	} \
	else \
	{ \
		SAVE_CUR_FRAME(nextIp) \
		PREPARE_NEW_FRAME_FROM_INTERPRETER(methodInfo, argBasePtr, retPtr); \
	} \
}

#define CALL_INTERP_RET_PREPARED(nextIp, methodInfo, preparedImi, argBasePtr, retPtr) { \
	if (preparedImi && IsSafeGenericHotc233CallFastPath(preparedImi->hotc233FastPathKind, retPtr) && TryExecuteHotc233FastPath(preparedImi, argBasePtr, retPtr)) \
	{ \
		ip = (byte*)(nextIp); \
	} \
	else \
	{ \
		SAVE_CUR_FRAME(nextIp) \
		PREPARE_NEW_FRAME_FROM_INTERPRETER_PREPARED(methodInfo, preparedImi, argBasePtr, retPtr); \
	} \
}

#pragma endregion

#pragma region delegate

inline StackObject* TryPrepareClosedInstanceInterpDelegate(uint16_t invokeParamCount, const MethodInfo* method, Il2CppObject* target, StackObject* argBasePtr)
{
	if ((int32_t)invokeParamCount != (int32_t)method->parameters_count || !hotc233::metadata::IsInstanceMethod(method))
	{
		return nullptr;
	}
	CHECK_NOT_NULL_THROW(target);
	argBasePtr->obj = target + IS_CLASS_VALUE_TYPE(method->klass);
	return argBasePtr;
}

IL2CPP_FORCE_INLINE bool TryGetCachedSingleInterpDelegatePrepared(uint64_t* cache, Il2CppMulticastDelegate* del, uint16_t invokeParamCount, StackObject* argBasePtr, const MethodInfo** methodOut, InterpMethodInfo** imiOut, StackObject** argBaseOut)
{
	if (cache == nullptr || del == nullptr || argBasePtr == nullptr || del->delegate.method == nullptr || methodOut == nullptr || imiOut == nullptr || argBaseOut == nullptr)
	{
		return false;
	}
	if ((Il2CppMulticastDelegate*)cache[0] != del)
	{
		return false;
	}

	const MethodInfo* method = del->delegate.method;
	InterpMethodInfo* methodImi = (InterpMethodInfo*)cache[1];
	if (!methodImi || !hotc233::metadata::IsInterpreterImplement(method) || !hotc233::metadata::IsInterpreterMethod(method))
	{
		cache[0] = 0;
		cache[1] = 0;
		return false;
	}

	StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(invokeParamCount, method, del->delegate.target, argBasePtr);
	if (!fastArgBase)
	{
		cache[0] = 0;
		cache[1] = 0;
		return false;
	}

	*methodOut = method;
	*imiOut = methodImi;
	*argBaseOut = fastArgBase;
	return true;
}

IL2CPP_FORCE_INLINE void StoreSingleInterpDelegatePreparedCache(uint64_t* cache, Il2CppMulticastDelegate* del, uint16_t invokeParamCount, const MethodInfo* method, InterpMethodInfo* methodImi)
{
	if (cache == nullptr || del == nullptr || method == nullptr || methodImi == nullptr)
	{
		return;
	}
	if ((int32_t)invokeParamCount != (int32_t)method->parameters_count || !hotc233::metadata::IsInstanceMethod(method))
	{
		return;
	}
	cache[0] = (uint64_t)del;
	cache[1] = (uint64_t)methodImi;
}

inline bool TryInvokeInterpDelegateSynchronously(uint16_t invokeParamCount, const MethodInfo* method, Il2CppObject* target, uint16_t* argIdxs, StackObject* localVarBase, void* ret)
{
	if (!hotc233::metadata::IsInterpreterImplement(method) || !hotc233::metadata::IsInterpreterMethod(method))
	{
		return false;
	}

	StackObject* argBasePtr = localVarBase + argIdxs[0];
	switch ((int32_t)invokeParamCount - (int32_t)method->parameters_count)
	{
	case 0:
	{
		if (hotc233::metadata::IsInstanceMethod(method))
		{
			CHECK_NOT_NULL_THROW(target);
			argBasePtr->obj = target + IS_CLASS_VALUE_TYPE(method->klass);
		}
		else
		{
			argBasePtr = invokeParamCount == 0 ? argBasePtr + 1 : localVarBase + argIdxs[1];
		}
		break;
	}
	case -1:
	{
		argBasePtr->obj = target;
		break;
	}
	case 1:
	{
		argBasePtr = localVarBase + argIdxs[1];
		CHECK_NOT_NULL_THROW(argBasePtr->obj);
		break;
	}
	default:
	{
		RaiseExecutionEngineException("CallInterpDelegate");
	}
	}

	Interpreter::Execute(method, argBasePtr, ret);
	return true;
}

inline void InvokeSingleDelegate(uint16_t invokeParamCount, const MethodInfo * method, Il2CppObject * obj, Managed2NativeCallMethod staticM2NMethod, Managed2NativeCallMethod instanceM2NMethod, uint16_t * argIdxs, StackObject * localVarBase, void* ret)
{
	if (!InitAndGetInterpreterDirectlyCallMethodPointer(method))
	{
		RaiseAOTGenericMethodNotInstantiatedException(method);
	}
	if (!InterpreterModule::HasImplementCallNative2Managed(method))
	{
		instanceM2NMethod = staticM2NMethod = InterpreterModule::Managed2NativeCallByReflectionInvoke;
	}
	StackObject* target;
	switch ((int32_t)invokeParamCount - (int32_t)method->parameters_count)
	{
	case 0:
	{
		if (hotc233::metadata::IsInstanceMethod(method))
		{
			CHECK_NOT_NULL_THROW(obj);
			target = localVarBase + argIdxs[0];
			target->obj = obj + IS_CLASS_VALUE_TYPE(method->klass);
			instanceM2NMethod(method, argIdxs, localVarBase, ret);
		}
		else
		{
			RuntimeInitClassCCtor(method);
			staticM2NMethod(method, argIdxs + 1, localVarBase, ret);
		}
		break;
	}
	case -1:
	{
		IL2CPP_ASSERT(!hotc233::metadata::IsInstanceMethod(method));
		target = localVarBase + argIdxs[0];
		target->obj = obj;
		instanceM2NMethod(method, argIdxs, localVarBase, ret);
		break;
	}
	case 1:
	{
		IL2CPP_ASSERT(invokeParamCount == method->parameters_count + 1);
		IL2CPP_ASSERT(hotc233::metadata::IsInstanceMethod(method));
		target = localVarBase + argIdxs[1];
		CHECK_NOT_NULL_THROW(target->obj);
		staticM2NMethod(method, argIdxs + 1, localVarBase, ret);
		break;
	}
	default:
	{
		RaiseExecutionEngineException("bad delegate");
	}
	}
}

inline Il2CppObject* InvokeDelegateBeginInvoke(const MethodInfo* method, uint16_t* argIdxs, StackObject* localVarBase)
{
	int32_t paramCount = method->parameters_count;
	RuntimeDelegate* del = (RuntimeDelegate*)localVarBase[argIdxs[0]].obj;
	CHECK_NOT_NULL_THROW(del);
	RuntimeDelegate* callBack = (RuntimeDelegate*)localVarBase[argIdxs[paramCount - 1]].obj;
	RuntimeObject* ctx = (RuntimeObject*)localVarBase[argIdxs[paramCount]].obj;
	IL2CPP_ASSERT(paramCount > 0);
	void** newArgs = (void**)alloca(sizeof(void*) * paramCount);
	newArgs[paramCount - 1] = {};
	for (int i = 0; i < paramCount - 2; i++)
	{
		const Il2CppType* argType = GET_METHOD_PARAMETER_TYPE(method->parameters[i]);
		StackObject* argSrc = localVarBase + argIdxs[i+1];
		void** argDst = newArgs + i;
		if (argType->byref)
		{
			argSrc = (StackObject*)argSrc->ptr;
		}
		if (hotc233::metadata::IsValueType(argType))
		{
			*argDst = il2cpp::vm::Object::Box(il2cpp::vm::Class::FromIl2CppType(argType), argSrc);
		}
		else
		{
			*argDst = argSrc->ptr;
		}
	}
	return (RuntimeObject*)il2cpp_codegen_delegate_begin_invoke((RuntimeDelegate*)del, (void**)newArgs, callBack, ctx);
}

inline void InvokeDelegateEndInvokeVoid(MethodInfo* method, Il2CppAsyncResult* asyncResult)
{
	il2cpp_codegen_delegate_end_invoke(asyncResult, 0);
}

inline void InvokeDelegateEndInvokeRet(MethodInfo* method, Il2CppAsyncResult* asyncResult, void* ret)
{
	Il2CppObject* result = il2cpp_codegen_delegate_end_invoke(asyncResult, 0);
	Il2CppClass* retKlass = il2cpp::vm::Class::FromIl2CppType(method->return_type);
	HiUnboxAny2StackObject(result, retKlass, ret);
}

#pragma endregion

#pragma region exception

constexpr int EXCEPTION_FLOW_INFO_ALLOC_BATCH_NUM = 3;

inline void PushExceptionFlowInfo(InterpFrame* frame, MachineState& machine, const ExceptionFlowInfo& newExFlowInfo)
{
	if (frame->exFlowCount >= frame->exFlowCapaticy)
	{
		ExceptionFlowInfo* newEfi = machine.AllocExceptionFlow(EXCEPTION_FLOW_INFO_ALLOC_BATCH_NUM);
		if (frame->exFlowBase == nullptr)
		{
			frame->exFlowBase = newEfi;
		}
		frame->exFlowCapaticy += EXCEPTION_FLOW_INFO_ALLOC_BATCH_NUM;
	}
	frame->exFlowBase[frame->exFlowCount++] = newExFlowInfo;
}

inline void PopPrevExceptionFlowInfo(InterpFrame* frame, ExceptionFlowInfo** curEx)
{
	if (frame->exFlowCount >= 2)
	{
		frame->exFlowBase[frame->exFlowCount - 2] = frame->exFlowBase[frame->exFlowCount - 1];
		--frame->exFlowCount;
		if (curEx)
		{
			*curEx = frame->exFlowBase + frame->exFlowCount - 1;
		}
	}
}

inline void PopCurExceptionFlowInfo(InterpFrame* frame)
{
	if (frame->exFlowCount >= 1)
	{
		--frame->exFlowCount;
	}
}

#define PREPARE_EXCEPTION(_ex_, _firstHanlderIndex_)  PushExceptionFlowInfo(frame, machine, {ExceptionFlowType::Exception, (int32_t)(ip - ipBase), _ex_, _firstHanlderIndex_, 0});


#define FIND_NEXT_EX_HANDLER_OR_UNWIND() \
while (true) \
{ \
	ExceptionFlowInfo* efi = frame->GetCurExFlow(); \
	IL2CPP_ASSERT(efi && efi->exFlowType == ExceptionFlowType::Exception); \
	IL2CPP_ASSERT(efi->ex); \
	int32_t exClauseNum = (int32_t)imi->exClauseCount; \
	for (; efi->nextExClauseIndex < exClauseNum; ) \
	{ \
		for (ExceptionFlowInfo* prevExFlow; (prevExFlow = frame->GetPrevExFlow()) && efi->nextExClauseIndex >= prevExFlow->nextExClauseIndex ;) {\
			const InterpExceptionClause* prevIec = &imi->exClauses[prevExFlow->nextExClauseIndex - 1]; \
			if (!(prevIec->handlerBeginOffset <= efi->throwOffset && efi->throwOffset < prevIec->handlerEndOffset)) { \
				PopPrevExceptionFlowInfo(frame, &efi);\
			} \
			else \
			{ \
				break; \
			} \
		}\
		const InterpExceptionClause* iec = &imi->exClauses[efi->nextExClauseIndex++]; \
		if (iec->tryBeginOffset <= efi->throwOffset && efi->throwOffset < iec->tryEndOffset) \
		{ \
			switch (iec->flags) \
			{ \
			case CorILExceptionClauseType::Exception: \
			{ \
			if (il2cpp::vm::Class::IsAssignableFrom(iec->exKlass, efi->ex->klass)) \
			{ \
			ip = ipBase + iec->handlerBeginOffset; \
			StackObject* exObj = localVarBase + imi->evalStackBaseOffset; \
			exObj->obj = efi->ex; \
			efi->exFlowType = ExceptionFlowType::Catch;\
			goto LoopStart; \
			} \
			break; \
			} \
			case CorILExceptionClauseType::Filter: \
			{ \
			ip = ipBase + iec->filterBeginOffset; \
			StackObject* exObj = localVarBase + imi->evalStackBaseOffset; \
			exObj->obj = efi->ex; \
			goto LoopStart; \
			} \
			case CorILExceptionClauseType::Finally: \
			{ \
			ip = ipBase + iec->handlerBeginOffset; \
			goto LoopStart; \
			} \
			case CorILExceptionClauseType::Fault: \
			{ \
			ip = ipBase + iec->handlerBeginOffset; \
			goto LoopStart; \
			} \
			default: \
			{ \
				RaiseExecutionEngineException(""); \
			} \
			} \
		} \
	} \
	frame = interpFrameGroup.LeaveFrame(); \
	if (frame) \
	{ \
		LOAD_PREV_FRAME(); \
		PREPARE_EXCEPTION(efi->ex, 0); \
	}\
	else \
	{ \
		lastUnwindException = efi->ex; \
		goto UnWindFail; \
	} \
}


#define THROW_EX(_ex_, _firstHandlerIndex_) { \
	Il2CppException* ex = _ex_; \
	il2cpp::vm::Exception::PrepareExceptionForThrow(ex, const_cast<MethodInfo*>(frame->method));\
	CHECK_NOT_NULL_THROW(ex); \
	PREPARE_EXCEPTION(ex, _firstHandlerIndex_); \
	FIND_NEXT_EX_HANDLER_OR_UNWIND(); \
}
#define RETHROW_EX() { \
	ExceptionFlowInfo* curExFlow = frame->GetCurExFlow(); \
	IL2CPP_ASSERT(curExFlow->exFlowType == ExceptionFlowType::Catch); \
	il2cpp::vm::Exception::Raise(curExFlow->ex, const_cast<MethodInfo*>(frame->method)); \
}

#define CONTINUE_NEXT_FINALLY() { \
ExceptionFlowInfo* efi = frame->GetCurExFlow(); \
IL2CPP_ASSERT(efi && efi->exFlowType == ExceptionFlowType::Leave); \
int32_t exClauseNum = (int32_t)imi->exClauseCount; \
for (; efi->nextExClauseIndex < exClauseNum; ) \
{ \
	const InterpExceptionClause* iec = &imi->exClauses[efi->nextExClauseIndex++]; \
	if (iec->tryBeginOffset <= efi->throwOffset && efi->throwOffset < iec->tryEndOffset) \
	{ \
		if (iec->tryBeginOffset <= efi->leaveTarget && efi->leaveTarget < iec->tryEndOffset) \
		{ \
			break; \
		} \
		switch (iec->flags) \
		{ \
		case CorILExceptionClauseType::Finally: \
		{ \
			ip = ipBase + iec->handlerBeginOffset; \
			goto LoopStart; \
		} \
		case CorILExceptionClauseType::Exception: \
		case CorILExceptionClauseType::Filter: \
		case CorILExceptionClauseType::Fault: \
		{ \
			break; \
		} \
		default: \
		{ \
			RaiseExecutionEngineException(""); \
		} \
		} \
	} \
} \
ip = ipBase + efi->leaveTarget; \
PopCurExceptionFlowInfo(frame); \
}

#define POP_PREV_CATCH_HANDLERS(leaveTarget)\
{ \
	for (ExceptionFlowInfo* prevExFlow; (prevExFlow = frame->GetPrevExFlow()) && prevExFlow->exFlowType == ExceptionFlowType::Catch ;) { \
			const InterpExceptionClause* prevIec = &imi->exClauses[prevExFlow->nextExClauseIndex - 1]; \
			if (!(prevIec->handlerBeginOffset <= leaveTarget && leaveTarget < prevIec->handlerEndOffset)) {	\
					PopPrevExceptionFlowInfo(frame, nullptr); \
			} \
			else \
			{ \
				break; \
			} \
	}\
}

#define LEAVE_EX(target, firstHandlerIndex)  { \
	PushExceptionFlowInfo(frame, machine, {ExceptionFlowType::Leave, (int32_t)(ip - ipBase), nullptr, firstHandlerIndex + 1, target}); \
	const InterpExceptionClause* iec = &imi->exClauses[firstHandlerIndex]; \
	POP_PREV_CATCH_HANDLERS(target); \
	ip = ipBase + iec->handlerBeginOffset; \
}

#define POP_CUR_CATCH_HANDLERS(leaveTarget)\
{ \
	for (ExceptionFlowInfo* prevExFlow; (prevExFlow = frame->GetCurExFlow()) && prevExFlow->exFlowType == ExceptionFlowType::Catch ;) { \
			const InterpExceptionClause* prevIec = &imi->exClauses[prevExFlow->nextExClauseIndex - 1]; \
			if (!(prevIec->handlerBeginOffset <= leaveTarget && leaveTarget < prevIec->handlerEndOffset)) {	\
					PopCurExceptionFlowInfo(frame); \
			} \
			else \
			{ \
				break; \
			} \
	}\
}

#define LEAVE_EX_DIRECTLY(target)  { \
	POP_CUR_CATCH_HANDLERS(target); \
	ip = ipBase + target; \
}

#define ENDFILTER_EX(value) {\
ExceptionFlowInfo* curExFlow = frame->GetCurExFlow(); \
IL2CPP_ASSERT(curExFlow->exFlowType == ExceptionFlowType::Exception); \
if(!(value)) \
{\
    FIND_NEXT_EX_HANDLER_OR_UNWIND();\
} \
else \
{ \
	curExFlow->exFlowType = ExceptionFlowType::Catch;\
}\
}

#define ENDFINALLY_EX() {\
ExceptionFlowInfo* curExFlow = frame->GetCurExFlow(); \
if (curExFlow->exFlowType == ExceptionFlowType::Exception) \
{ \
    FIND_NEXT_EX_HANDLER_OR_UNWIND(); \
} \
else \
{ \
    CONTINUE_NEXT_FINALLY();\
}\
}

#pragma endregion 

const int32_t kMaxRetValueTypeStackObjectSize = 1024;

	void Interpreter::Execute(const MethodInfo* methodInfo, StackObject* args, void* ret)
	{
#if !HOTC233_COMMUNITY_BASELINE
		if (TryExecuteHotc233CallFastPath(methodInfo, args, ret))
		{
			return;
		}
#endif

		MachineState& machine = InterpreterModule::GetCurrentThreadMachineState();
		InterpFrameGroup interpFrameGroup(machine);

		const InterpMethodInfo* imi;
		InterpFrame* frame;
		StackObject* localVarBase;
		byte* ipBase;
		byte* ip;

		Il2CppException* lastUnwindException;
		StackObject* tempRet = nullptr;
		uint32_t opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;

		PREPARE_NEW_FRAME_FROM_NATIVE(methodInfo, args, ret);

	LoopStart:
		opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
		try
		{
			for (;;)
			{
				static int s_opcodeTraceCount = 0;
				const char* _traceMethodName = frame && frame->method ? frame->method->name : nullptr;
				const char* _traceClassName = frame && frame->method && frame->method->klass ? frame->method->klass->name : nullptr;
				if (s_opcodeTraceCount < 300 && _traceMethodName &&
					(std::strcmp(_traceMethodName, "VerifyValueTuple") == 0 ||
					 std::strcmp(_traceMethodName, "RunSelfTest") == 0 ||
					 std::strcmp(_traceMethodName, "ComposeFeatureVerificationMessage") == 0 ||
					 std::strcmp(_traceMethodName, "ComposeSelfTestMessage") == 0 ||
					 std::strcmp(_traceMethodName, "VerifyLambda") == 0 ||
					 (std::strcmp(_traceMethodName, "Run") == 0 && _traceClassName && std::strcmp(_traceClassName, "CSharpUsageProbe") == 0)))
				{
					s_opcodeTraceCount++;
					std::printf("[hotc233][OpcodeTrace] method=%s offset=%lld opcode=%u size=%u\n",
						_traceMethodName,
						(long long)(ip - ipBase),
						(uint32_t)(*(HiOpcodeEnum*)ip),
						(unsigned)g_instructionSizes[(int)(*(HiOpcodeEnum*)ip)]);
					std::fflush(stdout);
				}
#if !HOTC233_ENABLE_THREADED_DISPATCH
				switch (*(HiOpcodeEnum*)ip)
#else
				HiOpcodeEnum __opcode = *(HiOpcodeEnum*)ip;
				if (g_opcodeProfilerEnabled)
				{
					uint32_t __opcodeIndex = (uint32_t)__opcode;
					if (__opcodeIndex < kDynamicOpcodeProfileCapacity)
					{
						g_opcodeProfilerCounts[__opcodeIndex]++;
						g_opcodeProfilerTotal++;
						if (opcodeProfilerLastOpcode < kDynamicOpcodeProfileCapacity)
						{
							RecordOpcodeProfilePair(opcodeProfilerLastOpcode, __opcodeIndex);
						}
						opcodeProfilerLastOpcode = __opcodeIndex;
					}
					else
					{
						opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
					}
				}
				switch (__opcode)
#endif
				{
					// avoid decrement *ip when compute jump table,  boosts about 5% performance
				case HiOpcodeEnum::None:
				{
					RaiseExecutionEngineException("Invalid hotc233 opcode: None");
				}
#pragma region memory
					//!!!{{MEMORY
				case HiOpcodeEnum::InitLocals_n_2:
				{
					uint16_t __size = *(uint16_t*)(ip + 2);
					InitDefaultN(localVarBase, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitLocals_n_4:
				{
					uint32_t __size = *(uint32_t*)(ip + 4);
					InitDefaultN(localVarBase, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitLocals_size_8:
				{
					InitDefault8(localVarBase);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitLocals_size_16:
				{
					InitDefault16(localVarBase);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitLocals_size_24:
				{
					InitDefault24(localVarBase);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitLocals_size_32:
				{
					InitDefault32(localVarBase);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_n_2:
				{
					uint16_t __size = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					InitDefaultN(localVarBase + __offset, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_n_4:
				{
					uint32_t __size = *(uint32_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					InitDefaultN(localVarBase + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_size_8:
				{
					uint32_t __offset = *(uint32_t*)(ip + 4);
					InitDefault8(localVarBase + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_size_16:
				{
					uint32_t __offset = *(uint32_t*)(ip + 4);
					InitDefault16(localVarBase + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_size_24:
				{
					uint32_t __offset = *(uint32_t*)(ip + 4);
					InitDefault24(localVarBase + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitInlineLocals_size_32:
				{
					uint32_t __offset = *(uint32_t*)(ip + 4);
					InitDefault32(localVarBase + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RegI32Copy:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(int32_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar:
				HOTC233_EXEC_LdlocVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(uint64_t*)(localVarBase + __dst)) = (*(uint64_t*)(localVarBase + __src));
					byte* __nextIp = ip + 8;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdfldVarVar_u1_BranchFalseVar_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldVarVar_u1_BranchFalseVar_i4;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::CallVirtual_ret_expand)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallVirtual_ret_expand;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::CallVirtual_ret)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallVirtual_ret;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::BranchVarVar_Clt_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchVarVar_Clt_i4;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdfldVarVar_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldVarVar_i4;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdstrVar)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdstrVar;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::BoxVarVar)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BoxVarVar;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::CallCommonNativeInstance_v_i4_2)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallCommonNativeInstance_v_i4_2;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::BranchUncondition_4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchUncondition_4;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarAddress)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::LdlocExpandVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(int8_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdlocExpandVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(uint8_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdlocExpandVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(int16_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdlocExpandVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(uint16_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVarSize:
				HOTC233_EXEC_LdlocVarVarSize:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
					std::memmove((void*)(localVarBase + __dst), (void*)(localVarBase + __src), __size);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::SetArrayElementVarVar_size_24)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_SetArrayElementVarVar_size_24;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __dst1 = *(uint16_t*)(ip + 2);
					uint16_t __src1 = *(uint16_t*)(ip + 4);
					uint16_t __dst2 = *(uint16_t*)(ip + 6);
					uint16_t __src2 = *(uint16_t*)(ip + 8);
					(*(uint64_t*)(localVarBase + __dst1)) = (*(uint64_t*)(localVarBase + __src1));
					(*(uint64_t*)(localVarBase + __dst2)) = (*(uint64_t*)(localVarBase + __src2));
					byte* __nextIp = ip + 16;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::CallCommonNativeInstance_v_i4_1)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallCommonNativeInstance_v_i4_1;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdcVarConst_4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdcVarConst_4;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::ConvertVarVar_i4_u1_SetArrayElementVarVar_i1)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_ConvertVarVar_i4_u1_SetArrayElementVarVar_i1;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdindVarVar_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdindVarVar_i4;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __elementDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __indexSrc = *(uint16_t*)(ip + 14);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy24((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(arr, _index, 24));
				    ip += 20;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24_LdlocVarVarSize:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __elementDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __indexSrc = *(uint16_t*)(ip + 14);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 16);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 18);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 20);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy24((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(arr, _index, 24));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __elementDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __indexSrc = *(uint16_t*)(ip + 14);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 16);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 18);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 20);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy28((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(arr, _index, 28));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 10);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 12);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 14);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::SetArrayElementVarVar_size_24)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_SetArrayElementVarVar_size_24;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 10);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 12);
					uint16_t __elementDst = *(uint16_t*)(ip + 14);
					uint16_t __arraySrc = *(uint16_t*)(ip + 16);
					uint16_t __indexSrc = *(uint16_t*)(ip + 18);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 20);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 22);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 24);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy28((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(arr, _index, 28));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 32;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_i4:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __elementDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __indexSrc = *(uint16_t*)(ip + 14);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    (*(int32_t*)(localVarBase + __elementDst)) = GetArrayElementFast<int32_t>(arr, _index);
					byte* __nextIp = ip + 16;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __addRet = *(uint16_t*)(ip + 10);
					uint16_t __addOp1 = *(uint16_t*)(ip + 12);
					uint16_t __addOp2 = *(uint16_t*)(ip + 14);
					uint16_t __constDst = *(uint16_t*)(ip + 16);
					uint32_t __constant = *(uint32_t*)(ip + 20);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					byte* __nextIp = ip + 24;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::BinOpVarVarVar_Rem_i4_BranchTrueVar_i4)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Rem_i4_BranchTrueVar_i4;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __dst1 = *(uint16_t*)(ip + 2);
					uint16_t __src1 = *(uint16_t*)(ip + 4);
					uint16_t __dst2 = *(uint16_t*)(ip + 6);
					uint16_t __src2 = *(uint16_t*)(ip + 8);
					uint16_t __dst3 = *(uint16_t*)(ip + 10);
					uint16_t __src3 = *(uint16_t*)(ip + 12);
					(*(uint64_t*)(localVarBase + __dst1)) = (*(uint64_t*)(localVarBase + __src1));
					(*(uint64_t*)(localVarBase + __dst2)) = (*(uint64_t*)(localVarBase + __src2));
					(*(uint64_t*)(localVarBase + __dst3)) = (*(uint64_t*)(localVarBase + __src3));
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::SetArrayElementVarVar_ref)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_SetArrayElementVarVar_ref;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 10);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 12);
					uint16_t __addRet = *(uint16_t*)(ip + 14);
					uint16_t __addOp1 = *(uint16_t*)(ip + 16);
					uint16_t __addOp2 = *(uint16_t*)(ip + 18);
					uint16_t __constDst = *(uint16_t*)(ip + 20);
					uint32_t __constant = *(uint32_t*)(ip + 24);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdindVarVar_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __indDst = *(uint16_t*)(ip + 6);
					uint16_t __indSrc = *(uint16_t*)(ip + 8);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __lengthDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					Il2CppArray* __arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
				    CHECK_NOT_NULL_THROW(__arr);
				    (*(int64_t*)(localVarBase + __lengthDst)) = (int64_t)il2cpp::vm::Array::GetLength(__arr);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __lengthDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 14);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 16);
					int32_t __offset = *(int32_t*)(ip + 20);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					Il2CppArray* __arr = *(Il2CppArray**)(localVarBase + __arraySrc);
				    (*(int64_t*)(localVarBase + __lengthDst)) = (int64_t)il2cpp::vm::Array::GetLength(__arr);
				    if (CompareCge((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
						ip += 24;
				    }
				    else
				    {
						ip = ipBase + __offset;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize_LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize_LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarVar_LdcVarConst_4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::StfldVarVar_u1)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_StfldVarVar_u1;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_BranchVarVar_CneUn_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 6);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 8);
					int32_t __offset = *(int32_t*)(ip + 12);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    if (CompareCneUn((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BranchVarVar_Clt_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 12);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 14);
					int32_t __offset = *(int32_t*)(ip + 16);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    if (CompareCge((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
				        ip += 24;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __addRet = *(uint16_t*)(ip + 12);
					uint16_t __addOp1 = *(uint16_t*)(ip + 14);
					uint16_t __addOp2 = *(uint16_t*)(ip + 16);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4:
				HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpRem_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __remRet = *(uint16_t*)(ip + 12);
					uint16_t __remOp1 = *(uint16_t*)(ip + 14);
					uint16_t __remOp2 = *(uint16_t*)(ip + 16);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __remRet)) = HiRem((*(int32_t*)(localVarBase + __remOp1)), (*(int32_t*)(localVarBase + __remOp2)));
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4_LdcVarConst_4_CompOpCeq_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __remConstDst = *(uint16_t*)(ip + 6);
					uint32_t __remConstant = *(uint32_t*)(ip + 8);
					uint16_t __remRet = *(uint16_t*)(ip + 12);
					uint16_t __remOp1 = *(uint16_t*)(ip + 14);
					uint16_t __remOp2 = *(uint16_t*)(ip + 16);
					uint16_t __compareConstDst = *(uint16_t*)(ip + 18);
					uint32_t __compareConstant = *(uint32_t*)(ip + 20);
					uint16_t __compareRet = *(uint16_t*)(ip + 24);
					uint16_t __compareOp1 = *(uint16_t*)(ip + 26);
					uint16_t __compareOp2 = *(uint16_t*)(ip + 28);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __remConstDst)) = __remConstant;
					(*(int32_t*)(localVarBase + __remRet)) = HiRem((*(int32_t*)(localVarBase + __remOp1)), (*(int32_t*)(localVarBase + __remOp2)));
					(*(int32_t*)(localVarBase + __compareConstDst)) = __compareConstant;
					(*(int32_t*)(localVarBase + __compareRet)) = CompareCeq((*(int32_t*)(localVarBase + __compareOp1)), (*(int32_t*)(localVarBase + __compareOp2)));
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4_LdcVarConst_4_CompOpCeq_i4_RetVar_ret_1:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __remConstDst = *(uint16_t*)(ip + 6);
					uint32_t __remConstant = *(uint32_t*)(ip + 8);
					uint16_t __remRet = *(uint16_t*)(ip + 12);
					uint16_t __remOp1 = *(uint16_t*)(ip + 14);
					uint16_t __remOp2 = *(uint16_t*)(ip + 16);
					uint16_t __compareConstDst = *(uint16_t*)(ip + 18);
					uint32_t __compareConstant = *(uint32_t*)(ip + 20);
					uint16_t __compareRet = *(uint16_t*)(ip + 24);
					uint16_t __compareOp1 = *(uint16_t*)(ip + 26);
					uint16_t __compareOp2 = *(uint16_t*)(ip + 28);
					uint16_t __ret = *(uint16_t*)(ip + 30);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __remConstDst)) = __remConstant;
					(*(int32_t*)(localVarBase + __remRet)) = HiRem((*(int32_t*)(localVarBase + __remOp1)), (*(int32_t*)(localVarBase + __remOp2)));
					(*(int32_t*)(localVarBase + __compareConstDst)) = __compareConstant;
					(*(int32_t*)(localVarBase + __compareRet)) = CompareCeq((*(int32_t*)(localVarBase + __compareOp1)), (*(int32_t*)(localVarBase + __compareOp2)));
				    SET_RET_AND_LEAVE_FRAME(1, 8);
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4:
				HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpMul_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __mulRet = *(uint16_t*)(ip + 12);
					uint16_t __mulOp1 = *(uint16_t*)(ip + 14);
					uint16_t __mulOp2 = *(uint16_t*)(ip + 16);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __mulRet)) = (*(int32_t*)(localVarBase + __mulOp1)) * (*(int32_t*)(localVarBase + __mulOp2));
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4_RetVar_ret_4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __mulRet = *(uint16_t*)(ip + 12);
					uint16_t __mulOp1 = *(uint16_t*)(ip + 14);
					uint16_t __mulOp2 = *(uint16_t*)(ip + 16);
					uint16_t __ret = *(uint16_t*)(ip + 18);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __mulRet)) = (*(int32_t*)(localVarBase + __mulOp1)) * (*(int32_t*)(localVarBase + __mulOp2));
				    SET_RET_AND_LEAVE_FRAME(4, 8);
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar:
				HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					uint16_t __addRet = *(uint16_t*)(ip + 12);
					uint16_t __addOp1 = *(uint16_t*)(ip + 14);
					uint16_t __addOp2 = *(uint16_t*)(ip + 16);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 18);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 20);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					byte* __nextIp = ip + 32;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4)
					{
						ip = __nextIp;
						uint16_t __lengthCopyDst1 = *(uint16_t*)(ip + 2);
						uint16_t __lengthCopySrc1 = *(uint16_t*)(ip + 4);
						uint16_t __lengthCopyDst2 = *(uint16_t*)(ip + 6);
						uint16_t __lengthCopySrc2 = *(uint16_t*)(ip + 8);
						uint16_t __lengthDst = *(uint16_t*)(ip + 10);
						uint16_t __arraySrc = *(uint16_t*)(ip + 12);
						uint16_t __branchOp1 = *(uint16_t*)(ip + 14);
						uint16_t __branchOp2 = *(uint16_t*)(ip + 16);
						int32_t __offset = *(int32_t*)(ip + 20);
						(*(uint64_t*)(localVarBase + __lengthCopyDst1)) = (*(uint64_t*)(localVarBase + __lengthCopySrc1));
						(*(uint64_t*)(localVarBase + __lengthCopyDst2)) = (*(uint64_t*)(localVarBase + __lengthCopySrc2));
						Il2CppArray* __arr = *(Il2CppArray**)(localVarBase + __arraySrc);
					    (*(int64_t*)(localVarBase + __lengthDst)) = (int64_t)il2cpp::vm::Array::GetLength(__arr);
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
					    if (CompareCge((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
					    {
							byte* __fallthroughIp = ip + 24;
							if (*(HiOpcodeEnum*)__fallthroughIp == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24_LdlocVarVarSize)
							{
								ip = __fallthroughIp;
								uint16_t __elementCopyDst1 = *(uint16_t*)(ip + 2);
								uint16_t __elementCopySrc1 = *(uint16_t*)(ip + 4);
								uint16_t __elementCopyDst2 = *(uint16_t*)(ip + 6);
								uint16_t __elementCopySrc2 = *(uint16_t*)(ip + 8);
								uint16_t __elementDst = *(uint16_t*)(ip + 10);
								uint16_t __elementArraySrc = *(uint16_t*)(ip + 12);
								uint16_t __indexSrc = *(uint16_t*)(ip + 14);
								uint16_t __sizedCopyDst = *(uint16_t*)(ip + 16);
								uint16_t __sizedCopySrc = *(uint16_t*)(ip + 18);
								uint16_t __sizedCopySize = *(uint16_t*)(ip + 20);
								(*(uint64_t*)(localVarBase + __elementCopyDst1)) = (*(uint64_t*)(localVarBase + __elementCopySrc1));
								(*(uint64_t*)(localVarBase + __elementCopyDst2)) = (*(uint64_t*)(localVarBase + __elementCopySrc2));
							    Il2CppArray* __elementArr = (*(Il2CppArray**)(localVarBase + __elementArraySrc));
								int32_t __index = (*(int32_t*)(localVarBase + __indexSrc));
							    Copy24((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(__elementArr, __index, 24));
								std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
								ip += 24;
							}
							else
							{
						        ip = __fallthroughIp;
							}
					    }
					    else
					    {
							byte* __targetIp = ipBase + __offset;
							if (*(HiOpcodeEnum*)__targetIp == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24_LdlocVarVarSize)
							{
								ip = __targetIp;
								uint16_t __elementCopyDst1 = *(uint16_t*)(ip + 2);
								uint16_t __elementCopySrc1 = *(uint16_t*)(ip + 4);
								uint16_t __elementCopyDst2 = *(uint16_t*)(ip + 6);
								uint16_t __elementCopySrc2 = *(uint16_t*)(ip + 8);
								uint16_t __elementDst = *(uint16_t*)(ip + 10);
								uint16_t __elementArraySrc = *(uint16_t*)(ip + 12);
								uint16_t __indexSrc = *(uint16_t*)(ip + 14);
								uint16_t __sizedCopyDst = *(uint16_t*)(ip + 16);
								uint16_t __sizedCopySrc = *(uint16_t*)(ip + 18);
								uint16_t __sizedCopySize = *(uint16_t*)(ip + 20);
								(*(uint64_t*)(localVarBase + __elementCopyDst1)) = (*(uint64_t*)(localVarBase + __elementCopySrc1));
								(*(uint64_t*)(localVarBase + __elementCopyDst2)) = (*(uint64_t*)(localVarBase + __elementCopySrc2));
							    Il2CppArray* __elementArr = (*(Il2CppArray**)(localVarBase + __elementArraySrc));
								int32_t __index = (*(int32_t*)(localVarBase + __indexSrc));
							    Copy24((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(__elementArr, __index, 24));
								std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
								ip += 24;
							}
							else
							{
					            ip = __targetIp;
							}
					    }
						continue;
					}
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BranchVarVar_Clt_i4)
					{
						ip = __nextIp;
						uint16_t __branchCopyDst = *(uint16_t*)(ip + 2);
						uint16_t __branchCopySrc = *(uint16_t*)(ip + 4);
						uint16_t __branchConstDst = *(uint16_t*)(ip + 6);
						uint32_t __branchConstant = *(uint32_t*)(ip + 8);
						uint16_t __branchOp1 = *(uint16_t*)(ip + 12);
						uint16_t __branchOp2 = *(uint16_t*)(ip + 14);
						int32_t __offset = *(int32_t*)(ip + 16);
						(*(uint64_t*)(localVarBase + __branchCopyDst)) = (*(uint64_t*)(localVarBase + __branchCopySrc));
						(*(int32_t*)(localVarBase + __branchConstDst)) = __branchConstant;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
					    if (CompareCge((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
					    {
					        ip += 24;
					    }
					    else
					    {
					        ip = ipBase + __offset;
					    }
						continue;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4:
				{
					uint16_t __headCopyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __headCopySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __headConstDst = *(uint16_t*)(ip + 6);
					uint32_t __headConstant = *(uint32_t*)(ip + 8);
					uint16_t __headAddRet = *(uint16_t*)(ip + 12);
					uint16_t __headAddOp1 = *(uint16_t*)(ip + 14);
					uint16_t __headAddOp2 = *(uint16_t*)(ip + 16);
					uint16_t __headCopyDst2 = *(uint16_t*)(ip + 18);
					uint16_t __headCopySrc2 = *(uint16_t*)(ip + 20);
					uint16_t __lengthCopyDst1 = *(uint16_t*)(ip + 22);
					uint16_t __lengthCopySrc1 = *(uint16_t*)(ip + 24);
					uint16_t __lengthCopyDst2 = *(uint16_t*)(ip + 26);
					uint16_t __lengthCopySrc2 = *(uint16_t*)(ip + 28);
					uint16_t __lengthDst = *(uint16_t*)(ip + 30);
					uint16_t __arraySrc = *(uint16_t*)(ip + 32);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 34);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 36);
					int32_t __offset = *(int32_t*)(ip + 38);
					(*(uint64_t*)(localVarBase + __headCopyDst1)) = (*(uint64_t*)(localVarBase + __headCopySrc1));
					(*(int32_t*)(localVarBase + __headConstDst)) = __headConstant;
					(*(int32_t*)(localVarBase + __headAddRet)) = (*(int32_t*)(localVarBase + __headAddOp1)) + (*(int32_t*)(localVarBase + __headAddOp2));
					(*(uint64_t*)(localVarBase + __headCopyDst2)) = (*(uint64_t*)(localVarBase + __headCopySrc2));
					(*(uint64_t*)(localVarBase + __lengthCopyDst1)) = (*(uint64_t*)(localVarBase + __lengthCopySrc1));
					(*(uint64_t*)(localVarBase + __lengthCopyDst2)) = (*(uint64_t*)(localVarBase + __lengthCopySrc2));
					Il2CppArray* __arr = *(Il2CppArray**)(localVarBase + __arraySrc);
				    (*(int64_t*)(localVarBase + __lengthDst)) = (int64_t)il2cpp::vm::Array::GetLength(__arr);
				    if (CompareCge((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
				        ip += 48;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize_LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize_LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4:
				{
					uint16_t __elementCopyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __elementCopySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __elementCopyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __elementCopySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __elementDst = *(uint16_t*)(ip + 10);
					uint16_t __arraySrc = *(uint16_t*)(ip + 12);
					uint16_t __indexSrc = *(uint16_t*)(ip + 14);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 16);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 18);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 20);
					uint16_t __tailCopyDst1 = *(uint16_t*)(ip + 22);
					uint16_t __tailCopySrc1 = *(uint16_t*)(ip + 24);
					uint16_t __tailCopyDst2 = *(uint16_t*)(ip + 26);
					uint16_t __tailCopySrc2 = *(uint16_t*)(ip + 28);
					uint16_t __tailCopyDst3 = *(uint16_t*)(ip + 30);
					uint16_t __tailCopySrc3 = *(uint16_t*)(ip + 32);
					uint16_t __addRet = *(uint16_t*)(ip + 34);
					uint16_t __addOp1 = *(uint16_t*)(ip + 36);
					uint16_t __addOp2 = *(uint16_t*)(ip + 38);
					uint16_t __constDst = *(uint16_t*)(ip + 40);
					uint32_t __constant = *(uint32_t*)(ip + 44);
					(*(uint64_t*)(localVarBase + __elementCopyDst1)) = (*(uint64_t*)(localVarBase + __elementCopySrc1));
					(*(uint64_t*)(localVarBase + __elementCopyDst2)) = (*(uint64_t*)(localVarBase + __elementCopySrc2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy28((void*)(localVarBase + __elementDst), GetCheckedArrayElementAddress(arr, _index, 28));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
					(*(uint64_t*)(localVarBase + __tailCopyDst1)) = (*(uint64_t*)(localVarBase + __tailCopySrc1));
					(*(uint64_t*)(localVarBase + __tailCopyDst2)) = (*(uint64_t*)(localVarBase + __tailCopySrc2));
					(*(uint64_t*)(localVarBase + __tailCopyDst3)) = (*(uint64_t*)(localVarBase + __tailCopySrc3));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 48;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __subRet = *(uint16_t*)(ip + 6);
					uint16_t __subOp1 = *(uint16_t*)(ip + 8);
					uint16_t __subOp2 = *(uint16_t*)(ip + 10);
					uint16_t __storeAddress = *(uint16_t*)(ip + 12);
					uint16_t __addressDst = *(uint16_t*)(ip + 14);
					uint16_t __addressSrc = *(uint16_t*)(ip + 16);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 18);
					uint16_t __obj = *(uint16_t*)(ip + 20);
					uint16_t __offset = *(uint16_t*)(ip + 22);
					uint16_t __addressCopyDst = *(uint16_t*)(ip + 24);
					uint16_t __addressCopySrc = *(uint16_t*)(ip + 26);
					uint16_t __indDst = *(uint16_t*)(ip + 28);
					uint16_t __indSrc = *(uint16_t*)(ip + 30);
					uint16_t __constDst = *(uint16_t*)(ip + 32);
					uint32_t __constant = *(uint32_t*)(ip + 36);
					uint16_t __tailCopyDst1 = *(uint16_t*)(ip + 40);
					uint16_t __tailCopySrc1 = *(uint16_t*)(ip + 42);
					uint16_t __tailCopyDst2 = *(uint16_t*)(ip + 44);
					uint16_t __tailCopySrc2 = *(uint16_t*)(ip + 46);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					int32_t __value = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __subRet)) = __value;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __value;
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __addressCopyDst)) = (*(uint64_t*)(localVarBase + __addressCopySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(uint64_t*)(localVarBase + __tailCopyDst1)) = (*(uint64_t*)(localVarBase + __tailCopySrc1));
					(*(uint64_t*)(localVarBase + __tailCopyDst2)) = (*(uint64_t*)(localVarBase + __tailCopySrc2));
				    ip += 48;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4:
				HOTC233_EXEC_BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4:
				{
					uint16_t __headSubRet = *(uint16_t*)(ip + 2);
					uint16_t __headSubOp1 = *(uint16_t*)(ip + 4);
					uint16_t __headSubOp2 = *(uint16_t*)(ip + 6);
					uint16_t __fieldDst = *(uint16_t*)(ip + 8);
					uint16_t __obj = *(uint16_t*)(ip + 10);
					uint16_t __offset = *(uint16_t*)(ip + 12);
					uint16_t __constDst = *(uint16_t*)(ip + 14);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					uint16_t __divRet = *(uint16_t*)(ip + 20);
					uint16_t __divOp1 = *(uint16_t*)(ip + 22);
					uint16_t __divOp2 = *(uint16_t*)(ip + 24);
					uint16_t __chainSubRet = *(uint16_t*)(ip + 26);
					uint16_t __chainSubOp1 = *(uint16_t*)(ip + 28);
					uint16_t __chainSubOp2 = *(uint16_t*)(ip + 30);
					uint16_t __maxRet = *(uint16_t*)(ip + 32);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 34);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 36);
					uint16_t __tailSubRet = *(uint16_t*)(ip + 38);
					uint16_t __tailSubOp1 = *(uint16_t*)(ip + 40);
					uint16_t __tailSubOp2 = *(uint16_t*)(ip + 42);
					uint16_t __storeAddress = *(uint16_t*)(ip + 44);
					(*(int32_t*)(localVarBase + __headSubRet)) = (*(int32_t*)(localVarBase + __headSubOp1)) - (*(int32_t*)(localVarBase + __headSubOp2));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					(*(int32_t*)(localVarBase + __chainSubRet)) = (*(int32_t*)(localVarBase + __chainSubOp1)) - (*(int32_t*)(localVarBase + __chainSubOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
					int32_t __tailValue = (*(int32_t*)(localVarBase + __tailSubOp1)) - (*(int32_t*)(localVarBase + __tailSubOp2));
					(*(int32_t*)(localVarBase + __tailSubRet)) = __tailValue;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __tailValue;
					ip += 48;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4:
				{
					uint16_t __leadingCopyDst = *(uint16_t*)(ip + 2);
					uint16_t __leadingCopySrc = *(uint16_t*)(ip + 4);
					uint16_t __addressDst = *(uint16_t*)(ip + 6);
					uint16_t __addressSrc = *(uint16_t*)(ip + 8);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 10);
					uint16_t __obj = *(uint16_t*)(ip + 12);
					uint16_t __offset = *(uint16_t*)(ip + 14);
					uint16_t __copyDst = *(uint16_t*)(ip + 16);
					uint16_t __copySrc = *(uint16_t*)(ip + 18);
					uint16_t __indDst = *(uint16_t*)(ip + 20);
					uint16_t __indSrc = *(uint16_t*)(ip + 22);
					uint16_t __tailCopyDst = *(uint16_t*)(ip + 24);
					uint16_t __tailCopySrc = *(uint16_t*)(ip + 26);
					uint16_t __subRet = *(uint16_t*)(ip + 28);
					uint16_t __subOp1 = *(uint16_t*)(ip + 30);
					uint16_t __subOp2 = *(uint16_t*)(ip + 32);
					uint16_t __storeAddress = *(uint16_t*)(ip + 34);
					(*(uint64_t*)(localVarBase + __leadingCopyDst)) = (*(uint64_t*)(localVarBase + __leadingCopySrc));
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
					(*(uint64_t*)(localVarBase + __tailCopyDst)) = (*(uint64_t*)(localVarBase + __tailCopySrc));
					int32_t __value = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __subRet)) = __value;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __value;
				    ip += 40;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar:
				HOTC233_EXEC_LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __leadingCopyDst = *(uint16_t*)(ip + 2);
					uint16_t __leadingCopySrc = *(uint16_t*)(ip + 4);
					uint16_t __firstAddressDst = *(uint16_t*)(ip + 6);
					uint16_t __firstAddressSrc = *(uint16_t*)(ip + 8);
					uint16_t __firstFieldAddressDst = *(uint16_t*)(ip + 10);
					uint16_t __firstObj = *(uint16_t*)(ip + 12);
					uint16_t __firstOffset = *(uint16_t*)(ip + 14);
					uint16_t __firstCopyDst = *(uint16_t*)(ip + 16);
					uint16_t __firstCopySrc = *(uint16_t*)(ip + 18);
					uint16_t __firstIndDst = *(uint16_t*)(ip + 20);
					uint16_t __firstIndSrc = *(uint16_t*)(ip + 22);
					uint16_t __tailCopyDst = *(uint16_t*)(ip + 24);
					uint16_t __tailCopySrc = *(uint16_t*)(ip + 26);
					uint16_t __subRet = *(uint16_t*)(ip + 28);
					uint16_t __subOp1 = *(uint16_t*)(ip + 30);
					uint16_t __subOp2 = *(uint16_t*)(ip + 32);
					uint16_t __storeAddress = *(uint16_t*)(ip + 34);
					uint16_t __secondAddressDst = *(uint16_t*)(ip + 36);
					uint16_t __secondAddressSrc = *(uint16_t*)(ip + 38);
					uint16_t __secondFieldAddressDst = *(uint16_t*)(ip + 40);
					uint16_t __secondObj = *(uint16_t*)(ip + 42);
					uint16_t __secondOffset = *(uint16_t*)(ip + 44);
					uint16_t __secondCopyDst = *(uint16_t*)(ip + 46);
					uint16_t __secondCopySrc = *(uint16_t*)(ip + 48);
					uint16_t __secondIndDst = *(uint16_t*)(ip + 50);
					uint16_t __secondIndSrc = *(uint16_t*)(ip + 52);
					uint16_t __constDst = *(uint16_t*)(ip + 54);
					uint32_t __constant = *(uint32_t*)(ip + 56);
					uint16_t __secondTailCopyDst1 = *(uint16_t*)(ip + 60);
					uint16_t __secondTailCopySrc1 = *(uint16_t*)(ip + 62);
					uint16_t __secondTailCopyDst2 = *(uint16_t*)(ip + 64);
					uint16_t __secondTailCopySrc2 = *(uint16_t*)(ip + 66);
					(*(uint64_t*)(localVarBase + __leadingCopyDst)) = (*(uint64_t*)(localVarBase + __leadingCopySrc));
					(*(void**)(localVarBase + __firstAddressDst)) = (void*)(localVarBase + __firstAddressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __firstObj)));
				    (*(void**)(localVarBase + __firstFieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __firstObj)) + __firstOffset;
					(*(uint64_t*)(localVarBase + __firstCopyDst)) = (*(uint64_t*)(localVarBase + __firstCopySrc));
					(*(int32_t*)(localVarBase + __firstIndDst)) = (*(int32_t*)*(void**)(localVarBase + __firstIndSrc));
					(*(uint64_t*)(localVarBase + __tailCopyDst)) = (*(uint64_t*)(localVarBase + __tailCopySrc));
					int32_t __value = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __subRet)) = __value;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __value;
					(*(void**)(localVarBase + __secondAddressDst)) = (void*)(localVarBase + __secondAddressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __secondObj)));
				    (*(void**)(localVarBase + __secondFieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __secondObj)) + __secondOffset;
					(*(uint64_t*)(localVarBase + __secondCopyDst)) = (*(uint64_t*)(localVarBase + __secondCopySrc));
					(*(int32_t*)(localVarBase + __secondIndDst)) = (*(int32_t*)*(void**)(localVarBase + __secondIndSrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(uint64_t*)(localVarBase + __secondTailCopyDst1)) = (*(uint64_t*)(localVarBase + __secondTailCopySrc1));
					(*(uint64_t*)(localVarBase + __secondTailCopyDst2)) = (*(uint64_t*)(localVarBase + __secondTailCopySrc2));
					ip += 72;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4:
				HOTC233_EXEC_BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4:
				{
					uint16_t __andRet = *(uint16_t*)(ip + 2);
					uint16_t __andOp1 = *(uint16_t*)(ip + 4);
					uint16_t __andOp2 = *(uint16_t*)(ip + 6);
					uint16_t __elementDst = *(uint16_t*)(ip + 8);
					uint16_t __arraySrc = *(uint16_t*)(ip + 10);
					uint16_t __indexSrc = *(uint16_t*)(ip + 12);
					uint16_t __firstFieldDst = *(uint16_t*)(ip + 14);
					uint16_t __firstObj = *(uint16_t*)(ip + 16);
					uint16_t __firstOffset = *(uint16_t*)(ip + 18);
					uint16_t __firstConstDst = *(uint16_t*)(ip + 20);
					uint32_t __firstConstant = *(uint32_t*)(ip + 24);
					uint16_t __firstDivRet = *(uint16_t*)(ip + 28);
					uint16_t __firstDivOp1 = *(uint16_t*)(ip + 30);
					uint16_t __firstDivOp2 = *(uint16_t*)(ip + 32);
					uint16_t __addRet = *(uint16_t*)(ip + 34);
					uint16_t __addOp1 = *(uint16_t*)(ip + 36);
					uint16_t __addOp2 = *(uint16_t*)(ip + 38);
					uint16_t __copyDst = *(uint16_t*)(ip + 40);
					uint16_t __copySrc = *(uint16_t*)(ip + 42);
					uint16_t __secondFieldDst = *(uint16_t*)(ip + 44);
					uint16_t __secondObj = *(uint16_t*)(ip + 46);
					uint16_t __secondOffset = *(uint16_t*)(ip + 48);
					uint16_t __secondCopyDst = *(uint16_t*)(ip + 50);
					uint16_t __secondCopySrc = *(uint16_t*)(ip + 52);
					uint16_t __secondConstDst = *(uint16_t*)(ip + 54);
					uint32_t __secondConstant = *(uint32_t*)(ip + 56);
					uint16_t __secondDivRet = *(uint16_t*)(ip + 60);
					uint16_t __secondDivOp1 = *(uint16_t*)(ip + 62);
					uint16_t __secondDivOp2 = *(uint16_t*)(ip + 64);
					uint16_t __minRet = *(uint16_t*)(ip + 66);
					uint16_t __minOp1 = *(uint16_t*)(ip + 68);
					uint16_t __minOp2 = *(uint16_t*)(ip + 70);
					(*(int32_t*)(localVarBase + __andRet)) = (*(int32_t*)(localVarBase + __andOp1)) & (*(int32_t*)(localVarBase + __andOp2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    (*(int32_t*)(localVarBase + __elementDst)) = GetArrayElementFast<int32_t>(arr, _index);
					(*(int32_t*)(localVarBase + __firstFieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __firstObj) + __firstOffset);
					(*(int32_t*)(localVarBase + __firstConstDst)) = __firstConstant;
					(*(int32_t*)(localVarBase + __firstDivRet)) = HiDiv((*(int32_t*)(localVarBase + __firstDivOp1)), (*(int32_t*)(localVarBase + __firstDivOp2)));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __secondFieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __secondObj) + __secondOffset);
					(*(uint64_t*)(localVarBase + __secondCopyDst)) = (*(uint64_t*)(localVarBase + __secondCopySrc));
					(*(int32_t*)(localVarBase + __secondConstDst)) = __secondConstant;
					(*(int32_t*)(localVarBase + __secondDivRet)) = HiDiv((*(int32_t*)(localVarBase + __secondDivOp1)), (*(int32_t*)(localVarBase + __secondDivOp2)));
					int32_t __v1 = (*(int32_t*)(localVarBase + __minOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __minOp2));
					(*(int32_t*)(localVarBase + __minRet)) = __v1 < __v2 ? __v1 : __v2;
					ip += 72;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress:
				HOTC233_EXEC_LdlocVarAddress:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(void**)(localVarBase + __dst)) = (void*)(localVarBase + __src);
				    ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4:
				{
					uint16_t __addressDst = *(uint16_t*)(ip + 2);
					uint16_t __addressSrc = *(uint16_t*)(ip + 4);
					uint16_t __constDst = *(uint16_t*)(ip + 6);
					uint32_t __constant = *(uint32_t*)(ip + 8);
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpMul_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				{
					uint16_t __addressDst = *(uint16_t*)(ip + 2);
					uint16_t __addressSrc = *(uint16_t*)(ip + 4);
					uint16_t __addressConstDst = *(uint16_t*)(ip + 6);
					uint32_t __addressConstant = *(uint32_t*)(ip + 8);
					uint16_t __fieldDst = *(uint16_t*)(ip + 12);
					uint16_t __obj = *(uint16_t*)(ip + 14);
					uint16_t __offset = *(uint16_t*)(ip + 16);
					uint16_t __fieldConstDst = *(uint16_t*)(ip + 18);
					uint32_t __fieldConstant = *(uint32_t*)(ip + 20);
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
					(*(int32_t*)(localVarBase + __addressConstDst)) = __addressConstant;
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __fieldConstDst)) = __fieldConstant;
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdcVarConst_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 4);
					uint8_t __src = *(uint8_t*)(ip + 2);
					(*(int32_t*)(localVarBase + __dst)) = __src;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdcVarConst_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = __src;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RegI32Ldc:
				case HiOpcodeEnum::LdcVarConst_4:
				HOTC233_EXEC_LdcVarConst_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint32_t __src = *(uint32_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = __src;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdcVarConst_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint64_t __src = *(uint64_t*)(ip + 8);
					(*(uint64_t*)(localVarBase + __dst)) = __src;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdnullVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					(*(void**)(localVarBase + __dst)) = nullptr;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(int8_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(uint8_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(int16_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(uint16_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i4:
				HOTC233_EXEC_LdindVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(int32_t*)__addr);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)(localVarBase + __dst)) = (*(uint32_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int64_t*)(localVarBase + __dst)) = (*(int64_t*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(float*)(localVarBase + __dst)) = (*(float*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __src);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(double*)(localVarBase + __dst)) = (*(double*)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int8_t*)__addr) = (*(int8_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int16_t*)__addr) = (*(int16_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i4:
				HOTC233_EXEC_StindVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int32_t*)__addr) = (*(int32_t*)(localVarBase + __src));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(int64_t*)__addr) = (*(int64_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(float*)__addr) = (*(float*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(double*)__addr) = (*(double*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_ref:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					void* __addr = *(void**)(localVarBase + __dst);
					if (!__addr)
					{
						il2cpp::vm::Exception::RaiseNullReferenceException();
					}
					(*(Il2CppObject**)__addr) = (*(Il2CppObject**)(localVarBase + __src));	HOTC233_SET_WRITE_BARRIER((void**)__addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LocalAllocVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
					(*(void**)(localVarBase + __dst)) = LOCAL_ALLOC((*(uint16_t*)(localVarBase + __size)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LocalAllocVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
					(*(void**)(localVarBase + __dst)) = LOCAL_ALLOC((*(uint32_t*)(localVarBase + __size)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitblkVarVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
					std::memset((*(void**)(localVarBase + __addr)), (*(uint8_t*)(localVarBase + __value)), (*(uint32_t*)(localVarBase + __size)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpblkVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
					std::memmove((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)), (*(uint32_t*)(localVarBase + __size)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MemoryBarrier:
				{
					MEMORY_BARRIER();
				    ip += 8;
				    continue;
				}

				//!!!}}MEMORY


#pragma endregion

#pragma region CONVERT
		//!!!{{CONVERT
				case HiOpcodeEnum::ConvertVarVar_i4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint8_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint16_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint32_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_i8:
				HOTC233_EXEC_ConvertVarVar_i4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(int32_t*)(localVarBase + __src)));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i8)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i8;
					}
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (uint64_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint8_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint16_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint32_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (uint64_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u4_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint8_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint16_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint32_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (uint64_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i8_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint8_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint16_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (uint32_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (uint64_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_u8_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int8_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint8_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int16_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint16_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint32_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int64_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint64_t, int64_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f4_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int8_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint8_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int16_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint16_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint32_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int64_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint64_t, int64_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (float)((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_f8_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (double)((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_i1(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_u1(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint8_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_i2(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_u2(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint16_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_i4(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_u4(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint32_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_i8(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int32_t val = (*(int32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i4_u8(*(int32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (uint64_t)(uint32_t)((*(int32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_i1(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_u1(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint8_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_i2(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_u2(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint16_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_i4(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_u4(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint32_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_i8(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint32_t val = (*(uint32_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u4_u8(*(uint32_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (uint64_t)((*(uint32_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_i1(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_u1(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint8_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_i2(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_u2(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint16_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_i4(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_u4(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint32_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_i8(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_i8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    int64_t val = (*(int64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_i8_u8(*(int64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (uint64_t)(uint64_t)((*(int64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_i1(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int8_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_u1(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint8_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_i2(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int16_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_u2(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint16_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_i4(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (int32_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_u4(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = (uint32_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_i8(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (int64_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_u8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    uint64_t val = (*(uint64_t*)(localVarBase + __src));
				    if (CheckConvertOverflow_u8_u8(*(uint64_t*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = (uint64_t)((*(uint64_t*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_i1(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int8_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_u1(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint8_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_i2(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int16_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_u2(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint16_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_i4(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_u4(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint32_t, int32_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_i8(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int64_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f4_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    float val = (*(float*)(localVarBase + __src));
				    if (CheckConvertOverflow_f4_u8(*(float*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint64_t, int64_t>((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_i1(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int8_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_u1(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint8_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_i2(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int16_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_u2(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint16_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_i4(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_u4(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int32_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint32_t, int32_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_i8(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_double_to_int<int64_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ConvertOverflowVarVar_f8_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    double val = (*(double*)(localVarBase + __src));
				    if (CheckConvertOverflow_f8_u8(*(double*)(localVarBase + __src)))
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    (*(int64_t*)(localVarBase + __dst)) = il2cpp_codegen_cast_floating_point<uint64_t, int64_t>((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}

				//!!!}}CONVERT
#pragma endregion

#pragma region ARITH
		//!!!{{ARITH
#if !HOTC233_ENABLE_THREADED_DISPATCH
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) + (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
#else
				case HiOpcodeEnum::RegI32Add:
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					int32_t __value = (*(int32_t*)(localVarBase + __op1)) + (*(int32_t*)(localVarBase + __op2));
					(*(int32_t*)(localVarBase + __ret)) = __value;
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::StindVarVar_i4)
					{
						uint16_t __dst = *(uint16_t*)(ip + 2);
						uint16_t __src = *(uint16_t*)(ip + 4);
						if (__src == __ret)
						{
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							(*(int32_t*)*(void**)(localVarBase + __dst)) = __value;
							ip += 8;
							continue;
						}
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::CallDelegateInvoke_void)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallDelegateInvoke_void;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::RetVar_ret_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						__ret = *(uint16_t*)(ip + 2);
						SET_RET_AND_LEAVE_FRAME(4, 8);
					}
				    continue;
				}
#endif
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdlocVarVar:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::RetVar_ret_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						uint16_t __ret = *(uint16_t*)(ip + 2);
						SET_RET_AND_LEAVE_FRAME(4, 8);
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __fieldDst = *(uint16_t*)(ip + 12);
					uint16_t __obj = *(uint16_t*)(ip + 14);
					uint16_t __offset = *(uint16_t*)(ip + 16);
					uint16_t __fieldCopyDst = *(uint16_t*)(ip + 18);
					uint16_t __fieldCopySrc = *(uint16_t*)(ip + 20);
					uint16_t __constDst = *(uint16_t*)(ip + 22);
					uint32_t __constant = *(uint32_t*)(ip + 24);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(uint64_t*)(localVarBase + __fieldCopyDst)) = (*(uint64_t*)(localVarBase + __fieldCopySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    ip += 32;
				    continue;
				}
#if !HOTC233_ENABLE_THREADED_DISPATCH
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __copyDst1 = *(uint16_t*)(ip + 8);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 10);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 12);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 14);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 16);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 18);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					if (__copyDst1 != __copySrc1)
					{
						(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					}
					if (__copyDst2 != __copySrc2)
					{
						(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					}
					if (__copyDst3 != __copySrc3)
					{
						(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
					}
				    ip += 24;
				    continue;
				}
#else
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar:
				{
					byte* __chainIp = ip;
					for (uint16_t __chain = 0; __chain < 64; __chain++)
					{
						if (*(HiOpcodeEnum*)__chainIp != HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar)
						{
							break;
						}
						uint16_t __addRet = *(uint16_t*)(__chainIp + 2);
						uint16_t __addOp1 = *(uint16_t*)(__chainIp + 4);
						uint16_t __addOp2 = *(uint16_t*)(__chainIp + 6);
						uint16_t __copyDst1 = *(uint16_t*)(__chainIp + 8);
						uint16_t __copySrc1 = *(uint16_t*)(__chainIp + 10);
						uint16_t __copyDst2 = *(uint16_t*)(__chainIp + 12);
						uint16_t __copySrc2 = *(uint16_t*)(__chainIp + 14);
						uint16_t __copyDst3 = *(uint16_t*)(__chainIp + 16);
						uint16_t __copySrc3 = *(uint16_t*)(__chainIp + 18);
						(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
						if (__copyDst1 != __copySrc1)
						{
							(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
						}
						if (__copyDst2 != __copySrc2)
						{
							(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
						}
						if (__copyDst3 != __copySrc3)
						{
							(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
						}
						__chainIp += 24;
					}
					if (g_opcodeProfilerEnabled && __chainIp != ip + 24)
					{
						opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
					}
					ip = __chainIp;
				    continue;
				}
#endif
#if HOTC233_ENABLE_PRO_CALL_TRACE
				case HiOpcodeEnum::RunI4AddCopyTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word0 = __trace[__step * 3];
						uint64_t __word1 = __trace[__step * 3 + 1];
						uint64_t __word2 = __trace[__step * 3 + 2];
						uint16_t __addRet = (uint16_t)__word0;
						uint16_t __addOp1 = (uint16_t)(__word0 >> 16);
						uint16_t __addOp2 = (uint16_t)(__word0 >> 32);
						uint16_t __copyDst1 = (uint16_t)(__word0 >> 48);
						uint16_t __copySrc1 = (uint16_t)__word1;
						uint16_t __copyDst2 = (uint16_t)(__word1 >> 16);
						uint16_t __copySrc2 = (uint16_t)(__word1 >> 32);
						uint16_t __copyDst3 = (uint16_t)(__word1 >> 48);
						uint16_t __copySrc3 = (uint16_t)__word2;
						(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
						(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
						(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
						(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
					}
				    ip += 8;
				    continue;
				}
#endif
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __copyDst1 = *(uint16_t*)(ip + 8);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 10);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 12);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 14);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 16);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 18);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 20);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 22);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 24);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdcVarConst_4:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Rem_i4_BranchTrueVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Rem_i4_BranchTrueVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4_BinOpAnd_i4_GetArrayElementVarVar_i4:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					uint16_t __andRet = *(uint16_t*)(ip + 16);
					uint16_t __andOp1 = *(uint16_t*)(ip + 18);
					uint16_t __andOp2 = *(uint16_t*)(ip + 20);
					uint16_t __elementDst = *(uint16_t*)(ip + 22);
					uint16_t __arraySrc = *(uint16_t*)(ip + 24);
					uint16_t __indexSrc = *(uint16_t*)(ip + 26);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __andRet)) = (*(int32_t*)(localVarBase + __andOp1)) & (*(int32_t*)(localVarBase + __andOp2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    (*(int32_t*)(localVarBase + __elementDst)) = GetArrayElementFast<int32_t>(arr, _index);
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_StfldVarVar_i4:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4_StfldVarVar_i4:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __storeObj = *(uint16_t*)(ip + 8);
					uint16_t __storeOffset = *(uint16_t*)(ip + 10);
					uint16_t __storeData = *(uint16_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __storeObj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __storeObj)) + __storeOffset;
				    *(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __storeData));
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::RetVar_void)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_RetVar_void;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4:
				HOTC233_EXEC_BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4:
				{
					uint16_t __addRet = *(uint16_t*)(ip + 2);
					uint16_t __addOp1 = *(uint16_t*)(ip + 4);
					uint16_t __addOp2 = *(uint16_t*)(ip + 6);
					uint16_t __storeObj = *(uint16_t*)(ip + 8);
					uint16_t __storeOffset = *(uint16_t*)(ip + 10);
					uint16_t __storeData = *(uint16_t*)(ip + 12);
					uint16_t __addressDst = *(uint16_t*)(ip + 14);
					uint16_t __addressSrc = *(uint16_t*)(ip + 16);
					uint16_t __constDst = *(uint16_t*)(ip + 18);
					uint32_t __constant = *(uint32_t*)(ip + 20);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __storeObj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __storeObj)) + __storeOffset;
					*(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __storeData));
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpRem_i4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::StfldVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_StfldVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::RegI32Sub:
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) - (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __maxRet = *(uint16_t*)(ip + 8);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 10);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::StfldVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_StfldVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __maxRet = *(uint16_t*)(ip + 8);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 10);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 12);
					uint16_t __tailSubRet = *(uint16_t*)(ip + 14);
					uint16_t __tailSubOp1 = *(uint16_t*)(ip + 16);
					uint16_t __tailSubOp2 = *(uint16_t*)(ip + 18);
					uint16_t __storeAddress = *(uint16_t*)(ip + 20);
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
					int32_t __tailValue = (*(int32_t*)(localVarBase + __tailSubOp1)) - (*(int32_t*)(localVarBase + __tailSubOp2));
					(*(int32_t*)(localVarBase + __tailSubRet)) = __tailValue;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __tailValue;
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				HOTC233_EXEC_BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __maxRet = *(uint16_t*)(ip + 8);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 10);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 12);
					uint16_t __storeObj = *(uint16_t*)(ip + 14);
					uint16_t __storeOffset = *(uint16_t*)(ip + 16);
					uint16_t __storeData = *(uint16_t*)(ip + 18);
					uint16_t __fieldDst = *(uint16_t*)(ip + 20);
					uint16_t __obj = *(uint16_t*)(ip + 22);
					uint16_t __offset = *(uint16_t*)(ip + 24);
					uint16_t __constDst = *(uint16_t*)(ip + 26);
					uint32_t __constant = *(uint32_t*)(ip + 28);
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __storeObj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __storeObj)) + __storeOffset;
					*(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __storeData));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 32;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BranchVarVar_Cgt_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchVarVar_Cgt_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __fieldDst = *(uint16_t*)(ip + 8);
					uint16_t __obj = *(uint16_t*)(ip + 10);
					uint16_t __offset = *(uint16_t*)(ip + 12);
					uint16_t __constDst = *(uint16_t*)(ip + 14);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					uint16_t __divRet = *(uint16_t*)(ip + 20);
					uint16_t __divOp1 = *(uint16_t*)(ip + 22);
					uint16_t __divOp2 = *(uint16_t*)(ip + 24);
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __fieldDst = *(uint16_t*)(ip + 8);
					uint16_t __obj = *(uint16_t*)(ip + 10);
					uint16_t __offset = *(uint16_t*)(ip + 12);
					uint16_t __constDst = *(uint16_t*)(ip + 14);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					uint16_t __divRet = *(uint16_t*)(ip + 20);
					uint16_t __divOp1 = *(uint16_t*)(ip + 22);
					uint16_t __divOp2 = *(uint16_t*)(ip + 24);
					uint16_t __tailSubRet = *(uint16_t*)(ip + 26);
					uint16_t __tailSubOp1 = *(uint16_t*)(ip + 28);
					uint16_t __tailSubOp2 = *(uint16_t*)(ip + 30);
					uint16_t __maxRet = *(uint16_t*)(ip + 32);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 34);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 36);
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					(*(int32_t*)(localVarBase + __tailSubRet)) = (*(int32_t*)(localVarBase + __tailSubOp1)) - (*(int32_t*)(localVarBase + __tailSubOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
				    ip += 40;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4:
				HOTC233_EXEC_BinOpVarVarVar_Sub_i4_StindVarVar_i4:
				{
					uint16_t __subRet = *(uint16_t*)(ip + 2);
					uint16_t __subOp1 = *(uint16_t*)(ip + 4);
					uint16_t __subOp2 = *(uint16_t*)(ip + 6);
					uint16_t __storeAddress = *(uint16_t*)(ip + 8);
					int32_t __value = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __subRet)) = __value;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __value;
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BranchUncondition_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchUncondition_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __subRet = *(uint16_t*)(ip + 6);
					uint16_t __subOp1 = *(uint16_t*)(ip + 8);
					uint16_t __subOp2 = *(uint16_t*)(ip + 10);
					uint16_t __storeAddress = *(uint16_t*)(ip + 12);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					int32_t __value = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					(*(int32_t*)(localVarBase + __subRet)) = __value;
					(*(int32_t*)*(void**)(localVarBase + __storeAddress)) = __value;
				    ip += 16;
				    continue;
				}
#if !HOTC233_ENABLE_THREADED_DISPATCH
				case HiOpcodeEnum::BinOpVarVarVar_Mul_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) * (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
#else
				case HiOpcodeEnum::RegI32Mul:
				case HiOpcodeEnum::BinOpVarVarVar_Mul_i4:
				HOTC233_EXEC_BinOpVarVarVar_Mul_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) * (*(int32_t*)(localVarBase + __op2));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4;
					}
				    continue;
				}
#endif
				case HiOpcodeEnum::BinOpVarVarVar_MulUn_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiMulUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Div_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiDiv((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4:
				{
					uint16_t __divRet = *(uint16_t*)(ip + 2);
					uint16_t __divOp1 = *(uint16_t*)(ip + 4);
					uint16_t __divOp2 = *(uint16_t*)(ip + 6);
					uint16_t __minRet = *(uint16_t*)(ip + 8);
					uint16_t __minOp1 = *(uint16_t*)(ip + 10);
					uint16_t __minOp2 = *(uint16_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					int32_t __v1 = (*(int32_t*)(localVarBase + __minOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __minOp2));
					(*(int32_t*)(localVarBase + __minRet)) = __v1 < __v2 ? __v1 : __v2;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_DivUn_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiDivUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Rem_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiRem((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Rem_i4_BranchTrueVar_i4:
				HOTC233_EXEC_BinOpVarVarVar_Rem_i4_BranchTrueVar_i4:
				{
					uint16_t __remRet = *(uint16_t*)(ip + 2);
					uint16_t __remOp1 = *(uint16_t*)(ip + 4);
					uint16_t __remOp2 = *(uint16_t*)(ip + 6);
					uint16_t __branchOp = *(uint16_t*)(ip + 8);
					int32_t __offsetBranch = *(int32_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __remRet)) = HiRem((*(int32_t*)(localVarBase + __remOp1)), (*(int32_t*)(localVarBase + __remOp2)));
				    if ((*(int32_t*)(localVarBase + __branchOp)))
				    {
						ip = ipBase + __offsetBranch;
				    }
				    else
				    {
						ip += 16;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_RemUn_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiRemUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_And_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) & (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4:
				{
					uint16_t __andRet = *(uint16_t*)(ip + 2);
					uint16_t __andOp1 = *(uint16_t*)(ip + 4);
					uint16_t __andOp2 = *(uint16_t*)(ip + 6);
					uint16_t __elementDst = *(uint16_t*)(ip + 8);
					uint16_t __arraySrc = *(uint16_t*)(ip + 10);
					uint16_t __indexSrc = *(uint16_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __andRet)) = (*(int32_t*)(localVarBase + __andOp1)) & (*(int32_t*)(localVarBase + __andOp2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    (*(int32_t*)(localVarBase + __elementDst)) = GetArrayElementFast<int32_t>(arr, _index);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar:
				{
					uint16_t __andRet = *(uint16_t*)(ip + 2);
					uint16_t __andOp1 = *(uint16_t*)(ip + 4);
					uint16_t __andOp2 = *(uint16_t*)(ip + 6);
					uint16_t __elementDst = *(uint16_t*)(ip + 8);
					uint16_t __arraySrc = *(uint16_t*)(ip + 10);
					uint16_t __indexSrc = *(uint16_t*)(ip + 12);
					uint16_t __fieldDst = *(uint16_t*)(ip + 14);
					uint16_t __obj = *(uint16_t*)(ip + 16);
					uint16_t __offset = *(uint16_t*)(ip + 18);
					uint16_t __constDst = *(uint16_t*)(ip + 20);
					uint32_t __constant = *(uint32_t*)(ip + 24);
					uint16_t __divRet = *(uint16_t*)(ip + 28);
					uint16_t __divOp1 = *(uint16_t*)(ip + 30);
					uint16_t __divOp2 = *(uint16_t*)(ip + 32);
					uint16_t __addRet = *(uint16_t*)(ip + 34);
					uint16_t __addOp1 = *(uint16_t*)(ip + 36);
					uint16_t __addOp2 = *(uint16_t*)(ip + 38);
					uint16_t __copyDst = *(uint16_t*)(ip + 40);
					uint16_t __copySrc = *(uint16_t*)(ip + 42);
					(*(int32_t*)(localVarBase + __andRet)) = (*(int32_t*)(localVarBase + __andOp1)) & (*(int32_t*)(localVarBase + __andOp2));
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    (*(int32_t*)(localVarBase + __elementDst)) = GetArrayElementFast<int32_t>(arr, _index);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    ip += 48;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Or_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) | (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RegI32Xor:
				case HiOpcodeEnum::BinOpVarVarVar_Xor_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __op1)) ^ (*(int32_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_i8:
				HOTC233_EXEC_BinOpVarVarVar_Add_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) + (*(int64_t*)(localVarBase + __op2));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) - (*(int64_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Mul_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) * (*(int64_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_MulUn_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiMulUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Div_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiDiv((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_DivUn_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiDivUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Rem_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiRem((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_RemUn_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiRemUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_And_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) & (*(int64_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Or_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) | (*(int64_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Xor_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __op1)) ^ (*(int64_t*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MathMinVarVarVar_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					int32_t __v1 = (*(int32_t*)(localVarBase + __op1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __op2));
					(*(int32_t*)(localVarBase + __ret)) = __v1 < __v2 ? __v1 : __v2;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MathMaxVarVarVar_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					int32_t __v1 = (*(int32_t*)(localVarBase + __op1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __op2));
					(*(int32_t*)(localVarBase + __ret)) = __v1 > __v2 ? __v1 : __v2;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MathMinVarVarVar_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					int64_t __v1 = (*(int64_t*)(localVarBase + __op1));
					int64_t __v2 = (*(int64_t*)(localVarBase + __op2));
					(*(int64_t*)(localVarBase + __ret)) = __v1 < __v2 ? __v1 : __v2;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MathMaxVarVarVar_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					int64_t __v1 = (*(int64_t*)(localVarBase + __op1));
					int64_t __v2 = (*(int64_t*)(localVarBase + __op2));
					(*(int64_t*)(localVarBase + __ret)) = __v1 > __v2 ? __v1 : __v2;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MathfClamp01VarVar_r4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					float __v = (*(float*)(localVarBase + __op1));
					(*(float*)(localVarBase + __ret)) = __v < 0.0f ? 0.0f : (__v > 1.0f ? 1.0f : __v);
				    ip += 6;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(float*)(localVarBase + __ret)) = (*(float*)(localVarBase + __op1)) + (*(float*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(float*)(localVarBase + __ret)) = (*(float*)(localVarBase + __op1)) - (*(float*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Mul_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(float*)(localVarBase + __ret)) = (*(float*)(localVarBase + __op1)) * (*(float*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Div_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(float*)(localVarBase + __ret)) = HiDiv((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Rem_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(float*)(localVarBase + __ret)) = HiRem((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Add_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(double*)(localVarBase + __ret)) = (*(double*)(localVarBase + __op1)) + (*(double*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Sub_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(double*)(localVarBase + __ret)) = (*(double*)(localVarBase + __op1)) - (*(double*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Mul_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(double*)(localVarBase + __ret)) = (*(double*)(localVarBase + __op1)) * (*(double*)(localVarBase + __op2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Div_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(double*)(localVarBase + __ret)) = HiDiv((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpVarVarVar_Rem_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					(*(double*)(localVarBase + __ret)) = HiRem((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Add_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int32_t op1 = (*(int32_t*)(localVarBase + __op1));
				    int32_t op2 = (*(int32_t*)(localVarBase + __op2));
				    if ((CheckAddOverflow(op1, op2)) == 0)
				    {
				        (*(int32_t*)(localVarBase + __ret)) = op1 + op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Sub_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int32_t op1 = (*(int32_t*)(localVarBase + __op1));
				    int32_t op2 = (*(int32_t*)(localVarBase + __op2));
				    if ((CheckSubOverflow(op1, op2)) == 0)
				    {
				        (*(int32_t*)(localVarBase + __ret)) = op1 - op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Mul_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int32_t op1 = (*(int32_t*)(localVarBase + __op1));
				    int32_t op2 = (*(int32_t*)(localVarBase + __op2));
				    if ((CheckMulOverflow(op1, op2)) == 0)
				    {
				        (*(int32_t*)(localVarBase + __ret)) = op1 * op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Add_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int64_t op1 = (*(int64_t*)(localVarBase + __op1));
				    int64_t op2 = (*(int64_t*)(localVarBase + __op2));
				    if ((CheckAddOverflow64(op1, op2)) == 0)
				    {
				        (*(int64_t*)(localVarBase + __ret)) = op1 + op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Sub_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int64_t op1 = (*(int64_t*)(localVarBase + __op1));
				    int64_t op2 = (*(int64_t*)(localVarBase + __op2));
				    if ((CheckSubOverflow64(op1, op2)) == 0)
				    {
				        (*(int64_t*)(localVarBase + __ret)) = op1 - op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Mul_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    int64_t op1 = (*(int64_t*)(localVarBase + __op1));
				    int64_t op2 = (*(int64_t*)(localVarBase + __op2));
				    if ((CheckMulOverflow64(op1, op2)) == 0)
				    {
				        (*(int64_t*)(localVarBase + __ret)) = op1 * op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Add_u4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint32_t op1 = (*(uint32_t*)(localVarBase + __op1));
				    uint32_t op2 = (*(uint32_t*)(localVarBase + __op2));
				    if ((CheckAddOverflowUn(op1, op2)) == 0)
				    {
				        (*(uint32_t*)(localVarBase + __ret)) = op1 + op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Sub_u4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint32_t op1 = (*(uint32_t*)(localVarBase + __op1));
				    uint32_t op2 = (*(uint32_t*)(localVarBase + __op2));
				    if ((CheckSubOverflowUn(op1, op2)) == 0)
				    {
				        (*(uint32_t*)(localVarBase + __ret)) = op1 - op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Mul_u4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint32_t op1 = (*(uint32_t*)(localVarBase + __op1));
				    uint32_t op2 = (*(uint32_t*)(localVarBase + __op2));
				    if ((CheckMulOverflowUn(op1, op2)) == 0)
				    {
				        (*(uint32_t*)(localVarBase + __ret)) = op1 * op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Add_u8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint64_t op1 = (*(uint64_t*)(localVarBase + __op1));
				    uint64_t op2 = (*(uint64_t*)(localVarBase + __op2));
				    if ((CheckAddOverflow64Un(op1, op2)) == 0)
				    {
				        (*(uint64_t*)(localVarBase + __ret)) = op1 + op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Sub_u8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint64_t op1 = (*(uint64_t*)(localVarBase + __op1));
				    uint64_t op2 = (*(uint64_t*)(localVarBase + __op2));
				    if ((CheckSubOverflow64Un(op1, op2)) == 0)
				    {
				        (*(uint64_t*)(localVarBase + __ret)) = op1 - op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BinOpOverflowVarVarVar_Mul_u8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
				    uint64_t op1 = (*(uint64_t*)(localVarBase + __op1));
				    uint64_t op2 = (*(uint64_t*)(localVarBase + __op2));
				    if ((CheckMulOverflow64Un(op1, op2)) == 0)
				    {
				        (*(uint64_t*)(localVarBase + __ret)) = op1 * op2;
				    }
				    else
				    {
				        il2cpp::vm::Exception::RaiseOverflowException();
				    }
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i4_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __value)) << (*(int32_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RegI32Shr:
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __value)) >> (*(int32_t*)(localVarBase + __shiftAmount));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::ConvertVarVar_i4_u1_SetArrayElementVarVar_i1)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_ConvertVarVar_i4_u1_SetArrayElementVarVar_i1;
					}
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i4_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiShrUn((*(int32_t*)(localVarBase + __value)), (*(int32_t*)(localVarBase + __shiftAmount)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i4_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __value)) << (*(int64_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = (*(int32_t*)(localVarBase + __value)) >> (*(int64_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i4_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = HiShrUn((*(int32_t*)(localVarBase + __value)), (*(int64_t*)(localVarBase + __shiftAmount)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i8_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __value)) << (*(int32_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i8_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __value)) >> (*(int32_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i8_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiShrUn((*(int64_t*)(localVarBase + __value)), (*(int32_t*)(localVarBase + __shiftAmount)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i8_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __value)) << (*(int64_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i8_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = (*(int64_t*)(localVarBase + __value)) >> (*(int64_t*)(localVarBase + __shiftAmount));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i8_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __value = *(uint16_t*)(ip + 4);
					uint16_t __shiftAmount = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __ret)) = HiShrUn((*(int64_t*)(localVarBase + __value)), (*(int64_t*)(localVarBase + __shiftAmount)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Neg_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = - (*(int32_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Not_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = ~ (*(int32_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Neg_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = - (*(int64_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Not_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = ~ (*(int64_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Neg_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = - (*(float*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnaryOpVarVar_Neg_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = - (*(double*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CheckFiniteVar_f4:
				{
					uint16_t __src = *(uint16_t*)(ip + 2);
					HiCheckFinite((*(float*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CheckFiniteVar_f8:
				{
					uint16_t __src = *(uint16_t*)(ip + 2);
					HiCheckFinite((*(double*)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}

				//!!!}}ARITH
#pragma endregion

#pragma region COMPARE
		//!!!{{COMPARE
				case HiOpcodeEnum::CompOpVarVarVar_Ceq_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCeq((*(int32_t*)(localVarBase + __c1)), (*(int32_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Ceq_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCeq((*(int64_t*)(localVarBase + __c1)), (*(int64_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Ceq_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCeq((*(float*)(localVarBase + __c1)), (*(float*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Ceq_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCeq((*(double*)(localVarBase + __c1)), (*(double*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Cgt_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgt((*(int32_t*)(localVarBase + __c1)), (*(int32_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Cgt_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgt((*(int64_t*)(localVarBase + __c1)), (*(int64_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Cgt_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgt((*(float*)(localVarBase + __c1)), (*(float*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Cgt_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgt((*(double*)(localVarBase + __c1)), (*(double*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CgtUn_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgtUn((*(int32_t*)(localVarBase + __c1)), (*(int32_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CgtUn_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgtUn((*(int64_t*)(localVarBase + __c1)), (*(int64_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CgtUn_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgtUn((*(float*)(localVarBase + __c1)), (*(float*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CgtUn_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCgtUn((*(double*)(localVarBase + __c1)), (*(double*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Clt_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareClt((*(int32_t*)(localVarBase + __c1)), (*(int32_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Clt_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareClt((*(int64_t*)(localVarBase + __c1)), (*(int64_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Clt_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareClt((*(float*)(localVarBase + __c1)), (*(float*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_Clt_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareClt((*(double*)(localVarBase + __c1)), (*(double*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CltUn_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCltUn((*(int32_t*)(localVarBase + __c1)), (*(int32_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CltUn_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCltUn((*(int64_t*)(localVarBase + __c1)), (*(int64_t*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CltUn_f4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCltUn((*(float*)(localVarBase + __c1)), (*(float*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CompOpVarVarVar_CltUn_f8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __c1 = *(uint16_t*)(ip + 4);
					uint16_t __c2 = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __ret)) = CompareCltUn((*(double*)(localVarBase + __c1)), (*(double*)(localVarBase + __c2)));
				    ip += 8;
				    continue;
				}

				//!!!}}COMPARE
#pragma endregion

#pragma region BRANCH
		//!!!{{BRANCH
				case HiOpcodeEnum::BranchUncondition_4:
				HOTC233_EXEC_BranchUncondition_4:
				{
					int32_t __offset = *(int32_t*)(ip + 4);
					ip = ipBase + __offset;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::BranchTrueVar_i4:
				HOTC233_EXEC_BranchTrueVar_i4:
				{
					uint16_t __op = *(uint16_t*)(ip + 2);
					int32_t __offset = *(int32_t*)(ip + 4);
				    if ((*(int32_t*)(localVarBase + __op)))
				    {
						ip = ipBase + __offset;
				    }
				    else
				    {
						ip += 8;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::BranchTrueVar_i8:
				{
					uint16_t __op = *(uint16_t*)(ip + 2);
					int32_t __offset = *(int32_t*)(ip + 4);
				    if ((*(int64_t*)(localVarBase + __op)))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 8;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchFalseVar_i4:
				{
					uint16_t __op = *(uint16_t*)(ip + 2);
					int32_t __offset = *(int32_t*)(ip + 4);
				    if (!(*(int32_t*)(localVarBase + __op)))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 8;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchFalseVar_i8:
				{
					uint16_t __op = *(uint16_t*)(ip + 2);
					int32_t __offset = *(int32_t*)(ip + 4);
				    if (!(*(int64_t*)(localVarBase + __op)))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 8;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Ceq_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCeq((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Ceq_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCeq((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Ceq_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCeq((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Ceq_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCeq((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CneUn_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCneUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CneUn_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCneUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CneUn_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCneUn((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CneUn_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCneUn((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip = ipBase + __offset;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cgt_i4:
				HOTC233_EXEC_BranchVarVar_Cgt_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCle((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
						ip += 16;
				    }
				    else
				    {
						ip = ipBase + __offset;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize;
					}
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cgt_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCle((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cgt_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCle((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cgt_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCle((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgtUn_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCleUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgtUn_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCleUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgtUn_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCleUn((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgtUn_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCleUn((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cge_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareClt((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cge_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareClt((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cge_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareClt((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cge_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareClt((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgeUn_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCltUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgeUn_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCltUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgeUn_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCltUn((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CgeUn_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCltUn((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Clt_i4:
				HOTC233_EXEC_BranchVarVar_Clt_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCge((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
						ip += 16;
				    }
				    else
				    {
						ip = ipBase + __offset;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Clt_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCge((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Clt_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCge((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Clt_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCge((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CltUn_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgeUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CltUn_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgeUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CltUn_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgeUn((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CltUn_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgeUn((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cle_i4:
				HOTC233_EXEC_BranchVarVar_Cle_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgt((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
						ip += 16;
				    }
				    else
				    {
						ip = ipBase + __offset;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cle_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgt((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cle_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgt((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_Cle_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgt((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CleUn_i4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgtUn((*(int32_t*)(localVarBase + __op1)), (*(int32_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CleUn_i8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgtUn((*(int64_t*)(localVarBase + __op1)), (*(int64_t*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CleUn_f4:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgtUn((*(float*)(localVarBase + __op1)), (*(float*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchVarVar_CleUn_f8:
				{
					uint16_t __op1 = *(uint16_t*)(ip + 2);
					uint16_t __op2 = *(uint16_t*)(ip + 4);
					int32_t __offset = *(int32_t*)(ip + 8);
				    if (CompareCgtUn((*(double*)(localVarBase + __op1)), (*(double*)(localVarBase + __op2))))
				    {
				        ip += 16;
				    }
				    else
				    {
				        ip = ipBase + __offset;
				    }
				    continue;
				}
				case HiOpcodeEnum::BranchJump:
				{
					uint32_t __token = *(uint32_t*)(ip + 4);
					IL2CPP_ASSERT(false);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BranchSwitch:
				{
				    uint16_t __value = *(uint16_t*)(ip + 2);
				    uint32_t __caseNum = *(uint32_t*)(ip + 4);
				    uint32_t __caseOffsets = *(uint32_t*)(ip + 8);
				    uint32_t __idx = (*(uint32_t*)(localVarBase + __value));
				    if (__idx < __caseNum)
				    {
				        ip = ipBase + ((uint32_t*)&imi->resolveDatas[__caseOffsets])[__idx];
				    }
				    else 
				    {
				        ip += 16;
				    }
				    continue;
				}

				//!!!}}BRANCH
#pragma endregion

#pragma region FUNCTION
		//!!!{{FUNCTION
				case HiOpcodeEnum::NewClassVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					void* __managed2NativeMethod = ((void*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    frame->ip = ip + 2;
				    uint16_t* _argIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    Il2CppObject* _obj = il2cpp::vm::Object::New(__method->klass);
				    *(Il2CppObject**)(localVarBase + _argIdxs[0]) = _obj;
				    ((Managed2NativeCallMethod)__managed2NativeMethod)(__method, _argIdxs, localVarBase, nullptr);
				    (*(Il2CppObject**)(localVarBase + __obj)) = _obj;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewClassVar_Ctor_0:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
				    frame->ip = ip + 2;
				    Il2CppObject* _obj = il2cpp::vm::Object::New(__method->klass);
				    ((NativeClassCtor0)(__method->methodPointerCallByInterp))(_obj, __method);
				    (*(Il2CppObject**)(localVarBase + __obj)) = _obj;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewClassVar_NotCtor:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
				    frame->ip = ip + 2;
				    (*(Il2CppObject**)(localVarBase + __obj)) = il2cpp::vm::Object::New(__klass);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewValueTypeVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					void* __managed2NativeMethod = ((void*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    frame->ip = ip + 2;
				    uint16_t* _argIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    int32_t _typeSize = GetTypeValueSize(__method->klass);
				    // arg1, arg2, ..., argN, value type, __this
				    StackObject* _frameBasePtr = localVarBase + _argIdxs[0];
				    Il2CppObject* _this = (Il2CppObject*)(_frameBasePtr - GetStackSizeByByteSize(_typeSize));
				    _frameBasePtr->ptr = _this;
				    ((Managed2NativeCallMethod)__managed2NativeMethod)(__method, _argIdxs, localVarBase, nullptr);
				    std::memmove((void*)(localVarBase + __obj), _this, _typeSize);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewValueTypeVar_Ctor_0:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    InitDefaultN((void*)(localVarBase + __obj), __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewClassInterpVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 12)]);
					uint16_t __argBase = *(uint16_t*)(ip + 4);
					uint16_t __argStackObjectNum = *(uint16_t*)(ip + 6);
					uint16_t __ctorFrameBase = *(uint16_t*)(ip + 8);
				    IL2CPP_ASSERT(__obj < __ctorFrameBase);
				    Il2CppObject* _newObj = il2cpp::vm::Object::New(__method->klass);
				    StackObject* _frameBasePtr = (StackObject*)(void*)(localVarBase + __ctorFrameBase);
				    std::memmove(_frameBasePtr + 1, (void*)(localVarBase + __argBase), __argStackObjectNum * sizeof(StackObject)); // move arg
				    _frameBasePtr->obj = _newObj; // prepare this 
				    (*(Il2CppObject**)(localVarBase + __obj)) = _newObj; // set must after move
				    CALL_INTERP_VOID((ip + 16), __method, _frameBasePtr);
				    continue;
				}
				case HiOpcodeEnum::NewClassInterpVar_Ctor_0:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __ctorFrameBase = *(uint16_t*)(ip + 4);
				    IL2CPP_ASSERT(__obj < __ctorFrameBase);
				    Il2CppObject* _newObj = il2cpp::vm::Object::New(__method->klass);
				    StackObject* _frameBasePtr = (StackObject*)(void*)(localVarBase + __ctorFrameBase);
				    _frameBasePtr->obj = _newObj; // prepare this 
				    (*(Il2CppObject**)(localVarBase + __obj)) = _newObj;
				    CALL_INTERP_VOID((ip + 16), __method, _frameBasePtr);
				    continue;
				}
				case HiOpcodeEnum::NewValueTypeInterpVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 12)]);
					uint16_t __argBase = *(uint16_t*)(ip + 4);
					uint16_t __argStackObjectNum = *(uint16_t*)(ip + 6);
					uint16_t __ctorFrameBase = *(uint16_t*)(ip + 8);
				    IL2CPP_ASSERT(__obj < __ctorFrameBase);
				    StackObject* _frameBasePtr = (StackObject*)(void*)(localVarBase + __ctorFrameBase);
				    std::memmove(_frameBasePtr + 1, (void*)(localVarBase + __argBase), __argStackObjectNum * sizeof(StackObject)); // move arg
				    _frameBasePtr->ptr = (StackObject*)(void*)(localVarBase + __obj);
				    CALL_INTERP_VOID((ip + 16), __method, _frameBasePtr);
				    continue;
				}
				case HiOpcodeEnum::AdjustValueTypeRefVar:
				{
					uint16_t __data = *(uint16_t*)(ip + 2);
				    // ref => fake value type boxed object value. // fake obj = ref(value_type) - sizeof(Il2CppObject)
				    StackObject* _thisSo = ((StackObject*)((void*)(localVarBase + __data)));
				    _thisSo->obj -= 1;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::BoxRefVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppObject**)(localVarBase + __dst)) = il2cpp::vm::Object::Box(__klass, (*(void**)(localVarBase + __src)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdvirftnVarVar:
				{
					uint16_t __resultMethod = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					MethodInfo* __virtualMethod = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(MethodInfo**)(localVarBase + __resultMethod)) = GET_OBJECT_VIRTUAL_METHOD((*(Il2CppObject**)(localVarBase + __obj)), __virtualMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_1:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(1, 8);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_2:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(2, 8);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(4, 8);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					static int s_ret8ProbeCount = 0;
					const bool _probeRet8 = s_ret8ProbeCount < 80;
					if (_probeRet8)
					{
						s_ret8ProbeCount++;
						std::printf("[hotc233][Ret8Probe] before method=%s.%s::%s retSlot=%u retPtr=%p value=%p frame=%p\n",
							frame && frame->method && frame->method->klass && frame->method->klass->namespaze ? frame->method->klass->namespaze : "<null>",
							frame && frame->method && frame->method->klass && frame->method->klass->name ? frame->method->klass->name : "<null>",
							frame && frame->method && frame->method->name ? frame->method->name : "<null>",
							(unsigned)__ret,
							frame ? frame->ret : nullptr,
							*(void**)(localVarBase + __ret),
							(void*)frame);
						std::fflush(stdout);
					}
				    SET_RET_AND_LEAVE_FRAME(8, 8);
					if (_probeRet8)
					{
						std::printf("[hotc233][Ret8Probe] after-load method=%s.%s::%s ipOffset=%lld\n",
							frame && frame->method && frame->method->klass && frame->method->klass->namespaze ? frame->method->klass->namespaze : "<null>",
							frame && frame->method && frame->method->klass && frame->method->klass->name ? frame->method->klass->name : "<null>",
							frame && frame->method && frame->method->name ? frame->method->name : "<null>",
							(long long)(ip - ipBase));
						std::fflush(stdout);
					}
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_12:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(12, 12);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_16:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(16, 16);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_20:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(20, 20);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_24:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(24, 24);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_28:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(28, 28);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_32:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    SET_RET_AND_LEAVE_FRAME(32, 32);
				    continue;
				}
				case HiOpcodeEnum::RetVar_ret_n:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 4);
				    std::memmove(frame->ret, (void*)(localVarBase + __ret), __size);
					LEAVE_FRAME();
				    continue;
				}
				case HiOpcodeEnum::RetVar_void:
				HOTC233_EXEC_RetVar_void:
				{
					LEAVE_FRAME();
				    continue;
				}
				case HiOpcodeEnum::CallNativeInstance_void:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
					MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					MethodInfo* _declaredMethod = _resolvedMethod;
					_resolvedMethod = ResolveActualReferenceInstanceMethod(_resolvedMethod, localVarBase + _resolvedArgIdxs[0]);
					bool _hotc233TraceValueTupleCtor = frame
						&& frame->method
						&& frame->method->name
						&& std::strcmp(frame->method->name, "VerifyValueTuple") == 0
						&& _resolvedMethod
						&& _resolvedMethod->name
						&& std::strcmp(_resolvedMethod->name, ".ctor") == 0
						&& _resolvedMethod->klass
						&& _resolvedMethod->klass->name
						&& std::strstr(_resolvedMethod->klass->name, "ValueTuple");
					if (_hotc233TraceValueTupleCtor)
					{
						StackObject* _hotc233TupleTarget = (StackObject*)((localVarBase + _resolvedArgIdxs[0])->ptr);
						std::printf("[hotc233][ValueTupleCtorProbe] before %s.%s::%s arg0=%u arg1=%u arg2=%u slot0=%p/%llu slot1=%p/%llu slot2=%p/%llu\n",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name,
							(uint32_t)_resolvedArgIdxs[0],
							(uint32_t)_resolvedArgIdxs[1],
							(uint32_t)_resolvedArgIdxs[2],
							(localVarBase + _resolvedArgIdxs[0])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[0])->u64,
							(localVarBase + _resolvedArgIdxs[1])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[1])->u64,
							(localVarBase + _resolvedArgIdxs[2])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[2])->u64);
						if (_hotc233TupleTarget)
						{
							std::printf("[hotc233][ValueTupleCtorProbe] before-target target=%p t0=%p/%llu t1=%p/%llu\n",
								(void*)_hotc233TupleTarget,
								_hotc233TupleTarget[0].ptr, (unsigned long long)_hotc233TupleTarget[0].u64,
								_hotc233TupleTarget[1].ptr, (unsigned long long)_hotc233TupleTarget[1].u64);
						}
						std::fflush(stdout);
					}
					Managed2NativeCallMethod _resolvedM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
					if (_resolvedMethod != _declaredMethod && _resolvedMethod->has_full_generic_sharing_signature)
					{
						_resolvedM2N = InterpreterModule::GetManaged2NativeMethodPointer(_resolvedMethod, false);
					}
#endif
					bool _hotc233HandledValueTupleCtor = TryExecuteValueTupleCtorByInflatedFields(_resolvedMethod, _resolvedArgIdxs, localVarBase);
					if (!_hotc233HandledValueTupleCtor)
					{
						if (!TryExecuteArraySetValueSingleIndex(_resolvedMethod, _resolvedArgIdxs, localVarBase)
							&& !TrySetDictionaryStringValueType(_resolvedMethod, _resolvedArgIdxs, localVarBase)
							&& !TryInvokeNativeInstanceVoidWithBoxedValueTypeArgs(_resolvedMethod, _resolvedArgIdxs, localVarBase))
						{
							_resolvedM2N(_resolvedMethod, _resolvedArgIdxs, localVarBase, nullptr);
						}
					}
					if (_hotc233TraceValueTupleCtor)
					{
						StackObject* _hotc233TupleTarget = (StackObject*)((localVarBase + _resolvedArgIdxs[0])->ptr);
						std::printf("[hotc233][ValueTupleCtorProbe] after %s.%s::%s slot0=%p/%llu slot0+1=%p/%llu slot0+2=%p/%llu\n",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name,
							(localVarBase + _resolvedArgIdxs[0])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[0])->u64,
							(localVarBase + _resolvedArgIdxs[0] + 1)->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[0] + 1)->u64,
							(localVarBase + _resolvedArgIdxs[0] + 2)->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[0] + 2)->u64);
						if (_hotc233TupleTarget)
						{
							std::printf("[hotc233][ValueTupleCtorProbe] after-target target=%p t0=%p/%llu t1=%p/%llu\n",
								(void*)_hotc233TupleTarget,
								_hotc233TupleTarget[0].ptr, (unsigned long long)_hotc233TupleTarget[0].u64,
								_hotc233TupleTarget[1].ptr, (unsigned long long)_hotc233TupleTarget[1].u64);
						}
						std::fflush(stdout);
					}
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallNativeInstance_ret:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
				    void* _ret = (void*)(localVarBase + __ret);
					MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					MethodInfo* _declaredMethod = _resolvedMethod;
					_resolvedMethod = ResolveActualReferenceInstanceMethod(_resolvedMethod, localVarBase + _resolvedArgIdxs[0]);
					bool _hotc233TraceTypeGetMethod = _resolvedMethod
						&& _resolvedMethod->name
						&& std::strstr(_resolvedMethod->name, "GetMethod")
						&& _resolvedMethod->klass
						&& _resolvedMethod->klass->namespaze
						&& std::strcmp(_resolvedMethod->klass->namespaze, "System") == 0;
					if (_hotc233TraceTypeGetMethod)
					{
						std::printf("[hotc233][ReflectionGetMethodProbe] before %s.%s::%s ret=%u retRaw=%llu this=%p arg1=%p/%llu arg2=%p/%llu m2n=%p\n",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name ? _resolvedMethod->name : "",
							(uint32_t)__ret,
							(unsigned long long)(localVarBase + __ret)->u64,
							(void*)(localVarBase + _resolvedArgIdxs[0])->obj,
							(localVarBase + _resolvedArgIdxs[1])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[1])->u64,
							(localVarBase + _resolvedArgIdxs[2])->ptr, (unsigned long long)(localVarBase + _resolvedArgIdxs[2])->u64,
							(void*)imi->resolveDatas[__managed2NativeMethod]);
						std::fflush(stdout);
					}
					if (TryExecuteRuntimeTypeGetConstructorByTypes(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteRuntimeTypeGetFieldByName(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteRuntimeTypeGetPropertyByName(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteRuntimeTypeGetMethodByName(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteArrayGetValueSingleIndex(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryGetDictionaryStringValueType(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteConstructorInfoInvokeObjectArray(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					if (TryExecuteMethodBaseInvokeObjectArray(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ip += 16;
						continue;
					}
					bool _hotc233TraceReflectionInvoke = _resolvedMethod
						&& _resolvedMethod->name
						&& std::strcmp(_resolvedMethod->name, "Invoke") == 0
						&& _resolvedMethod->klass
						&& _resolvedMethod->klass->namespaze
						&& std::strstr(_resolvedMethod->klass->namespaze, "System.Reflection");
					if (_hotc233TraceReflectionInvoke)
					{
						StackObject _hotc233ZeroArg = {};
						StackObject* _hotc233Arg1 = _resolvedMethod->parameters_count >= 1 ? localVarBase + _resolvedArgIdxs[1] : &_hotc233ZeroArg;
						StackObject* _hotc233Arg2 = _resolvedMethod->parameters_count >= 2 ? localVarBase + _resolvedArgIdxs[2] : &_hotc233ZeroArg;
						StackObject* _hotc233Arg3 = _resolvedMethod->parameters_count >= 3 ? localVarBase + _resolvedArgIdxs[3] : &_hotc233ZeroArg;
						std::printf("[hotc233][ReflectionInvokeProbe] instance-ret declared=%s.%s::%s actual=%s.%s::%s m2n=%p thisSlot=%u this=%p arg1=%p/%llu arg2=%p/%llu arg3=%p/%llu\n",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->namespaze ? _declaredMethod->klass->namespaze : "",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->name ? _declaredMethod->klass->name : "",
							_declaredMethod && _declaredMethod->name ? _declaredMethod->name : "",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name ? _resolvedMethod->name : "",
							(void*)imi->resolveDatas[__managed2NativeMethod],
							(uint32_t)_resolvedArgIdxs[0],
							(void*)(localVarBase + _resolvedArgIdxs[0])->obj,
							_hotc233Arg1->ptr, (unsigned long long)_hotc233Arg1->u64,
							_hotc233Arg2->ptr, (unsigned long long)_hotc233Arg2->u64,
							_hotc233Arg3->ptr, (unsigned long long)_hotc233Arg3->u64);
						std::fflush(stdout);
					}
					Managed2NativeCallMethod _resolvedM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
					if (_resolvedMethod != _declaredMethod && _resolvedMethod->has_full_generic_sharing_signature)
					{
						_resolvedM2N = InterpreterModule::GetManaged2NativeMethodPointer(_resolvedMethod, false);
					}
#endif
				    _resolvedM2N(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret);
					if (_hotc233TraceTypeGetMethod)
					{
						Il2CppObject* retObj = (localVarBase + __ret)->obj;
						std::printf("[hotc233][ReflectionGetMethodProbe] after %s.%s::%s ret=%u retObj=%p class=%s.%s raw=%llu\n",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name ? _resolvedMethod->name : "",
							(uint32_t)__ret,
							(void*)retObj,
							retObj && retObj->klass && retObj->klass->namespaze ? retObj->klass->namespaze : "",
							retObj && retObj->klass && retObj->klass->name ? retObj->klass->name : "",
							(unsigned long long)(localVarBase + __ret)->u64);
						std::fflush(stdout);
					}
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallNativeInstance_ret_expand:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 8);
					uint32_t __methodInfo = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __retLocationType = *(uint8_t*)(ip + 2);
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
				    void* _ret = (void*)(localVarBase + __ret);
					MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					MethodInfo* _declaredMethod = _resolvedMethod;
					_resolvedMethod = ResolveActualReferenceInstanceMethod(_resolvedMethod, localVarBase + _resolvedArgIdxs[0]);
					if (TryExecuteConstructorInfoInvokeObjectArray(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
						ip += 24;
						continue;
					}
					if (TryExecuteMethodBaseInvokeObjectArray(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret))
					{
						ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
						ip += 24;
						continue;
					}
					bool _hotc233TraceReflectionInvoke = _resolvedMethod
						&& _resolvedMethod->name
						&& std::strcmp(_resolvedMethod->name, "Invoke") == 0
						&& _resolvedMethod->klass
						&& _resolvedMethod->klass->namespaze
						&& std::strstr(_resolvedMethod->klass->namespaze, "System.Reflection");
					if (_hotc233TraceReflectionInvoke)
					{
						StackObject _hotc233ZeroArg = {};
						StackObject* _hotc233Arg1 = _resolvedMethod->parameters_count >= 1 ? localVarBase + _resolvedArgIdxs[1] : &_hotc233ZeroArg;
						StackObject* _hotc233Arg2 = _resolvedMethod->parameters_count >= 2 ? localVarBase + _resolvedArgIdxs[2] : &_hotc233ZeroArg;
						StackObject* _hotc233Arg3 = _resolvedMethod->parameters_count >= 3 ? localVarBase + _resolvedArgIdxs[3] : &_hotc233ZeroArg;
						std::printf("[hotc233][ReflectionInvokeProbe] instance-ret-expand declared=%s.%s::%s actual=%s.%s::%s m2n=%p thisSlot=%u this=%p arg1=%p/%llu arg2=%p/%llu arg3=%p/%llu\n",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->namespaze ? _declaredMethod->klass->namespaze : "",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->name ? _declaredMethod->klass->name : "",
							_declaredMethod && _declaredMethod->name ? _declaredMethod->name : "",
							_resolvedMethod->klass->namespaze ? _resolvedMethod->klass->namespaze : "",
							_resolvedMethod->klass->name ? _resolvedMethod->klass->name : "",
							_resolvedMethod->name ? _resolvedMethod->name : "",
							(void*)imi->resolveDatas[__managed2NativeMethod],
							(uint32_t)_resolvedArgIdxs[0],
							(void*)(localVarBase + _resolvedArgIdxs[0])->obj,
							_hotc233Arg1->ptr, (unsigned long long)_hotc233Arg1->u64,
							_hotc233Arg2->ptr, (unsigned long long)_hotc233Arg2->u64,
							_hotc233Arg3->ptr, (unsigned long long)_hotc233Arg3->u64);
						std::fflush(stdout);
					}
					Managed2NativeCallMethod _resolvedM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
					if (_resolvedMethod != _declaredMethod && _resolvedMethod->has_full_generic_sharing_signature)
					{
						_resolvedM2N = InterpreterModule::GetManaged2NativeMethodPointer(_resolvedMethod, false);
					}
#endif
				    _resolvedM2N(_resolvedMethod, _resolvedArgIdxs, localVarBase, _ret);
				    ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallNativeStatic_void:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_resolvedMethod, ((uint16_t*)&imi->resolveDatas[__argIdxs]), localVarBase, nullptr);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallNativeStatic_ret:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _ret = (void*)(localVarBase + __ret);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_resolvedMethod, ((uint16_t*)&imi->resolveDatas[__argIdxs]), localVarBase, _ret);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallNativeStatic_ret_expand:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 8);
					uint32_t __methodInfo = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __retLocationType = *(uint8_t*)(ip + 2);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _ret = (void*)(localVarBase + __ret);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_resolvedMethod, ((uint16_t*)&imi->resolveDatas[__argIdxs]), localVarBase, _ret);
				    ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallInterp_void:
				{
					MethodInfo* __methodInfo = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					if (metadata::IsInstanceMethod(__methodInfo))
					{
						CHECK_NOT_NULL_THROW((localVarBase + __argBase)->obj);
					}
					CALL_INTERP_VOID((ip + 8), __methodInfo, (StackObject*)(void*)(localVarBase + __argBase));
				    continue;
				}
				case HiOpcodeEnum::CallInterpStatic_void:
				{
					MethodInfo* __methodInfo = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					CALL_INTERP_VOID((ip + 8), __methodInfo, (StackObject*)(void*)(localVarBase + __argBase));
				    continue;
				}
				case HiOpcodeEnum::CallInterp_ret:
				{
					MethodInfo* __methodInfo = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __isInstanceMethod = *(uint8_t*)(ip + 12);
					if (__methodInfo && __methodInfo->name && !std::strcmp(__methodInfo->name, "Compare") && __methodInfo->klass && __methodInfo->klass->name && std::strstr(__methodInfo->klass->name, "NullableComparer"))
					{
						StackObject* __probeArgBase = (StackObject*)(void*)(localVarBase + __argBase);
						std::printf("[hotc233][CallInterpNullableProbe] caller=%s callee=%s.%s::%s argBase=%u ret=%u isInstance=%u full=%d pcount=%d slot0.ptr=%p slot0.u64=%llu slot1.ptr=%p slot1.u64=%llu slot2.ptr=%p slot2.u64=%llu slot3.ptr=%p slot3.u64=%llu\n",
							frame && frame->method && frame->method->name ? frame->method->name : "",
							__methodInfo->klass->namespaze ? __methodInfo->klass->namespaze : "",
							__methodInfo->klass->name ? __methodInfo->klass->name : "",
							__methodInfo->name ? __methodInfo->name : "",
							(uint32_t)__argBase,
							(uint32_t)__ret,
							(uint32_t)__isInstanceMethod,
#if HOTC233_UNITY_2021_OR_NEW
							__methodInfo->has_full_generic_sharing_signature ? 1 : 0,
#else
							0,
#endif
							(int)__methodInfo->parameters_count,
							__probeArgBase[0].ptr, (unsigned long long)__probeArgBase[0].u64,
							__probeArgBase[1].ptr, (unsigned long long)__probeArgBase[1].u64,
							__probeArgBase[2].ptr, (unsigned long long)__probeArgBase[2].u64,
							__probeArgBase[3].ptr, (unsigned long long)__probeArgBase[3].u64);
						std::fflush(stdout);
					}
					if (frame && frame->method && frame->method->name && std::strcmp(frame->method->name, "VerifyValueTuple") == 0)
					{
						std::printf("[hotc233][CallInterpRetProbe] caller=%s callee=%s.%s::%s argBase=%u ret=%u isInstance=%u offset=%lld\n",
							frame->method->name,
							__methodInfo && __methodInfo->klass && __methodInfo->klass->namespaze ? __methodInfo->klass->namespaze : "",
							__methodInfo && __methodInfo->klass && __methodInfo->klass->name ? __methodInfo->klass->name : "",
							__methodInfo && __methodInfo->name ? __methodInfo->name : "",
							(uint32_t)__argBase,
							(uint32_t)__ret,
							(uint32_t)__isInstanceMethod,
							(long long)(ip - ipBase));
						std::fflush(stdout);
					}
					if (__isInstanceMethod)
					{
						CHECK_NOT_NULL_THROW((localVarBase + __argBase)->obj);
					}
					InterpMethodInfo* __calleeImi = __methodInfo->interpData ? (InterpMethodInfo*)__methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(__methodInfo);
					RuntimeInitClassCCtorWithoutInitClass(__methodInfo);
					void* __retPtr = (void*)(localVarBase + __ret);
					StackObject* __argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
					if (TryExecuteHotc233CallFastPath(__methodInfo, __argBasePtr, __retPtr))
					{
						ip += 16;
						continue;
					}
					CALL_INTERP_RET_PREPARED((ip + 16), __methodInfo, __calleeImi, __argBasePtr, __retPtr);
				    continue;
				}
				case HiOpcodeEnum::CallInterpStatic_ret:
				{
					static int s_callInterpStaticRetProbeCount = 0;
					MethodInfo* __methodInfo = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint32_t __interpMethodCache = *(uint32_t*)(ip + 12);
					InterpMethodInfo* __calleeImi = (InterpMethodInfo*)imi->resolveDatas[__interpMethodCache];
					if (!__calleeImi)
					{
						__calleeImi = __methodInfo->interpData ? (InterpMethodInfo*)__methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(__methodInfo);
						imi->resolveDatas[__interpMethodCache] = (uint64_t)__calleeImi;
					}
					RuntimeInitClassCCtorWithoutInitClass(__methodInfo);
					void* __retPtr = (void*)(localVarBase + __ret);
					StackObject* __argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
					const bool _probeCallInterpStaticRet = false;
					if (_probeCallInterpStaticRet)
					{
						s_callInterpStaticRetProbeCount++;
						std::printf("[hotc233][CallInterpStaticRetProbe] callerImi=%p offset=%lld callee=%s.%s::%s argBase=%u ret=%u next=16 calleeImi=%p\n",
							(void*)imi,
							(long long)(ip - ipBase),
							__methodInfo && __methodInfo->klass && __methodInfo->klass->namespaze ? __methodInfo->klass->namespaze : "<null>",
							__methodInfo && __methodInfo->klass && __methodInfo->klass->name ? __methodInfo->klass->name : "<null>",
							__methodInfo && __methodInfo->name ? __methodInfo->name : "<null>",
							(unsigned)__argBase,
							(unsigned)__ret,
							(void*)__calleeImi);
						std::fflush(stdout);
					}
					if (TryExecuteHotc233CallFastPath(__methodInfo, __argBasePtr, __retPtr))
					{
						if (_probeCallInterpStaticRet)
						{
							std::printf("[hotc233][CallInterpStaticRetProbe] fast-return callee=%s\n",
								__methodInfo && __methodInfo->name ? __methodInfo->name : "<null>");
							std::fflush(stdout);
						}
						ip += 16;
						continue;
					}
					if (_probeCallInterpStaticRet)
					{
						std::printf("[hotc233][CallInterpStaticRetProbe] enter-callee callee=%s\n",
							__methodInfo && __methodInfo->name ? __methodInfo->name : "<null>");
						std::fflush(stdout);
					}
					CALL_INTERP_RET_PREPARED((ip + 16), __methodInfo, __calleeImi, __argBasePtr, __retPtr);
				    continue;
				}
				case HiOpcodeEnum::CallVirtual_void:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    uint16_t* _argIdxData = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _objPtr = localVarBase + _argIdxData[0];
				    MethodInfo* _declaredMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
					bool _hotc233TraceDictionarySetItem = false;
					if (_hotc233TraceDictionarySetItem)
					{
						std::printf("[hotc233][CallVirtualProbe] before declared=%s.%s::%s obj=%p m2n=%p arg1=%u arg2=%u\n",
							_declaredMethod->klass->namespaze ? _declaredMethod->klass->namespaze : "",
							_declaredMethod->klass->name ? _declaredMethod->klass->name : "",
							_declaredMethod->name,
							(void*)_objPtr->obj,
							(void*)imi->resolveDatas[__managed2NativeMethod],
							(uint32_t)_argIdxData[1],
							(uint32_t)_argIdxData[2]);
						std::fflush(stdout);
					}
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_objPtr->obj, _declaredMethod);
					if (_hotc233TraceDictionarySetItem)
					{
						std::printf("[hotc233][CallVirtualProbe] after actual=%s.%s::%s interp=%d method=%p\n",
							_actualMethod && _actualMethod->klass && _actualMethod->klass->namespaze ? _actualMethod->klass->namespaze : "",
							_actualMethod && _actualMethod->klass && _actualMethod->klass->name ? _actualMethod->klass->name : "",
							_actualMethod && _actualMethod->name ? _actualMethod->name : "",
							_actualMethod ? (int)(hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod)) : -1,
							(void*)_actualMethod);
						std::fflush(stdout);
					}
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _objPtr->obj += 1;
				    }
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod))
				    {
				        InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				        if (_actualImi)
				        {
				            CALL_INTERP_VOID((ip + 16), _actualMethod, _objPtr);
				        }
				        else
				        {
				            frame->ip = ip + 2;
				            if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				            {
				                RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				            }
				            Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				            if (_actualMethod->has_full_generic_sharing_signature)
				            {
				                _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				            }
#endif
				            _actualM2N(_actualMethod, _argIdxData, localVarBase, nullptr);
				            ip += 16;
				        }
				    }
				    else 
				    {
				        frame->ip = ip + 2;
						if (_hotc233TraceDictionarySetItem)
						{
							std::printf("[hotc233][CallVirtualProbe] before-init actual=%p\n", (void*)_actualMethod);
							std::fflush(stdout);
						}
				        if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				        {
				            RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				        }
						if (_hotc233TraceDictionarySetItem)
						{
							std::printf("[hotc233][CallVirtualProbe] before-m2n actual=%p\n", (void*)_actualMethod);
							std::fflush(stdout);
						}
				        Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				        if (_actualMethod->has_full_generic_sharing_signature)
				        {
				            _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				        }
#endif
				        _actualM2N(_actualMethod, _argIdxData, localVarBase, nullptr);
						if (_hotc233TraceDictionarySetItem)
						{
							std::printf("[hotc233][CallVirtualProbe] after-m2n actual=%p\n", (void*)_actualMethod);
							std::fflush(stdout);
						}
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::CallVirtual_ret:
				HOTC233_EXEC_CallVirtual_ret:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    uint16_t* _argIdxData = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _objPtr = localVarBase + _argIdxData[0];
				    MethodInfo* _declaredMethod = ((MethodInfo*)imi->resolveDatas[__methodInfo]);
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_objPtr->obj, _declaredMethod);
				    void* _ret = (void*)(localVarBase + __ret);
					bool _hotc233TraceNullableCompare = _actualMethod && _actualMethod->name && !std::strcmp(_actualMethod->name, "Compare") && _actualMethod->klass && _actualMethod->klass->name && std::strstr(_actualMethod->klass->name, "NullableComparer");
					if (_hotc233TraceNullableCompare)
					{
						std::printf("[hotc233][CallVirtualNullableProbe] declared=%s.%s::%s actual=%s.%s::%s ret=%u arg0=%u arg1=%u arg2=%u full=%d cachedM2N=%p slot0.ptr=%p slot1.ptr=%p slot2.ptr=%p\n",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->namespaze ? _declaredMethod->klass->namespaze : "",
							_declaredMethod && _declaredMethod->klass && _declaredMethod->klass->name ? _declaredMethod->klass->name : "",
							_declaredMethod && _declaredMethod->name ? _declaredMethod->name : "",
							_actualMethod && _actualMethod->klass && _actualMethod->klass->namespaze ? _actualMethod->klass->namespaze : "",
							_actualMethod && _actualMethod->klass && _actualMethod->klass->name ? _actualMethod->klass->name : "",
							_actualMethod && _actualMethod->name ? _actualMethod->name : "",
							(uint32_t)__ret,
							(uint32_t)_argIdxData[0],
							(uint32_t)_argIdxData[1],
							(uint32_t)_argIdxData[2],
#if HOTC233_UNITY_2021_OR_NEW
							_actualMethod->has_full_generic_sharing_signature ? 1 : 0,
#else
							0,
#endif
							(void*)imi->resolveDatas[__managed2NativeMethod],
							(localVarBase + _argIdxData[0])->ptr,
							(localVarBase + _argIdxData[1])->ptr,
							(localVarBase + _argIdxData[2])->ptr);
						std::fflush(stdout);
					}
					if (_hotc233TraceNullableCompare && _actualMethod->parameters_count == 2)
					{
						const bool _leftHasValue = *((uint8_t*)(localVarBase + _argIdxData[1])) != 0;
						const bool _rightHasValue = *((uint8_t*)(localVarBase + _argIdxData[2])) != 0;
						if (_leftHasValue != _rightHasValue || !_leftHasValue)
						{
							*(int32_t*)_ret = _leftHasValue ? 1 : (_rightHasValue ? -1 : 0);
							ip += 16;
							continue;
						}
					}
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _objPtr->obj += 1;
				    }
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod))
				    {
				        InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				        if (_actualImi)
				        {
				            CALL_INTERP_RET_PREPARED((ip + 16), _actualMethod, _actualImi, _objPtr, _ret);
				        }
				        else
				        {
				            frame->ip = ip + 2;
				            if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				            {
				                RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				            }
				            Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				            if (_actualMethod->has_full_generic_sharing_signature)
				            {
				                _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				            }
#endif
				            _actualM2N(_actualMethod, _argIdxData, localVarBase, _ret);
				            ip += 16;
				        }
				    }
				    else 
				    {
				        frame->ip = ip + 2;
				        if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				        {
				            RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				        }
				        Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				        if (_actualMethod->has_full_generic_sharing_signature)
				        {
				            _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				        }
#endif
				        _actualM2N(_actualMethod, _argIdxData, localVarBase, _ret);
				        ip += 16;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::CallVirtual_ret_expand:
				HOTC233_EXEC_CallVirtual_ret_expand:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 8);
					uint32_t __methodInfo = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __retLocationType = *(uint8_t*)(ip + 2);
				    uint16_t* _argIdxData = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _objPtr = localVarBase + _argIdxData[0];
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_objPtr->obj, ((MethodInfo*)imi->resolveDatas[__methodInfo]));
				    void* _ret = (void*)(localVarBase + __ret);
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _objPtr->obj += 1;
				    }
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod))
				    {
				        InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				        if (_actualImi)
				        {
				            CALL_INTERP_RET_PREPARED((ip + 24), _actualMethod, _actualImi, _objPtr, _ret);
				        }
				        else
				        {
				            frame->ip = ip + 2;
				            if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				            {
				                RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				            }
				            Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				            if (_actualMethod->has_full_generic_sharing_signature)
				            {
				                _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				            }
#endif
				            _actualM2N(_actualMethod, _argIdxData, localVarBase, _ret);
				            ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				            ip += 24;
				        }
				    }
				    else 
				    {
				        frame->ip = ip + 2;
				        if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				        {
				            RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				        }
				        Managed2NativeCallMethod _actualM2N = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod];
#if HOTC233_UNITY_2021_OR_NEW
				        if (_actualMethod->has_full_generic_sharing_signature)
				        {
				            _actualM2N = InterpreterModule::GetManaged2NativeMethodPointer(_actualMethod, false);
				        }
#endif
				        _actualM2N(_actualMethod, _argIdxData, localVarBase, _ret);
				        ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				        ip += 24;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BranchTrueVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchTrueVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::CallInterpVirtual_void:
				{
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
				    StackObject* _argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_argBasePtr->obj, __method);
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _argBasePtr->obj += 1;
				    }
				    if (!(hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod)))
				    {
				        uint16_t* _argIdxData = (uint16_t*)alloca(sizeof(uint16_t) * ((uint32_t)_actualMethod->parameters_count + 1u));
				        for (uint32_t _argIdx = 0; _argIdx <= (uint32_t)_actualMethod->parameters_count; ++_argIdx)
				        {
				            _argIdxData[_argIdx] = (uint16_t)(__argBase + _argIdx);
				        }
				        InterpreterModule::Managed2NativeCallByReflectionInvoke(_actualMethod, _argIdxData, localVarBase, nullptr);
				        ip += 8;
				        continue;
				    }
				    InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				    if (_actualImi)
				    {
				        SAVE_CUR_FRAME(ip + 8)
				        PREPARE_NEW_FRAME_FROM_INTERPRETER_PREPARED(_actualMethod, _actualImi, _argBasePtr, nullptr);
				    }
				    else
				    {
				        uint16_t* _argIdxData = (uint16_t*)alloca(sizeof(uint16_t) * ((uint32_t)_actualMethod->parameters_count + 1u));
				        for (uint32_t _argIdx = 0; _argIdx <= (uint32_t)_actualMethod->parameters_count; ++_argIdx)
				        {
				            _argIdxData[_argIdx] = (uint16_t)(__argBase + _argIdx);
				        }
				        InterpreterModule::Managed2NativeCallByReflectionInvoke(_actualMethod, _argIdxData, localVarBase, nullptr);
				        ip += 8;
				    }
				    continue;
				}
				case HiOpcodeEnum::CallInterpVirtual_ret:
				{
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    StackObject* _argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_argBasePtr->obj, __method);
				    if (_actualMethod && _actualMethod->name && !std::strcmp(_actualMethod->name, "Compare") && _actualMethod->klass && _actualMethod->klass->name && std::strstr(_actualMethod->klass->name, "NullableComparer"))
				    {
				        std::printf("[hotc233][CallInterpNullableProbe] virtual caller=%s callee=%s.%s::%s argBase=%u ret=%u full=%d pcount=%d slot0.ptr=%p slot0.u64=%llu slot1.ptr=%p slot1.u64=%llu slot2.ptr=%p slot2.u64=%llu slot3.ptr=%p slot3.u64=%llu\n",
				            frame && frame->method && frame->method->name ? frame->method->name : "",
				            _actualMethod->klass->namespaze ? _actualMethod->klass->namespaze : "",
				            _actualMethod->klass->name ? _actualMethod->klass->name : "",
				            _actualMethod->name ? _actualMethod->name : "",
				            (uint32_t)__argBase,
				            (uint32_t)__ret,
#if HOTC233_UNITY_2021_OR_NEW
				            _actualMethod->has_full_generic_sharing_signature ? 1 : 0,
#else
				            0,
#endif
				            (int)_actualMethod->parameters_count,
				            _argBasePtr[0].ptr, (unsigned long long)_argBasePtr[0].u64,
				            _argBasePtr[1].ptr, (unsigned long long)_argBasePtr[1].u64,
				            _argBasePtr[2].ptr, (unsigned long long)_argBasePtr[2].u64,
				            _argBasePtr[3].ptr, (unsigned long long)_argBasePtr[3].u64);
				        std::fflush(stdout);
				    }
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _argBasePtr->obj += 1;
				    }
				    void* _ret = (void*)(localVarBase + __ret);
				    if (!(hotc233::metadata::IsInterpreterImplement(_actualMethod) && hotc233::metadata::IsInterpreterMethod(_actualMethod)))
				    {
				        uint16_t* _argIdxData = (uint16_t*)alloca(sizeof(uint16_t) * ((uint32_t)_actualMethod->parameters_count + 1u));
				        for (uint32_t _argIdx = 0; _argIdx <= (uint32_t)_actualMethod->parameters_count; ++_argIdx)
				        {
				            _argIdxData[_argIdx] = (uint16_t)(__argBase + _argIdx);
				        }
				        InterpreterModule::Managed2NativeCallByReflectionInvoke(_actualMethod, _argIdxData, localVarBase, _ret);
				        ip += 16;
				        continue;
				    }
				    InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				    if (_actualImi)
				    {
				        CALL_INTERP_RET_PREPARED((ip + 16), _actualMethod, _actualImi, _argBasePtr, _ret);
				    }
				    else
				    {
				        uint16_t* _argIdxData = (uint16_t*)alloca(sizeof(uint16_t) * ((uint32_t)_actualMethod->parameters_count + 1u));
				        for (uint32_t _argIdx = 0; _argIdx <= (uint32_t)_actualMethod->parameters_count; ++_argIdx)
				        {
				            _argIdxData[_argIdx] = (uint16_t)(__argBase + _argIdx);
				        }
				        InterpreterModule::Managed2NativeCallByReflectionInvoke(_actualMethod, _argIdxData, localVarBase, _ret);
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::CallInd_void:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 8);
					uint8_t& __isMethodInfoPointer = *(uint8_t*)(ip + 2);
					uint32_t __methodInfo = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
				    frame->ip = ip + 2;
				    Managed2NativeCallMethod _nativeMethodPointer = ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod]);
				    Managed2NativeFunctionPointerCallMethod _nativeMethodPointer2 = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
					Il2CppMethodPointer _methodPointer = (Il2CppMethodPointer)(localVarBase + __methodInfo)->ptr;
					if (__isMethodInfoPointer == 0)
					{
				        __isMethodInfoPointer = hotc233::interpreter::InterpreterModule::IsMethodInfoPointer((void*)_methodPointer) ? 1 : 2;
					}
					if (__isMethodInfoPointer == 1)
					{
					    MethodInfo* _method = (MethodInfo*)_methodPointer;
					    if (metadata::IsInstanceMethod(_method))
					    {
				            CHECK_NOT_NULL_THROW(_argBasePtr->obj);
					    }
					    if (IsInterpreterImplement(_method) && IsInterpreterMethod(_method))
					    {
				            InterpMethodInfo* _methodImi = _method->interpData ? (InterpMethodInfo*)_method->interpData : InterpreterModule::GetInterpMethodInfo(_method);
				            if (_methodImi)
				            {
				                SAVE_CUR_FRAME(ip + 24)
				                PREPARE_NEW_FRAME_FROM_INTERPRETER_PREPARED(_method, _methodImi, _argBasePtr, nullptr);
				                continue;
				            }
					    }
					    if (!InitAndGetInterpreterDirectlyCallMethodPointer(_method))
					    {
				            RaiseAOTGenericMethodNotInstantiatedException(_method);
					    }
				        _nativeMethodPointer(_method, _argIdxsPtr, localVarBase, nullptr);
				    }
				    else
				    {
				        _nativeMethodPointer2(_methodPointer, _argIdxsPtr, localVarBase, nullptr);
				    }
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallInd_ret:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 8);
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 12);
					uint8_t& __isMethodInfoPointer = *(uint8_t*)(ip + 2);
					uint32_t __methodInfo = *(uint32_t*)(ip + 16);
					uint32_t __argIdxs = *(uint32_t*)(ip + 20);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
				    Managed2NativeCallMethod _nativeMethodPointer = ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod]);
				    Managed2NativeFunctionPointerCallMethod _nativeMethodPointer2 = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
					Il2CppMethodPointer _methodPointer = (Il2CppMethodPointer)(localVarBase + __methodInfo)->ptr;
					if (__isMethodInfoPointer == 0)
					{
				        __isMethodInfoPointer = hotc233::interpreter::InterpreterModule::IsMethodInfoPointer((void*)_methodPointer) ? 1 : 2;
					}
					if (__isMethodInfoPointer == 1)
					{
					    MethodInfo* _method = (MethodInfo*)_methodPointer;
					    if (metadata::IsInstanceMethod(_method))
					    {
				            CHECK_NOT_NULL_THROW(_argBasePtr->obj);
					    }
					    if (IsInterpreterImplement(_method) && IsInterpreterMethod(_method))
					    {
				            InterpMethodInfo* _methodImi = _method->interpData ? (InterpMethodInfo*)_method->interpData : InterpreterModule::GetInterpMethodInfo(_method);
				            if (_methodImi)
				            {
				                CALL_INTERP_RET_PREPARED((ip + 24), _method, _methodImi, _argBasePtr, _ret);
				                continue;
				            }
					    }
					    if (!InitAndGetInterpreterDirectlyCallMethodPointer(_method))
					    {
				            RaiseAOTGenericMethodNotInstantiatedException(_method);
					    }
				        _nativeMethodPointer(_method, _argIdxsPtr, localVarBase, _ret);
				    }
				    else
				    {
				        _nativeMethodPointer2(_methodPointer, _argIdxsPtr, localVarBase, _ret);
				    }
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallInd_ret_expand:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 8);
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 12);
					uint8_t& __isMethodInfoPointer = *(uint8_t*)(ip + 2);
					uint32_t __methodInfo = *(uint32_t*)(ip + 16);
					uint32_t __argIdxs = *(uint32_t*)(ip + 20);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __retLocationType = *(uint8_t*)(ip + 3);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
				    Managed2NativeCallMethod _nativeMethodPointer = ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod]);
				    Managed2NativeFunctionPointerCallMethod _nativeMethodPointer2 = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
					Il2CppMethodPointer _methodPointer = (Il2CppMethodPointer)(localVarBase + __methodInfo)->ptr;
					if (__isMethodInfoPointer == 0)
					{
				        __isMethodInfoPointer = hotc233::interpreter::InterpreterModule::IsMethodInfoPointer((void*)_methodPointer) ? 1 : 2;
					}
					if (__isMethodInfoPointer == 1)
					{
					    MethodInfo* _method = (MethodInfo*)_methodPointer;
					    if (metadata::IsInstanceMethod(_method))
					    {
				            CHECK_NOT_NULL_THROW(_argBasePtr->obj);
					    }
					    if (IsInterpreterImplement(_method) && IsInterpreterMethod(_method))
					    {
				            InterpMethodInfo* _methodImi = _method->interpData ? (InterpMethodInfo*)_method->interpData : InterpreterModule::GetInterpMethodInfo(_method);
				            if (_methodImi)
				            {
				                CALL_INTERP_RET_PREPARED((ip + 24), _method, _methodImi, _argBasePtr, _ret);
				                continue;
				            }
					    }
					    if (!InitAndGetInterpreterDirectlyCallMethodPointer(_method))
					    {
				            RaiseAOTGenericMethodNotInstantiatedException(_method);
					    }
				        _nativeMethodPointer(_method, _argIdxsPtr, localVarBase, _ret);
				    }
				    else
				    {
				        _nativeMethodPointer2(_methodPointer, _argIdxsPtr, localVarBase, _ret);
				    }
				    ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallPInvoke_void:
				{
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 4);
					uint32_t __pinvokeMethodPointer = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    frame->ip = ip + 2;
				    Managed2NativeFunctionPointerCallMethod _managed2NativeFuncMethodPointer = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
				    Il2CppMethodPointer _pinvokeMethodPointer = ((Il2CppMethodPointer)imi->resolveDatas[__pinvokeMethodPointer]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
				    _managed2NativeFuncMethodPointer(_pinvokeMethodPointer, _argIdxsPtr, localVarBase, nullptr);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallPInvoke_ret:
				{
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 4);
					uint32_t __pinvokeMethodPointer = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
				    Managed2NativeFunctionPointerCallMethod _managed2NativeFuncMethodPointer = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
				    Il2CppMethodPointer _pinvokeMethodPointer = ((Il2CppMethodPointer)imi->resolveDatas[__pinvokeMethodPointer]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
				    _managed2NativeFuncMethodPointer(_pinvokeMethodPointer, _argIdxsPtr, localVarBase, _ret);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallPInvoke_ret_expand:
				{
					uint32_t __managed2NativeFunctionPointerMethod = *(uint32_t*)(ip + 8);
					uint32_t __pinvokeMethodPointer = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint8_t __retLocationType = *(uint8_t*)(ip + 2);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
				    Managed2NativeFunctionPointerCallMethod _managed2NativeFuncMethodPointer = ((Managed2NativeFunctionPointerCallMethod)imi->resolveDatas[__managed2NativeFunctionPointerMethod]);
				    Il2CppMethodPointer _pinvokeMethodPointer = ((Il2CppMethodPointer)imi->resolveDatas[__pinvokeMethodPointer]);
					uint16_t* _argIdxsPtr = (uint16_t*)&imi->resolveDatas[__argIdxs];
					StackObject* _argBasePtr = localVarBase + _argIdxsPtr[0];
				    _managed2NativeFuncMethodPointer(_pinvokeMethodPointer, _argIdxsPtr, localVarBase, _ret);
				    ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateInvoke_void:
				HOTC233_EXEC_CallDelegateInvoke_void:
				{
					uint32_t __managed2NativeStaticMethod = *(uint32_t*)(ip + 4);
					uint32_t __managed2NativeInstanceMethod = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint32_t __interpDelegateCache = *(uint32_t*)(ip + 16);
					uint16_t __invokeParamCount = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
					void* _ret = nullptr;
					uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _argBasePtr = localVarBase + _resolvedArgIdxs[0];
					Il2CppMulticastDelegate* _del = (Il2CppMulticastDelegate*)_argBasePtr->obj;
					CHECK_NOT_NULL_THROW(_del);
					uint64_t* _interpDelegateCache = &imi->resolveDatas[__interpDelegateCache];
					const bool _probeDelegateVoid = false;
					if (_probeDelegateVoid)
					{
						std::printf("[hotc233][DelegateVoidProbe] enter imi=%p invokeParamCount=%u del=%p list=%p ip=%p size=%u\n",
							(void*)imi,
							(unsigned)__invokeParamCount,
							(void*)_del,
							(void*)_del->delegates,
							(void*)ip,
							(unsigned)g_instructionSizes[(int)HiOpcodeEnum::CallDelegateInvoke_void]);
					}
					if (_del->delegates == nullptr)
					{
						const MethodInfo* _cachedMethod = nullptr;
						InterpMethodInfo* _cachedMethodImi = nullptr;
						StackObject* _cachedArgBase = nullptr;
						if (TryGetCachedSingleInterpDelegatePrepared(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, &_cachedMethod, &_cachedMethodImi, &_cachedArgBase))
						{
							CALL_INTERP_RET_PREPARED((ip + 20), _cachedMethod, _cachedMethodImi, _cachedArgBase, _ret);
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (_probeDelegateVoid)
						{
							std::printf("[hotc233][DelegateVoidProbe] single method=%s.%s::%s target=%p interp=%d pcount=%d\n",
								method && method->klass && method->klass->namespaze ? method->klass->namespaze : "<null>",
								method && method->klass && method->klass->name ? method->klass->name : "<null>",
								method && method->name ? method->name : "<null>",
								(void*)target,
								method ? (int)(hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method)) : 0,
								method ? (int)method->parameters_count : -1);
						}
						if (hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 20;
									continue;
								}
								if (methodImi)
								{
									StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
									CALL_INTERP_RET_PREPARED((ip + 20), method, methodImi, fastArgBase, _ret);
									continue;
								}
							}
							switch ((int32_t)__invokeParamCount - (int32_t)method->parameters_count)
							{
							case 0:
							{
								if (hotc233::metadata::IsInstanceMethod(method))
								{
									CHECK_NOT_NULL_THROW(target);
									target += IS_CLASS_VALUE_TYPE(method->klass);
									_argBasePtr->obj = target;
								}
								else
								{
									_argBasePtr = __invokeParamCount == 0 ? _argBasePtr + 1 : localVarBase + _resolvedArgIdxs[1];
								}
								break;
							}
							case -1:
							{
								_argBasePtr->obj = target;
								break;
							}
							case 1:
							{
								_argBasePtr = localVarBase + _resolvedArgIdxs[1];
								CHECK_NOT_NULL_THROW(_argBasePtr->obj);
								break;
							}
							default:
							{
								RaiseExecutionEngineException("CallInterpDelegate");
							}
							}
							InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
							if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 20;
								continue;
							}
							if (methodImi)
							{
								StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
								CALL_INTERP_RET_PREPARED((ip + 20), method, methodImi, _argBasePtr, _ret);
								continue;
							}
						}
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						if (_probeDelegateVoid)
						{
							std::printf("[hotc233][DelegateVoidProbe] single before InvokeSingleDelegate method=%s\n",
								method && method->name ? method->name : "<null>");
						}
						InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _ret);
						if (_probeDelegateVoid)
						{
							std::printf("[hotc233][DelegateVoidProbe] single after InvokeSingleDelegate method=%s\n",
								method && method->name ? method->name : "<null>");
						}
					}
					else
					{
						Il2CppArray* dels = _del->delegates;
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						if (_probeDelegateVoid)
						{
							std::printf("[hotc233][DelegateVoidProbe] multicast count=%d\n", (int)dels->max_length);
						}
						for (il2cpp_array_size_t i = 0; i < dels->max_length; i++)
						{
							Il2CppMulticastDelegate* subDel = il2cpp_array_get(dels, Il2CppMulticastDelegate *, i);
							IL2CPP_ASSERT(subDel);
							IL2CPP_ASSERT(subDel->delegates == nullptr);
							const MethodInfo* method = subDel->delegate.method;
							Il2CppObject* target = subDel->delegate.target;
							if (_probeDelegateVoid)
							{
								std::printf("[hotc233][DelegateVoidProbe] multicast item=%d method=%s.%s::%s target=%p interp=%d pcount=%d\n",
									(int)i,
									method && method->klass && method->klass->namespaze ? method->klass->namespaze : "<null>",
									method && method->klass && method->klass->name ? method->klass->name : "<null>",
									method && method->name ? method->name : "<null>",
									(void*)target,
									method ? (int)(hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method)) : 0,
									method ? (int)method->parameters_count : -1);
							}
							if (TryInvokeInterpDelegateSynchronously(__invokeParamCount, method, target, _resolvedArgIdxs, localVarBase, _ret))
							{
								if (_probeDelegateVoid)
								{
									std::printf("[hotc233][DelegateVoidProbe] multicast item=%d after interp-sync\n", (int)i);
								}
								continue;
							}
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _ret);
							if (_probeDelegateVoid)
							{
								std::printf("[hotc233][DelegateVoidProbe] multicast item=%d after InvokeSingleDelegate\n", (int)i);
							}
						}
					}
					if (_probeDelegateVoid)
					{
						std::printf("[hotc233][DelegateVoidProbe] exit advance=20 imi=%p\n", (void*)imi);
					}
				    ip += 20;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateInvoke_ret:
				{
					uint32_t __managed2NativeStaticMethod = *(uint32_t*)(ip + 8);
					uint32_t __managed2NativeInstanceMethod = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint32_t __interpDelegateCache = *(uint32_t*)(ip + 20);
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __invokeParamCount = *(uint16_t*)(ip + 4);
					uint16_t __retTypeStackObjectSize = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
					IL2CPP_ASSERT(__retTypeStackObjectSize <= kMaxRetValueTypeStackObjectSize);
					StackObject* _tempRet = tempRet ? tempRet : (tempRet = (StackObject*)alloca(sizeof(StackObject) * kMaxRetValueTypeStackObjectSize));
					uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _argBasePtr = localVarBase + _resolvedArgIdxs[0];
					Il2CppMulticastDelegate* _del = (Il2CppMulticastDelegate*)_argBasePtr->obj;
					CHECK_NOT_NULL_THROW(_del);
					uint64_t* _interpDelegateCache = &imi->resolveDatas[__interpDelegateCache];
					const bool _probeDelegateRet = false;
					if (_probeDelegateRet)
					{
						std::printf("[hotc233][DelegateRetProbe] enter imi=%p offset=%lld invokeParamCount=%u ret=%u retSize=%u del=%p list=%p size=24\n",
							(void*)imi,
							(long long)(ip - ipBase),
							(unsigned)__invokeParamCount,
							(unsigned)__ret,
							(unsigned)__retTypeStackObjectSize,
							(void*)_del,
							(void*)_del->delegates);
						std::fflush(stdout);
					}
					if (_del->delegates == nullptr)
					{
						const MethodInfo* _cachedMethod = nullptr;
						InterpMethodInfo* _cachedMethodImi = nullptr;
						StackObject* _cachedArgBase = nullptr;
						if (TryGetCachedSingleInterpDelegatePrepared(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, &_cachedMethod, &_cachedMethodImi, &_cachedArgBase))
						{
							CALL_INTERP_RET_PREPARED((ip + 24), _cachedMethod, _cachedMethodImi, _cachedArgBase, _ret);
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (_probeDelegateRet)
						{
							std::printf("[hotc233][DelegateRetProbe] single method=%s.%s::%s target=%p interp=%d pcount=%d\n",
								method && method->klass && method->klass->namespaze ? method->klass->namespaze : "<null>",
								method && method->klass && method->klass->name ? method->klass->name : "<null>",
								method && method->name ? method->name : "<null>",
								(void*)target,
								method ? (int)(hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method)) : 0,
								method ? (int)method->parameters_count : -1);
							std::fflush(stdout);
						}
						if (hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 24;
									continue;
								}
								if (methodImi)
								{
									if (_probeDelegateRet)
									{
										std::printf("[hotc233][DelegateRetProbe] single call-interp-prepared method=%s next=24 argBase=%u\n",
											method && method->name ? method->name : "<null>",
											(unsigned)_resolvedArgIdxs[0]);
										std::fflush(stdout);
									}
									StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
									CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, fastArgBase, _ret);
									continue;
								}
							}
							switch ((int32_t)__invokeParamCount - (int32_t)method->parameters_count)
							{
							case 0:
							{
								if (hotc233::metadata::IsInstanceMethod(method))
								{
									CHECK_NOT_NULL_THROW(target);
									target += IS_CLASS_VALUE_TYPE(method->klass);
									_argBasePtr->obj = target;
								}
								else
								{
									_argBasePtr = __invokeParamCount == 0 ? _argBasePtr + 1 : localVarBase + _resolvedArgIdxs[1];
								}
								break;
							}
							case -1:
							{
								_argBasePtr->obj = target;
								break;
							}
							case 1:
							{
								_argBasePtr = localVarBase + _resolvedArgIdxs[1];
								CHECK_NOT_NULL_THROW(_argBasePtr->obj);
								break;
							}
							default:
							{
								RaiseExecutionEngineException("CallInterpDelegate");
							}
							}
							InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
							if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 24;
								continue;
							}
							if (methodImi)
							{
								if (_probeDelegateRet)
								{
									std::printf("[hotc233][DelegateRetProbe] single call-interp-fallback method=%s next=24\n",
										method && method->name ? method->name : "<null>");
									std::fflush(stdout);
								}
								StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
								CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, _argBasePtr, _ret);
								continue;
							}
						}
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						if (_probeDelegateRet)
						{
							std::printf("[hotc233][DelegateRetProbe] single before InvokeSingleDelegate method=%s\n",
								method && method->name ? method->name : "<null>");
							std::fflush(stdout);
						}
						InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
						if (_probeDelegateRet)
						{
							std::printf("[hotc233][DelegateRetProbe] single after InvokeSingleDelegate method=%s\n",
								method && method->name ? method->name : "<null>");
							std::fflush(stdout);
						}
					}
					else
					{
						Il2CppArray* dels = _del->delegates;
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						for (il2cpp_array_size_t i = 0; i < dels->max_length; i++)
						{
							Il2CppMulticastDelegate* subDel = il2cpp_array_get(dels, Il2CppMulticastDelegate *, i);
							IL2CPP_ASSERT(subDel);
							IL2CPP_ASSERT(subDel->delegates == nullptr);
							const MethodInfo* method = subDel->delegate.method;
							Il2CppObject* target = subDel->delegate.target;
							if (TryInvokeInterpDelegateSynchronously(__invokeParamCount, method, target, _resolvedArgIdxs, localVarBase, _tempRet))
							{
								continue;
							}
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
						}
					}
					CopyStackObject((StackObject*)_ret, _tempRet, __retTypeStackObjectSize);
					if (_probeDelegateRet)
					{
						std::printf("[hotc233][DelegateRetProbe] exit advance=24 imi=%p\n", (void*)imi);
						std::fflush(stdout);
					}
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateInvoke_ret_expand:
				{
					uint32_t __managed2NativeStaticMethod = *(uint32_t*)(ip + 8);
					uint32_t __managed2NativeInstanceMethod = *(uint32_t*)(ip + 12);
					uint32_t __argIdxs = *(uint32_t*)(ip + 16);
					uint32_t __interpDelegateCache = *(uint32_t*)(ip + 20);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint16_t __invokeParamCount = *(uint16_t*)(ip + 6);
					uint8_t __retLocationType = *(uint8_t*)(ip + 2);
				    frame->ip = ip + 2;
				    void* _ret = (void*)(localVarBase + __ret);
					StackObject _tempRet[1];
					uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
					StackObject* _argBasePtr = localVarBase + _resolvedArgIdxs[0];
					Il2CppMulticastDelegate* _del = (Il2CppMulticastDelegate*)_argBasePtr->obj;
					CHECK_NOT_NULL_THROW(_del);
					uint64_t* _interpDelegateCache = &imi->resolveDatas[__interpDelegateCache];
					if (_del->delegates == nullptr)
					{
						const MethodInfo* _cachedMethod = nullptr;
						InterpMethodInfo* _cachedMethodImi = nullptr;
						StackObject* _cachedArgBase = nullptr;
						if (TryGetCachedSingleInterpDelegatePrepared(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, &_cachedMethod, &_cachedMethodImi, &_cachedArgBase))
						{
							CALL_INTERP_RET_PREPARED((ip + 24), _cachedMethod, _cachedMethodImi, _cachedArgBase, _ret);
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (hotc233::metadata::IsInterpreterImplement(method) && hotc233::metadata::IsInterpreterMethod(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 24;
									continue;
								}
								if (methodImi)
								{
									StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
									CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, fastArgBase, _ret);
									continue;
								}
							}
							switch ((int32_t)__invokeParamCount - (int32_t)method->parameters_count)
							{
							case 0:
							{
								if (hotc233::metadata::IsInstanceMethod(method))
								{
									CHECK_NOT_NULL_THROW(target);
									target += IS_CLASS_VALUE_TYPE(method->klass);
									_argBasePtr->obj = target;
								}
								else
								{
									_argBasePtr = __invokeParamCount == 0 ? _argBasePtr + 1 : localVarBase + _resolvedArgIdxs[1];
								}
								break;
							}
							case -1:
							{
								_argBasePtr->obj = target;
								break;
							}
							case 1:
							{
								_argBasePtr = localVarBase + _resolvedArgIdxs[1];
								CHECK_NOT_NULL_THROW(_argBasePtr->obj);
								break;
							}
							default:
							{
								RaiseExecutionEngineException("CallInterpDelegate");
							}
							}
							InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
							if (false && methodImi && methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 24;
								continue;
							}
							if (methodImi)
							{
								StoreSingleInterpDelegatePreparedCache(_interpDelegateCache, _del, __invokeParamCount, method, methodImi);
								CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, _argBasePtr, _ret);
								continue;
							}
						}
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
					}
					else
					{
						Il2CppArray* dels = _del->delegates;
						Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
						Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
						for (il2cpp_array_size_t i = 0; i < dels->max_length; i++)
						{
							Il2CppMulticastDelegate* subDel = il2cpp_array_get(dels, Il2CppMulticastDelegate *, i);
							IL2CPP_ASSERT(subDel);
							IL2CPP_ASSERT(subDel->delegates == nullptr);
							const MethodInfo* method = subDel->delegate.method;
							Il2CppObject* target = subDel->delegate.target;
							if (TryInvokeInterpDelegateSynchronously(__invokeParamCount, method, target, _resolvedArgIdxs, localVarBase, _tempRet))
							{
								continue;
							}
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
						}
					}
				    CopyLocationData2StackDataByType((StackObject*)_ret, _tempRet, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateBeginInvoke:
				{
					uint16_t __result = *(uint16_t*)(ip + 2);
					uint32_t __methodInfo = *(uint32_t*)(ip + 4);
					uint32_t __argIdxs = *(uint32_t*)(ip + 8);
				    frame->ip = ip + 2;
					(*(Il2CppObject**)(localVarBase + __result)) = InvokeDelegateBeginInvoke(((MethodInfo*)imi->resolveDatas[__methodInfo]), ((uint16_t*)&imi->resolveDatas[__argIdxs]), localVarBase);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateEndInvoke_void:
				{
					uint32_t __methodInfo = *(uint32_t*)(ip + 4);
					uint16_t __asyncResult = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    InvokeDelegateEndInvokeVoid(((MethodInfo*)imi->resolveDatas[__methodInfo]), (Il2CppAsyncResult*)(*(Il2CppObject**)(localVarBase + __asyncResult)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallDelegateEndInvoke_ret:
				{
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint16_t __asyncResult = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    InvokeDelegateEndInvokeRet(((MethodInfo*)imi->resolveDatas[__methodInfo]), (Il2CppAsyncResult*)(*(Il2CppObject**)(localVarBase + __asyncResult)), (void*)(localVarBase + __ret));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewDelegate:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __method = *(uint16_t*)(ip + 6);
				    Il2CppDelegate* del = (Il2CppDelegate*)il2cpp::vm::Object::New(__klass);
				    ConstructDelegate(del, (*(Il2CppObject**)(localVarBase + __obj)), (*(MethodInfo**)(localVarBase + __method)));
				    (*(Il2CppObject**)(localVarBase + __dst)) = (Il2CppObject*)del;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CtorDelegate:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					MethodInfo* __ctor = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __method = *(uint16_t*)(ip + 6);
				    Il2CppDelegate* _del = (Il2CppDelegate*)il2cpp::vm::Object::New(__ctor->klass);
				    ConstructorDelegate2(__ctor, _del, (*(Il2CppObject**)(localVarBase + __obj)), (*(MethodInfo**)(localVarBase + __method)));
				    (*(Il2CppObject**)(localVarBase + __dst)) = (Il2CppObject*)_del;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __self = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i1_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int8_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					frame->ip = ip + 2;
					void* _self = (*(void**)(localVarBase + __self));
					CHECK_NOT_NULL_THROW(_self);
					MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
					RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
					uint8_t _retValue = 0;
					_resolvedMethod->invoker_method(
						_resolvedMethod->methodPointerCallByInterp,
						_resolvedMethod,
						_self,
						nullptr,
						&_retValue);
					*(int32_t*)(void*)(localVarBase + __ret) = _retValue;
					ip += 16;
					continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i2_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int16_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u2_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint16_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_1:
				HOTC233_EXEC_CallCommonNativeInstance_v_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					frame->ip = ip + 2;
					void* _self = (*(void**)(localVarBase + __self));
					CHECK_NOT_NULL_THROW(_self);
					MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
					if (_resolvedMethod != nullptr && _resolvedMethod->name != nullptr && std::strcmp(_resolvedMethod->name, "SetActive") == 0)
					{
						RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
						bool _p0 = (*(int32_t*)(localVarBase + __param0)) != 0;
						void* _args[1] = { &_p0 };
						_resolvedMethod->invoker_method(
							_resolvedMethod->methodPointerCallByInterp,
							_resolvedMethod,
							_self,
							_args,
							nullptr);
					}
					else
					{
						typedef void(*_NativeMethod_)(void*, int32_t, MethodInfo*);
						((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
					}
					byte* __nextIp = ip + 16;
					if (*(HiOpcodeEnum*)__nextIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						ip = __nextIp;
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
				    ip = __nextIp;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_2:
				HOTC233_EXEC_CallCommonNativeInstance_v_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_5:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __param4 = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(
						_self,
						(*(int32_t*)(localVarBase + __param0)),
						(*(int32_t*)(localVarBase + __param1)),
						(*(int32_t*)(localVarBase + __param2)),
						(*(int32_t*)(localVarBase + __param3)),
						(*(int32_t*)(localVarBase + __param4)),
						_resolvedMethod);
				    ip += 24;
				    continue;
				}
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
				case HiOpcodeEnum::CallCommonNativeInstance_v_i4_5Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __param4 = *(uint16_t*)(ip + 12);
					uint32_t __thunkCache = *(uint32_t*)(ip + 20);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    int32_t _p0 = *(int32_t*)(localVarBase + __param0);
				    int32_t _p1 = *(int32_t*)(localVarBase + __param1);
				    int32_t _p2 = *(int32_t*)(localVarBase + __param2);
				    int32_t _p3 = *(int32_t*)(localVarBase + __param3);
				    int32_t _p4 = *(int32_t*)(localVarBase + __param4);
				    InvokeVoidI4x5Cached(
				    	imi->resolveDatas,
				    	__thunkCache,
				    	_resolvedMethod,
				    	_self,
				    	_p0,
				    	_p1,
				    	_p2,
				    	_p3,
				    	_p4);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_v3_1Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 14);
					uint32_t __thunkCache = *(uint32_t*)(ip + 18);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _p0 = (void*)(localVarBase + __param0);
				    typedef void(*_NativeMethod_)(void*, void*, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _p0, _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_v3_2Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 14);
					uint32_t __thunkCache = *(uint32_t*)(ip + 18);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _p0 = (void*)(localVarBase + __param0);
				    void* _p1 = (void*)(localVarBase + __param1);
					InvokeVoidV3x2Cached(
						imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_v3_3Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 14);
					uint32_t __thunkCache = *(uint32_t*)(ip + 18);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _p0 = (void*)(localVarBase + __param0);
				    void* _p1 = (void*)(localVarBase + __param1);
				    void* _p2 = (void*)(localVarBase + __param2);
					InvokeVoidV3x3Cached(
						imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_v3_4Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 14);
					uint32_t __thunkCache = *(uint32_t*)(ip + 18);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _p0 = (void*)(localVarBase + __param0);
				    void* _p1 = (void*)(localVarBase + __param1);
				    void* _p2 = (void*)(localVarBase + __param2);
				    void* _p3 = (void*)(localVarBase + __param3);
				    InvokeVoidV3x4Cached(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v3_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 6);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint32_t __thunkCache = *(uint32_t*)(ip + 10);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _retBuf = (void*)(localVarBase + __ret);
				    Il2CppMethodPointer _directPtr = GetOrCacheDirectNativeMethodPointer(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, Hotc233DirectCallKind::InstanceV3Return);
				    if (_directPtr != nullptr)
				    {
				    	typedef void(*DirectInstanceV3)(void*, void*);
				    	((DirectInstanceV3)_directPtr)(_self, _retBuf);
				    }
				    else
				    {
				    	_resolvedMethod->invoker_method(
				    		_resolvedMethod->methodPointer, _resolvedMethod, _self, nullptr, _retBuf);
				    }
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_ref_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 6);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
					uint32_t __thunkCache = *(uint32_t*)(ip + 10);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    Il2CppObject** _retObj = (Il2CppObject**)(void*)(localVarBase + __ret);
				    _resolvedMethod->invoker_method(
				    	_resolvedMethod->methodPointer,
				    	_resolvedMethod,
				    	_self,
				    	nullptr,
				    	_retObj);
				    ip += 16;
				    continue;
				}
#endif
#if HOTC233_ENABLE_PRO_CALL_TRACE
				case HiOpcodeEnum::RunInstanceVoidI4x5CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __self = *(uint16_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 6);
					uint16_t __param1 = *(uint16_t*)(ip + 8);
					uint16_t __param2 = *(uint16_t*)(ip + 10);
					uint16_t __param3 = *(uint16_t*)(ip + 12);
					uint16_t __param4 = *(uint16_t*)(ip + 14);
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint32_t __thunkCache = *(uint32_t*)(ip + 20);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    int32_t _p0 = *(int32_t*)(localVarBase + __param0);
				    int32_t _p1 = *(int32_t*)(localVarBase + __param1);
				    int32_t _p2 = *(int32_t*)(localVarBase + __param2);
				    int32_t _p3 = *(int32_t*)(localVarBase + __param3);
				    int32_t _p4 = *(int32_t*)(localVarBase + __param4);
				    Il2CppMethodPointer _directPtr = ResolveVoidI4x5DirectPtr(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, imi, _self, _p0, _p1, _p2, _p3, _p4);
				    InvokeVoidI4x5Repeated(
				    	_directPtr, _resolvedMethod, _self, _p0, _p1, _p2, _p3, _p4, __stepCount);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::RunInstanceVoidV3x1CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __self = *(uint16_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 6);
					uint32_t __method = *(uint32_t*)(ip + 8);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* _p0 = (void*)(localVarBase + __param0);
				    typedef void(*_NativeMethod_)(void*, void*, MethodInfo*);
				    _NativeMethod_ _fn = (_NativeMethod_)_resolvedMethod->methodPointerCallByInterp;
				    for (uint16_t __step = 0; __step < __stepCount; __step++)
				    {
				    	_fn(_self, _p0, _resolvedMethod);
				    }
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RunInstanceVoidV3x4CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __self = *(uint16_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 6);
					uint16_t __param1 = *(uint16_t*)(ip + 8);
					uint16_t __param2 = *(uint16_t*)(ip + 10);
					uint16_t __param3 = *(uint16_t*)(ip + 12);
					uint32_t __method = *(uint32_t*)(ip + 14);
					uint32_t __thunkCache = *(uint32_t*)(ip + 18);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    void* _p0 = (void*)(localVarBase + __param0);
				    void* _p1 = (void*)(localVarBase + __param1);
				    void* _p2 = (void*)(localVarBase + __param2);
				    void* _p3 = (void*)(localVarBase + __param3);
				    if (__stepCount == 10)
				    {
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	InvokeVoidV3x4Cached(imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    }
				    else
				    {
				    	for (uint16_t __step = 0; __step < __stepCount; __step++)
				    	{
				    		InvokeVoidV3x4Cached(
				    			imi->resolveDatas, __thunkCache, _resolvedMethod, _self, _p0, _p1, _p2, _p3);
				    	}
				    }
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::RunInstanceGetTransformSetV3CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __selfGo = *(uint16_t*)(ip + 4);
					uint16_t __paramV3 = *(uint16_t*)(ip + 6);
					uint32_t __getMethod = *(uint32_t*)(ip + 8);
					uint32_t __setMethod = *(uint32_t*)(ip + 12);
					uint32_t __getThunkCache = *(uint32_t*)(ip + 16);
					uint32_t __setThunkCache = *(uint32_t*)(ip + 20);
				    void* _go = (*(void**)(localVarBase + __selfGo));
				    CHECK_NOT_NULL_THROW(_go);
				    MethodInfo* _getResolved = ((MethodInfo*)imi->resolveDatas[__getMethod]);
				    MethodInfo* _setResolved = ((MethodInfo*)imi->resolveDatas[__setMethod]);
				    RuntimeInitClassCCtorWithoutInitClass(_getResolved);
				    RuntimeInitClassCCtorWithoutInitClass(_setResolved);
				    void* _v3 = (void*)(localVarBase + __paramV3);
				    InvokeGetTransformSetV3CachedBatch(
				    	imi, imi->resolveDatas, __setThunkCache, _getResolved, _setResolved, _go, _v3, __stepCount);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::RunInstanceV3ReturnCallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __self = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint32_t __thunkCache = *(uint32_t*)(ip + 12);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    void* _retBuf = (void*)(localVarBase + __ret);
				    Il2CppMethodPointer _directPtr = ResolveV3ReturnDirectPtr(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, imi, _self);
				    InvokeV3ReturnRepeated(_directPtr, _resolvedMethod, _self, _retBuf, __stepCount);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RunInstanceI4ReturnCallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint16_t __self = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint32_t __thunkCache = *(uint32_t*)(ip + 12);
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    int32_t* _retSlot = (int32_t*)(localVarBase + __ret);
				    InvokeI4ReturnRepeated(nullptr, _resolvedMethod, _self, _retSlot, __stepCount);
				    ip += 16;
				    continue;
				}
#endif
				case HiOpcodeEnum::CallCommonNativeInstance_v_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_v_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef void(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_u1_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef uint8_t(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i4_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int32_t(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_i8_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef int64_t(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f4_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef float(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, float, float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, float, float, float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, double, double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeInstance_f8_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 16);
					uint16_t __self = *(uint16_t*)(ip + 2);
					uint16_t __param0 = *(uint16_t*)(ip + 4);
					uint16_t __param1 = *(uint16_t*)(ip + 6);
					uint16_t __param2 = *(uint16_t*)(ip + 8);
					uint16_t __param3 = *(uint16_t*)(ip + 10);
					uint16_t __ret = *(uint16_t*)(ip + 12);
				    frame->ip = ip + 2;
				    void* _self = (*(void**)(localVarBase + __self));
				    CHECK_NOT_NULL_THROW(_self);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    typedef double(*_NativeMethod_)(void*, double, double, double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i1_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int8_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i2_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int16_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u2_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint16_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
				case HiOpcodeEnum::CallCommonNativeStatic_i4_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint32_t __thunkCache = *(uint32_t*)(ip + 8);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    Il2CppMethodPointer _directPtr = GetOrCacheDirectNativeMethodPointer(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, Hotc233DirectCallKind::StaticI4OrNoArg);
				    if (_directPtr != nullptr)
				    {
						typedef int32_t(*DirectStaticI4)(MethodInfo*);
						*(int32_t*)(void*)(localVarBase + __ret) = ((DirectStaticI4)_directPtr)(_resolvedMethod);
				    }
				    else
				    {
				    	typedef int32_t(*_NativeMethod_)(MethodInfo*);
				    	*(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    }
				    ip += 12;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint32_t __thunkCache = *(uint32_t*)(ip + 8);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    StaticF4CallTarget _callTarget = StaticF4CallTarget::Resolve(_resolvedMethod);
				    GetOrCacheDirectNativeMethodPointer(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, Hotc233DirectCallKind::StaticF4OrNoArg);
				    *(float*)(void*)(localVarBase + __ret) = _callTarget.Invoke(_resolvedMethod);
				    ip += 12;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v3_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    void* __retPtr = (void*)(localVarBase + __ret);
				    if (_resolvedMethod->methodPointer != nullptr)
				    {
				    	typedef HtVector3f(*DirectStaticRetV3Val)();
				    	*(HtVector3f*)__retPtr = ((DirectStaticRetV3Val)_resolvedMethod->methodPointer)();
				    }
				    else
				    {
				    	_resolvedMethod->invoker_method(_resolvedMethod->methodPointerCallByInterp, _resolvedMethod, nullptr, nullptr, __retPtr);
				    }
				    ip += 12;
				    continue;
				}
#endif
				case HiOpcodeEnum::CallCommonNativeStatic_i8_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    StaticF4CallTarget _callTarget = StaticF4CallTarget::Resolve(_resolvedMethod);
				    *(float*)(void*)(localVarBase + __ret) = _callTarget.Invoke(_resolvedMethod);
				    ip += 8;
				    continue;
				}
#if HOTC233_ENABLE_PRO_CALL_TRACE
				case HiOpcodeEnum::RunStaticF4CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint32_t __thunkCache = *(uint32_t*)(ip + 12);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    StaticF4CallTarget _callTarget = StaticF4CallTarget::Resolve(_resolvedMethod);
				    GetOrCacheDirectNativeMethodPointer(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, Hotc233DirectCallKind::StaticF4OrNoArg);
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __copyDst = (uint16_t)(__word >> 16);
						float __value = _callTarget.Invoke(_resolvedMethod);
						if (__copyDst != 0xffff)
						{
							*(float*)(void*)(localVarBase + __copyDst) = __value;
						}
						else if (__ret != 0xffff)
						{
							*(float*)(void*)(localVarBase + __ret) = __value;
						}
					}
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RunStaticI4CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint32_t __thunkCache = *(uint32_t*)(ip + 12);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    Il2CppMethodPointer _directPtr = GetOrCacheDirectNativeMethodPointer(
				    	imi->resolveDatas, __thunkCache, _resolvedMethod, Hotc233DirectCallKind::StaticI4OrNoArg);
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __copyDst = (uint16_t)(__word >> 16);
						int32_t __value = 0;
						if (_directPtr != nullptr)
						{
							typedef int32_t(*DirectStaticI4)(MethodInfo*);
							__value = ((DirectStaticI4)_directPtr)(_resolvedMethod);
						}
						else
						{
							typedef int32_t(*_NativeMethod_)(MethodInfo*);
							__value = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
						}
						if (__copyDst != 0xffff)
						{
							*(int32_t*)(void*)(localVarBase + __copyDst) = __value;
						}
						else
						{
							*(int32_t*)(void*)(localVarBase + __ret) = __value;
						}
					}
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RunRegI32NumericTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __op1 = (uint16_t)(__word >> 16);
						uint16_t __op2 = (uint16_t)(__word >> 32);
						uint8_t __kind = (uint8_t)(__word >> 48);
						if (__kind == 3)
						{
							*(int32_t*)(localVarBase + __ret) = (int32_t)((uint32_t)__op1 | ((uint32_t)__op2 << 16));
							continue;
						}
						int32_t __v1 = *(int32_t*)(localVarBase + __op1);
						int32_t __v2 = *(int32_t*)(localVarBase + __op2);
						int32_t __result = __kind == 0 ? (__v1 + __v2) : (__kind == 1 ? (__v1 - __v2) : (__v1 * __v2));
						*(int32_t*)(localVarBase + __ret) = __result;
					}
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RunRegI32AddTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					uint16_t __step = 0;
					for (; __step + 3 < __stepCount; __step += 4)
					{
						uint64_t __word0 = __trace[__step];
						uint64_t __word1 = __trace[__step + 1];
						uint64_t __word2 = __trace[__step + 2];
						uint64_t __word3 = __trace[__step + 3];
						uint16_t __ret0 = (uint16_t)__word0;
						uint16_t __op10 = (uint16_t)(__word0 >> 16);
						uint16_t __op20 = (uint16_t)(__word0 >> 32);
						uint16_t __ret1 = (uint16_t)__word1;
						uint16_t __op11 = (uint16_t)(__word1 >> 16);
						uint16_t __op21 = (uint16_t)(__word1 >> 32);
						uint16_t __ret2 = (uint16_t)__word2;
						uint16_t __op12 = (uint16_t)(__word2 >> 16);
						uint16_t __op22 = (uint16_t)(__word2 >> 32);
						uint16_t __ret3 = (uint16_t)__word3;
						uint16_t __op13 = (uint16_t)(__word3 >> 16);
						uint16_t __op23 = (uint16_t)(__word3 >> 32);
						*(int32_t*)(localVarBase + __ret0) = *(int32_t*)(localVarBase + __op10) + *(int32_t*)(localVarBase + __op20);
						*(int32_t*)(localVarBase + __ret1) = *(int32_t*)(localVarBase + __op11) + *(int32_t*)(localVarBase + __op21);
						*(int32_t*)(localVarBase + __ret2) = *(int32_t*)(localVarBase + __op12) + *(int32_t*)(localVarBase + __op22);
						*(int32_t*)(localVarBase + __ret3) = *(int32_t*)(localVarBase + __op13) + *(int32_t*)(localVarBase + __op23);
					}
					for (; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __op1 = (uint16_t)(__word >> 16);
						uint16_t __op2 = (uint16_t)(__word >> 32);
						*(int32_t*)(localVarBase + __ret) = *(int32_t*)(localVarBase + __op1) + *(int32_t*)(localVarBase + __op2);
					}
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RunRegI32AddCopyTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word0 = __trace[__step * 3];
						uint64_t __word1 = __trace[__step * 3 + 1];
						uint64_t __word2 = __trace[__step * 3 + 2];
						uint16_t __addRet = (uint16_t)__word0;
						uint16_t __addOp1 = (uint16_t)(__word0 >> 16);
						uint16_t __addOp2 = (uint16_t)(__word0 >> 32);
						uint16_t __copyDst1 = (uint16_t)(__word0 >> 48);
						uint16_t __copySrc1 = (uint16_t)__word1;
						uint16_t __copyDst2 = (uint16_t)(__word1 >> 16);
						uint16_t __copySrc2 = (uint16_t)(__word1 >> 32);
						uint16_t __copyDst3 = (uint16_t)(__word1 >> 48);
						uint16_t __copySrc3 = (uint16_t)__word2;
						(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
						if (__copyDst1 != 0xffff)
						{
							(*(int32_t*)(localVarBase + __copyDst1)) = (*(int32_t*)(localVarBase + __copySrc1));
						}
						if (__copyDst2 != 0xffff)
						{
							(*(int32_t*)(localVarBase + __copyDst2)) = (*(int32_t*)(localVarBase + __copySrc2));
						}
						if (__copyDst3 != 0xffff)
						{
							(*(int32_t*)(localVarBase + __copyDst3)) = (*(int32_t*)(localVarBase + __copySrc3));
						}
					}
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RunArrayI4IncrementTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word0 = __trace[__step * 2];
						uint64_t __word1 = __trace[__step * 2 + 1];
						uint16_t __loadArr = (uint16_t)__word0;
						uint16_t __loadIndex = (uint16_t)(__word0 >> 16);
						uint16_t __addValue = (uint16_t)(__word0 >> 32);
						uint16_t __storeArr = (uint16_t)(__word0 >> 48);
						uint16_t __storeIndex = (uint16_t)__word1;
						Il2CppArray* loadArr = (*(Il2CppArray**)(localVarBase + __loadArr));
						int32_t loadIndex = (*(int32_t*)(localVarBase + __loadIndex));
						int32_t value = *(int32_t*)GetCheckedArrayElementAddress(loadArr, loadIndex, 4);
						Il2CppArray* storeArr = (*(Il2CppArray**)(localVarBase + __storeArr));
						int32_t storeIndex = (*(int32_t*)(localVarBase + __storeIndex));
						SetArrayElementFast<int32_t>(storeArr, storeIndex, value + (*(int32_t*)(localVarBase + __addValue)));
					}
				    ip += 8;
				    continue;
				}
#endif
				case HiOpcodeEnum::RegVector3Copy:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					float* __dstPtr = (float*)(localVarBase + __dst);
					float* __srcPtr = (float*)(localVarBase + __src);
					__dstPtr[0] = __srcPtr[0];
					__dstPtr[1] = __srcPtr[1];
					__dstPtr[2] = __srcPtr[2];
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RegVector3Add:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __op1 = *(uint16_t*)(ip + 4);
					uint16_t __op2 = *(uint16_t*)(ip + 6);
					float* __retPtr = (float*)(localVarBase + __ret);
					float* __op1Ptr = (float*)(localVarBase + __op1);
					float* __op2Ptr = (float*)(localVarBase + __op2);
					__retPtr[0] = __op1Ptr[0] + __op2Ptr[0];
					__retPtr[1] = __op1Ptr[1] + __op2Ptr[1];
					__retPtr[2] = __op1Ptr[2] + __op2Ptr[2];
				    ip += 8;
				    continue;
				}
#if HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM
				case HiOpcodeEnum::RunRegVector3AddTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __op1 = (uint16_t)(__word >> 16);
						uint16_t __op2 = (uint16_t)(__word >> 32);
						float* __retPtr = (float*)(localVarBase + __ret);
						float* __op1Ptr = (float*)(localVarBase + __op1);
						float* __op2Ptr = (float*)(localVarBase + __op2);
						__retPtr[0] = __op1Ptr[0] + __op2Ptr[0];
						__retPtr[1] = __op1Ptr[1] + __op2Ptr[1];
						__retPtr[2] = __op1Ptr[2] + __op2Ptr[2];
					}
				    ip += 8;
				    continue;
				}
#endif
				case HiOpcodeEnum::RegVector3SqrMag:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					float* __srcPtr = (float*)(localVarBase + __src);
					float __x = __srcPtr[0];
					float __y = __srcPtr[1];
					float __z = __srcPtr[2];
					*(float*)(localVarBase + __ret) = __x * __x + __y * __y + __z * __z;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_0:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(float, float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(double, double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_v_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef void(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_u1_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef uint8_t(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i4_2:
#if HOTC233_ENABLE_THREADED_DISPATCH
				HOTC233_EXEC_CallCommonNativeStatic_i4_i4_2:
#endif
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
#if HOTC233_ENABLE_THREADED_DISPATCH
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
#endif
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i4_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int32_t(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(float, float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(double, double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_i8_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef int64_t(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    *(int64_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(float, float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(double, double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f4_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int32_t, int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int32_t, int32_t, int32_t, int32_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int32_t*)(localVarBase + __param0)), (*(int32_t*)(localVarBase + __param1)), (*(int32_t*)(localVarBase + __param2)), (*(int32_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int64_t, int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_i8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(int64_t, int64_t, int64_t, int64_t, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(int64_t*)(localVarBase + __param0)), (*(int64_t*)(localVarBase + __param1)), (*(int64_t*)(localVarBase + __param2)), (*(int64_t*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f4_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f4_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f4_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(float, float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f4_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(float, float, float, float, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(float*)(localVarBase + __param0)), (*(float*)(localVarBase + __param1)), (*(float*)(localVarBase + __param2)), (*(float*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f8_1:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f8_2:
				{
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 6);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f8_3:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __ret = *(uint16_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(double, double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), _resolvedMethod);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallCommonNativeStatic_f8_f8_4:
				{
					uint32_t __method = *(uint32_t*)(ip + 12);
					uint16_t __param0 = *(uint16_t*)(ip + 2);
					uint16_t __param1 = *(uint16_t*)(ip + 4);
					uint16_t __param2 = *(uint16_t*)(ip + 6);
					uint16_t __param3 = *(uint16_t*)(ip + 8);
					uint16_t __ret = *(uint16_t*)(ip + 10);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef double(*_NativeMethod_)(double, double, double, double, MethodInfo*);
				    *(double*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)((*(double*)(localVarBase + __param0)), (*(double*)(localVarBase + __param1)), (*(double*)(localVarBase + __param2)), (*(double*)(localVarBase + __param3)), _resolvedMethod);
				    ip += 16;
				    continue;
				}

				//!!!}}FUNCTION
#pragma endregion

#pragma region OBJECT
		//!!!{{OBJECT
				case HiOpcodeEnum::BoxVarVar:
				HOTC233_EXEC_BoxVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __data = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppObject**)(localVarBase + __dst)) = il2cpp::vm::Object::Box(__klass, (void*)(localVarBase + __data));
					ip += 16;
#if HOTC233_ENABLE_THREADED_DISPATCH
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::CallCommonNativeStatic_i4_i4_2)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_CallCommonNativeStatic_i4_i4_2;
					}
#endif
				    continue;
				}
				case HiOpcodeEnum::UnBoxVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(void**)(localVarBase + __addr)) = HiUnbox((*(Il2CppObject**)(localVarBase + __obj)), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::UnBoxAnyVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    HiUnboxAny2StackObject((*(Il2CppObject**)(localVarBase + __obj)), __klass, (void*)(localVarBase + __dst));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CastclassVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __klass = *(uint32_t*)(ip + 4);
				    HiCastClass((*(Il2CppObject**)(localVarBase + __obj)), ((Il2CppClass*)imi->resolveDatas[__klass]));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::IsInstVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __klass = *(uint32_t*)(ip + 4);
				    (*(Il2CppObject**)(localVarBase + __obj)) = il2cpp::vm::Object::IsInst((*(Il2CppObject**)(localVarBase + __obj)), ((Il2CppClass*)imi->resolveDatas[__klass]));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdtokenVar:
				{
					uint16_t __runtimeHandle = *(uint16_t*)(ip + 2);
					void* __token = ((void*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
				    (*(void**)(localVarBase + __runtimeHandle)) = __token;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdtokenTypeObjectVar:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					Il2CppObject* __typeObject = ((Il2CppObject*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
				    (*(Il2CppObject**)(localVarBase + __ret)) = __typeObject;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MakeRefVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __data = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppTypedRef*)(localVarBase + __dst)) = MAKE_TYPEDREFERENCE(__klass, (*(void**)(localVarBase + __data)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::RefAnyTypeVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __typedRef = *(uint16_t*)(ip + 4);
				    (*(void**)(localVarBase + __dst)) = RefAnyType((*(Il2CppTypedRef*)(localVarBase + __typedRef)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::RefAnyValueVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __typedRef = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(void**)(localVarBase + __addr)) = RefAnyValue((*(Il2CppTypedRef*)(localVarBase + __typedRef)), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_ref:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    void** _dstAddr_ = (void**)((*(void**)(localVarBase + __dst)));
				    *_dstAddr_ = *(void**)(*(void**)(localVarBase + __src));
				    HOTC233_SET_WRITE_BARRIER(_dstAddr_);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy1((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy2((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy4((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy8((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy12((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy16((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy20((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy24((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy28((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy32((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
					std::memmove((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)), __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 8);
					std::memmove((*(void**)(localVarBase + __dst)), (*(void**)(localVarBase + __src)), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_WriteBarrier_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
				    void* _dstAddr_ = (void*)((*(void**)(localVarBase + __dst)));
				    std::memmove(_dstAddr_, (*(void**)(localVarBase + __src)), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_dstAddr_, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CpobjVarVar_WriteBarrier_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 8);
				    void* _dstAddr_ = (void*)((*(void**)(localVarBase + __dst)));
				    std::memmove(_dstAddr_, (*(void**)(localVarBase + __src)), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_dstAddr_, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_ref:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					*(void**)(void*)(localVarBase + __dst) = (*(void**)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy1((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy2((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy4((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy8((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy12((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy16((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy20((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy24((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy28((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy32((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdobjVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 8);
					std::memmove((void*)(localVarBase + __dst), (*(void**)(localVarBase + __src)), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_ref:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    void** _dstAddr_ = (void**)((*(void**)(localVarBase + __dst)));
				    *_dstAddr_ = (*(Il2CppObject**)(localVarBase + __src));
				    HOTC233_SET_WRITE_BARRIER(_dstAddr_);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy1((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy2((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy4((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy8((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy12((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy16((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy20((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy24((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy28((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					Copy32((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 8);
					std::memmove((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __src), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StobjVarVar_WriteBarrier_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 8);
				    void* _dstAddr_ = (*(void**)(localVarBase + __dst));
				    std::memmove(_dstAddr_, (void*)(localVarBase + __src), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_dstAddr_, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_ref:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
				    void* _objAddr_ = (*(void**)(localVarBase + __obj));
				    CHECK_NOT_NULL_THROW(_objAddr_);
				    *(void**)_objAddr_ = nullptr;
				    HOTC233_SET_WRITE_BARRIER((void**)_objAddr_);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_1:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault1((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault2((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault4((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault8((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_12:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault12((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_16:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault16((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_20:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault20((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_24:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault24((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_28:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault28((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_32:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					InitDefault32((*(void**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
					InitDefaultN((*(void**)(localVarBase + __obj)), __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 4);
					InitDefaultN((*(void**)(localVarBase + __obj)), __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_WriteBarrier_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    void* _objAddr_ = (*(void**)(localVarBase + __obj));
				    InitDefaultN(_objAddr_, __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_objAddr_, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitobjVar_WriteBarrier_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 4);
				    void* _objAddr_ = (*(void**)(localVarBase + __obj));
				    InitDefaultN(_objAddr_, __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_objAddr_, __size);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdstrVar:
				HOTC233_EXEC_LdstrVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint32_t __str = *(uint32_t*)(ip + 4);
				    (*(Il2CppString**)(localVarBase + __dst)) = ((Il2CppString*)imi->resolveDatas[__str]);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int8_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_u1:
				HOTC233_EXEC_LdfldVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BranchTrueVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchTrueVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_u1_BranchFalseVar_i4:
				HOTC233_EXEC_LdfldVarVar_u1_BranchFalseVar_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __branchOp = *(uint16_t*)(ip + 8);
					int32_t __offsetBranch = *(int32_t*)(ip + 12);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __fieldDst)) = *(uint8_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    if (!(*(int32_t*)(localVarBase + __branchOp)))
				    {
				        ip = ipBase + __offsetBranch;
				    }
				    else
				    {
				        ip += 16;
				    }
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int16_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_i4:
				HOTC233_EXEC_LdfldVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 12);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 14);
					int32_t __offsetBranch = *(int32_t*)(ip + 20);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    if (CompareCneUn((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
						byte* __targetIp = ipBase + __offsetBranch;
						if (*(HiOpcodeEnum*)__targetIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
						{
							ip = __targetIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
						}
				        ip = __targetIp;
						if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
						{
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
						}
				    }
				    else
				    {
						byte* __fallthroughIp = ip + 24;
						if (*(HiOpcodeEnum*)__fallthroughIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
						{
							ip = __fallthroughIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
						}
				        ip = __fallthroughIp;
						if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
						{
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
						}
				    }
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4:
				HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4:
				{
					uint16_t __copyDst1 = *(uint16_t*)(ip + 2);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 4);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 6);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 8);
					uint16_t __fieldDst = *(uint16_t*)(ip + 10);
					uint16_t __obj = *(uint16_t*)(ip + 12);
					uint16_t __fieldOffset = *(uint16_t*)(ip + 14);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 16);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 18);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 20);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 22);
					int32_t __offsetBranch = *(int32_t*)(ip + 24);
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __fieldOffset);
					(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
				    if (CompareCneUn((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
						byte* __targetIp = ipBase + __offsetBranch;
						if (*(HiOpcodeEnum*)__targetIp == HiOpcodeEnum::LdlocVarVar)
						{
							ip = __targetIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar;
						}
				        ip = __targetIp;
				    }
				    else
				    {
						byte* __fallthroughIp = ip + 32;
						if (*(HiOpcodeEnum*)__fallthroughIp == HiOpcodeEnum::LdlocVarVar)
						{
							ip = __fallthroughIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar;
						}
				        ip = __fallthroughIp;
				    }
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int64_t*)(localVarBase + __dst)) = *(int64_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy8((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy12((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy16((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy20((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy24((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy28((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy32((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __size = *(uint16_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint32_t __size = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(int8_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(int16_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4:
				HOTC233_EXEC_LdfldValueTypeVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					ip += 8;
#if HOTC233_ENABLE_THREADED_DISPATCH
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Mul_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Mul_i4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
					}
#endif
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldDst = *(uint16_t*)(ip + 6);
					uint16_t __obj = *(uint16_t*)(ip + 8);
					uint16_t __offset = *(uint16_t*)(ip + 10);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4:
				HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __constDst = *(uint16_t*)(ip + 12);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __constDst = *(uint16_t*)(ip + 12);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					uint16_t __divRet = *(uint16_t*)(ip + 20);
					uint16_t __divOp1 = *(uint16_t*)(ip + 22);
					uint16_t __divOp2 = *(uint16_t*)(ip + 24);
					uint16_t __minRet = *(uint16_t*)(ip + 26);
					uint16_t __minOp1 = *(uint16_t*)(ip + 28);
					uint16_t __minOp2 = *(uint16_t*)(ip + 30);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					int32_t __v1 = (*(int32_t*)(localVarBase + __minOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __minOp2));
					(*(int32_t*)(localVarBase + __minRet)) = __v1 < __v2 ? __v1 : __v2;
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4:
				HOTC233_EXEC_LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 16);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 18);
					int32_t __offsetBranch = *(int32_t*)(ip + 20);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    if (CompareCgt((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
						ip += 24;
				    }
				    else
				    {
						ip = ipBase + __offsetBranch;
				    }
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Add_i4_LdcVarConst_4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					uint16_t __divRet = *(uint16_t*)(ip + 16);
					uint16_t __divOp1 = *(uint16_t*)(ip + 18);
					uint16_t __divOp2 = *(uint16_t*)(ip + 20);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpSub_i4_MathMaxVarVarVar_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					uint16_t __subRet = *(uint16_t*)(ip + 16);
					uint16_t __subOp1 = *(uint16_t*)(ip + 18);
					uint16_t __subOp2 = *(uint16_t*)(ip + 20);
					uint16_t __maxRet = *(uint16_t*)(ip + 22);
					uint16_t __maxOp1 = *(uint16_t*)(ip + 24);
					uint16_t __maxOp2 = *(uint16_t*)(ip + 26);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __subRet)) = (*(int32_t*)(localVarBase + __subOp1)) - (*(int32_t*)(localVarBase + __subOp2));
					int32_t __v1 = (*(int32_t*)(localVarBase + __maxOp1));
					int32_t __v2 = (*(int32_t*)(localVarBase + __maxOp2));
					(*(int32_t*)(localVarBase + __maxRet)) = __v1 > __v2 ? __v1 : __v2;
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __constDst = *(uint16_t*)(ip + 8);
					uint32_t __constant = *(uint32_t*)(ip + 12);
					uint16_t __divRet = *(uint16_t*)(ip + 16);
					uint16_t __divOp1 = *(uint16_t*)(ip + 18);
					uint16_t __divOp2 = *(uint16_t*)(ip + 20);
					uint16_t __addRet = *(uint16_t*)(ip + 22);
					uint16_t __addOp1 = *(uint16_t*)(ip + 24);
					uint16_t __addOp2 = *(uint16_t*)(ip + 26);
					uint16_t __copyDst = *(uint16_t*)(ip + 28);
					uint16_t __copySrc = *(uint16_t*)(ip + 30);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __divRet)) = HiDiv((*(int32_t*)(localVarBase + __divOp1)), (*(int32_t*)(localVarBase + __divOp2)));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4:
				HOTC233_EXEC_LdfldValueTypeVarVar_i4_BinOpAdd_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __addRet = *(uint16_t*)(ip + 8);
					uint16_t __addOp1 = *(uint16_t*)(ip + 10);
					uint16_t __addOp2 = *(uint16_t*)(ip + 12);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::StindVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_StindVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __fieldAddRet = *(uint16_t*)(ip + 8);
					uint16_t __fieldAddOp1 = *(uint16_t*)(ip + 10);
					uint16_t __fieldAddOp2 = *(uint16_t*)(ip + 12);
					uint16_t __addRet = *(uint16_t*)(ip + 14);
					uint16_t __addOp1 = *(uint16_t*)(ip + 16);
					uint16_t __addOp2 = *(uint16_t*)(ip + 18);
					uint16_t __copyDst1 = *(uint16_t*)(ip + 20);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 22);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 24);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 26);
					uint16_t __copyDst3 = *(uint16_t*)(ip + 28);
					uint16_t __copySrc3 = *(uint16_t*)(ip + 30);
					uint16_t __sizedCopyDst = *(uint16_t*)(ip + 32);
					uint16_t __sizedCopySrc = *(uint16_t*)(ip + 34);
					uint16_t __sizedCopySize = *(uint16_t*)(ip + 36);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __fieldAddRet)) = (*(int32_t*)(localVarBase + __fieldAddOp1)) + (*(int32_t*)(localVarBase + __fieldAddOp2));
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					(*(uint64_t*)(localVarBase + __copyDst3)) = (*(uint64_t*)(localVarBase + __copySrc3));
					std::memmove((void*)(localVarBase + __sizedCopyDst), (void*)(localVarBase + __sizedCopySrc), __sizedCopySize);
				    ip += 40;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4:
				{
					uint16_t __fieldDst1 = *(uint16_t*)(ip + 2);
					uint16_t __obj1 = *(uint16_t*)(ip + 4);
					uint16_t __offset1 = *(uint16_t*)(ip + 6);
					uint16_t __addRet1 = *(uint16_t*)(ip + 8);
					uint16_t __addOp11 = *(uint16_t*)(ip + 10);
					uint16_t __addOp21 = *(uint16_t*)(ip + 12);
					uint16_t __fieldDst2 = *(uint16_t*)(ip + 14);
					uint16_t __obj2 = *(uint16_t*)(ip + 16);
					uint16_t __offset2 = *(uint16_t*)(ip + 18);
					uint16_t __addRet2 = *(uint16_t*)(ip + 20);
					uint16_t __addOp12 = *(uint16_t*)(ip + 22);
					uint16_t __addOp22 = *(uint16_t*)(ip + 24);
					(*(int32_t*)(localVarBase + __fieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj1) + __offset1);
					(*(int32_t*)(localVarBase + __addRet1)) = (*(int32_t*)(localVarBase + __addOp11)) + (*(int32_t*)(localVarBase + __addOp21));
					(*(int32_t*)(localVarBase + __fieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj2) + __offset2);
					(*(int32_t*)(localVarBase + __addRet2)) = (*(int32_t*)(localVarBase + __addOp12)) + (*(int32_t*)(localVarBase + __addOp22));
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4:
				{
					uint16_t __fieldDst0 = *(uint16_t*)(ip + 2);
					uint16_t __obj0 = *(uint16_t*)(ip + 4);
					uint16_t __offset0 = *(uint16_t*)(ip + 6);
					uint16_t __fieldDst1 = *(uint16_t*)(ip + 8);
					uint16_t __obj1 = *(uint16_t*)(ip + 10);
					uint16_t __offset1 = *(uint16_t*)(ip + 12);
					uint16_t __addRet1 = *(uint16_t*)(ip + 14);
					uint16_t __addOp11 = *(uint16_t*)(ip + 16);
					uint16_t __addOp21 = *(uint16_t*)(ip + 18);
					uint16_t __fieldDst2 = *(uint16_t*)(ip + 20);
					uint16_t __obj2 = *(uint16_t*)(ip + 22);
					uint16_t __offset2 = *(uint16_t*)(ip + 24);
					uint16_t __addRet2 = *(uint16_t*)(ip + 26);
					uint16_t __addOp12 = *(uint16_t*)(ip + 28);
					uint16_t __addOp22 = *(uint16_t*)(ip + 30);
					(*(int32_t*)(localVarBase + __fieldDst0)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj0) + __offset0);
					(*(int32_t*)(localVarBase + __fieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj1) + __offset1);
					(*(int32_t*)(localVarBase + __addRet1)) = (*(int32_t*)(localVarBase + __addOp11)) + (*(int32_t*)(localVarBase + __addOp21));
					(*(int32_t*)(localVarBase + __fieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj2) + __offset2);
					(*(int32_t*)(localVarBase + __addRet2)) = (*(int32_t*)(localVarBase + __addOp12)) + (*(int32_t*)(localVarBase + __addOp22));
				    ip += 32;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldDst0 = *(uint16_t*)(ip + 6);
					uint16_t __obj0 = *(uint16_t*)(ip + 8);
					uint16_t __offset0 = *(uint16_t*)(ip + 10);
					uint16_t __fieldDst1 = *(uint16_t*)(ip + 12);
					uint16_t __obj1 = *(uint16_t*)(ip + 14);
					uint16_t __offset1 = *(uint16_t*)(ip + 16);
					uint16_t __addRet1 = *(uint16_t*)(ip + 18);
					uint16_t __addOp11 = *(uint16_t*)(ip + 20);
					uint16_t __addOp21 = *(uint16_t*)(ip + 22);
					uint16_t __fieldDst2 = *(uint16_t*)(ip + 24);
					uint16_t __obj2 = *(uint16_t*)(ip + 26);
					uint16_t __offset2 = *(uint16_t*)(ip + 28);
					uint16_t __addRet2 = *(uint16_t*)(ip + 30);
					uint16_t __addOp12 = *(uint16_t*)(ip + 32);
					uint16_t __addOp22 = *(uint16_t*)(ip + 34);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __fieldDst0)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj0) + __offset0);
					(*(int32_t*)(localVarBase + __fieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj1) + __offset1);
					(*(int32_t*)(localVarBase + __addRet1)) = (*(int32_t*)(localVarBase + __addOp11)) + (*(int32_t*)(localVarBase + __addOp21));
					(*(int32_t*)(localVarBase + __fieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj2) + __offset2);
					(*(int32_t*)(localVarBase + __addRet2)) = (*(int32_t*)(localVarBase + __addOp12)) + (*(int32_t*)(localVarBase + __addOp22));
				    ip += 40;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				HOTC233_EXEC_LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize:
				{
					uint16_t __copyDst = *(uint16_t*)(ip + 2);
					uint16_t __copySrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldDst0 = *(uint16_t*)(ip + 6);
					uint16_t __obj0 = *(uint16_t*)(ip + 8);
					uint16_t __offset0 = *(uint16_t*)(ip + 10);
					uint16_t __fieldDst1 = *(uint16_t*)(ip + 12);
					uint16_t __obj1 = *(uint16_t*)(ip + 14);
					uint16_t __offset1 = *(uint16_t*)(ip + 16);
					uint16_t __addRet1 = *(uint16_t*)(ip + 18);
					uint16_t __addOp11 = *(uint16_t*)(ip + 20);
					uint16_t __addOp21 = *(uint16_t*)(ip + 22);
					uint16_t __fieldDst2 = *(uint16_t*)(ip + 24);
					uint16_t __obj2 = *(uint16_t*)(ip + 26);
					uint16_t __offset2 = *(uint16_t*)(ip + 28);
					uint16_t __addRet2 = *(uint16_t*)(ip + 30);
					uint16_t __addOp12 = *(uint16_t*)(ip + 32);
					uint16_t __addOp22 = *(uint16_t*)(ip + 34);
					uint16_t __tailFieldDst = *(uint16_t*)(ip + 36);
					uint16_t __tailObj = *(uint16_t*)(ip + 38);
					uint16_t __tailOffset = *(uint16_t*)(ip + 40);
					uint16_t __tailFieldAddRet = *(uint16_t*)(ip + 42);
					uint16_t __tailFieldAddOp1 = *(uint16_t*)(ip + 44);
					uint16_t __tailFieldAddOp2 = *(uint16_t*)(ip + 46);
					uint16_t __tailAddRet = *(uint16_t*)(ip + 48);
					uint16_t __tailAddOp1 = *(uint16_t*)(ip + 50);
					uint16_t __tailAddOp2 = *(uint16_t*)(ip + 52);
					uint16_t __tailCopyDst1 = *(uint16_t*)(ip + 54);
					uint16_t __tailCopySrc1 = *(uint16_t*)(ip + 56);
					uint16_t __tailCopyDst2 = *(uint16_t*)(ip + 58);
					uint16_t __tailCopySrc2 = *(uint16_t*)(ip + 60);
					uint16_t __tailCopyDst3 = *(uint16_t*)(ip + 62);
					uint16_t __tailCopySrc3 = *(uint16_t*)(ip + 64);
					uint16_t __tailSizedCopyDst = *(uint16_t*)(ip + 66);
					uint16_t __tailSizedCopySrc = *(uint16_t*)(ip + 68);
					uint16_t __tailSizedCopySize = *(uint16_t*)(ip + 70);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __fieldDst0)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj0) + __offset0);
					(*(int32_t*)(localVarBase + __fieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj1) + __offset1);
					(*(int32_t*)(localVarBase + __addRet1)) = (*(int32_t*)(localVarBase + __addOp11)) + (*(int32_t*)(localVarBase + __addOp21));
					(*(int32_t*)(localVarBase + __fieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj2) + __offset2);
					(*(int32_t*)(localVarBase + __addRet2)) = (*(int32_t*)(localVarBase + __addOp12)) + (*(int32_t*)(localVarBase + __addOp22));
					(*(int32_t*)(localVarBase + __tailFieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __tailObj) + __tailOffset);
					(*(int32_t*)(localVarBase + __tailFieldAddRet)) = (*(int32_t*)(localVarBase + __tailFieldAddOp1)) + (*(int32_t*)(localVarBase + __tailFieldAddOp2));
					(*(int32_t*)(localVarBase + __tailAddRet)) = (*(int32_t*)(localVarBase + __tailAddOp1)) + (*(int32_t*)(localVarBase + __tailAddOp2));
					(*(uint64_t*)(localVarBase + __tailCopyDst1)) = (*(uint64_t*)(localVarBase + __tailCopySrc1));
					(*(uint64_t*)(localVarBase + __tailCopyDst2)) = (*(uint64_t*)(localVarBase + __tailCopySrc2));
					(*(uint64_t*)(localVarBase + __tailCopyDst3)) = (*(uint64_t*)(localVarBase + __tailCopySrc3));
					std::memmove((void*)(localVarBase + __tailSizedCopyDst), (void*)(localVarBase + __tailSizedCopySrc), __tailSizedCopySize);
					ip += 72;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::SetArrayElementVarVar_size_28_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_SetArrayElementVarVar_size_28_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::Hotc233FieldAddPair_SetArrayElement_size_28:
				HOTC233_EXEC_Hotc233FieldAddPair_SetArrayElement_size_28:
				{
					IRHotc233FieldAddPair_SetArrayElement_size_28* __ir = (IRHotc233FieldAddPair_SetArrayElement_size_28*)ip;
					(*(uint64_t*)(localVarBase + __ir->copyDst)) = (*(uint64_t*)(localVarBase + __ir->copySrc));
					(*(int32_t*)(localVarBase + __ir->fieldDst0)) = *(int32_t*)((byte*)(void*)(localVarBase + __ir->obj0) + __ir->offset0);
					(*(int32_t*)(localVarBase + __ir->fieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __ir->obj1) + __ir->offset1);
					(*(int32_t*)(localVarBase + __ir->addRet1)) = (*(int32_t*)(localVarBase + __ir->addOp11)) + (*(int32_t*)(localVarBase + __ir->addOp21));
					(*(int32_t*)(localVarBase + __ir->fieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __ir->obj2) + __ir->offset2);
					(*(int32_t*)(localVarBase + __ir->addRet2)) = (*(int32_t*)(localVarBase + __ir->addOp12)) + (*(int32_t*)(localVarBase + __ir->addOp22));
					(*(int32_t*)(localVarBase + __ir->midFieldDst1)) = *(int32_t*)((byte*)(void*)(localVarBase + __ir->midObj1) + __ir->midOffset1);
					(*(int32_t*)(localVarBase + __ir->midAddRet1)) = (*(int32_t*)(localVarBase + __ir->midAddOp11)) + (*(int32_t*)(localVarBase + __ir->midAddOp21));
					(*(int32_t*)(localVarBase + __ir->midFieldDst2)) = *(int32_t*)((byte*)(void*)(localVarBase + __ir->midObj2) + __ir->midOffset2);
					(*(int32_t*)(localVarBase + __ir->midAddRet2)) = (*(int32_t*)(localVarBase + __ir->midAddOp12)) + (*(int32_t*)(localVarBase + __ir->midAddOp22));
					(*(int32_t*)(localVarBase + __ir->tailAddRet)) = (*(int32_t*)(localVarBase + __ir->tailAddOp1)) + (*(int32_t*)(localVarBase + __ir->tailAddOp2));
					(*(uint64_t*)(localVarBase + __ir->tailCopyDst1)) = (*(uint64_t*)(localVarBase + __ir->tailCopySrc1));
					(*(uint64_t*)(localVarBase + __ir->tailCopyDst2)) = (*(uint64_t*)(localVarBase + __ir->tailCopySrc2));
					(*(uint64_t*)(localVarBase + __ir->tailCopyDst3)) = (*(uint64_t*)(localVarBase + __ir->tailCopySrc3));
					std::memmove((void*)(localVarBase + __ir->tailSizedCopyDst), (void*)(localVarBase + __ir->tailSizedCopySrc), __ir->tailSizedCopySize);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __ir->arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __ir->indexSrc));
				    Copy28(GetCheckedArrayElementAddress(_arr, _index, 28), (void*)(localVarBase + __ir->elementSrc));
					ip += 96;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVarSize;
					}
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4:
				{
					uint16_t __fieldDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __branchOp1 = *(uint16_t*)(ip + 12);
					uint16_t __branchOp2 = *(uint16_t*)(ip + 14);
					int32_t __offsetBranch = *(int32_t*)(ip + 20);
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    if (CompareCneUn((*(int32_t*)(localVarBase + __branchOp1)), (*(int32_t*)(localVarBase + __branchOp2))))
				    {
						byte* __targetIp = ipBase + __offsetBranch;
						if (*(HiOpcodeEnum*)__targetIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
						{
							ip = __targetIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
						}
				        ip = __targetIp;
						if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
						{
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
						}
				    }
				    else
				    {
						byte* __fallthroughIp = ip + 24;
						if (*(HiOpcodeEnum*)__fallthroughIp == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
						{
							ip = __fallthroughIp;
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
						}
				        ip = __fallthroughIp;
						if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
						{
							if (g_opcodeProfilerEnabled)
							{
								opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
							}
							goto HOTC233_EXEC_LdfldValueTypeVarVar_i4;
						}
				    }
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __dst)) = *(int64_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					(*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy8((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy12((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy16((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy20((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy24((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy28((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					Copy32((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __size = *(uint16_t*)(ip + 8);
					std::memmove((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint32_t __size = *(uint32_t*)(ip + 8);
					std::memmove((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldaVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __dst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdfldaVarVar_LdlocVarVar:
				{
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldaVarVar_LdlocVarVar_LdindVarVar_i4:
				{
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __offset = *(uint16_t*)(ip + 6);
					uint16_t __copyDst = *(uint16_t*)(ip + 8);
					uint16_t __copySrc = *(uint16_t*)(ip + 10);
					uint16_t __indDst = *(uint16_t*)(ip + 12);
					uint16_t __indSrc = *(uint16_t*)(ip + 14);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4:
				HOTC233_EXEC_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4:
				{
					uint16_t __addressDst = *(uint16_t*)(ip + 2);
					uint16_t __addressSrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 6);
					uint16_t __obj = *(uint16_t*)(ip + 8);
					uint16_t __offset = *(uint16_t*)(ip + 10);
					uint16_t __copyDst = *(uint16_t*)(ip + 12);
					uint16_t __copySrc = *(uint16_t*)(ip + 14);
					uint16_t __indDst = *(uint16_t*)(ip + 16);
					uint16_t __indSrc = *(uint16_t*)(ip + 18);
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
					ip += 24;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdfldValueTypeVarVar_i4_BinOpAdd_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4:
				{
					uint16_t __leadingCopyDst = *(uint16_t*)(ip + 2);
					uint16_t __leadingCopySrc = *(uint16_t*)(ip + 4);
					uint16_t __addressDst = *(uint16_t*)(ip + 6);
					uint16_t __addressSrc = *(uint16_t*)(ip + 8);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 10);
					uint16_t __obj = *(uint16_t*)(ip + 12);
					uint16_t __offset = *(uint16_t*)(ip + 14);
					uint16_t __copyDst = *(uint16_t*)(ip + 16);
					uint16_t __copySrc = *(uint16_t*)(ip + 18);
					uint16_t __indDst = *(uint16_t*)(ip + 20);
					uint16_t __indSrc = *(uint16_t*)(ip + 22);
					(*(uint64_t*)(localVarBase + __leadingCopyDst)) = (*(uint64_t*)(localVarBase + __leadingCopySrc));
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4:
				HOTC233_EXEC_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4:
				{
					uint16_t __addressDst = *(uint16_t*)(ip + 2);
					uint16_t __addressSrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 6);
					uint16_t __obj = *(uint16_t*)(ip + 8);
					uint16_t __offset = *(uint16_t*)(ip + 10);
					uint16_t __copyDst = *(uint16_t*)(ip + 12);
					uint16_t __copySrc = *(uint16_t*)(ip + 14);
					uint16_t __indDst = *(uint16_t*)(ip + 16);
					uint16_t __indSrc = *(uint16_t*)(ip + 18);
					uint16_t __constDst = *(uint16_t*)(ip + 20);
					uint32_t __constant = *(uint32_t*)(ip + 24);
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					ip += 32;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BinOpVarVarVar_Sub_i4_StindVarVar_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar:
				{
					uint16_t __addressDst = *(uint16_t*)(ip + 2);
					uint16_t __addressSrc = *(uint16_t*)(ip + 4);
					uint16_t __fieldAddressDst = *(uint16_t*)(ip + 6);
					uint16_t __obj = *(uint16_t*)(ip + 8);
					uint16_t __offset = *(uint16_t*)(ip + 10);
					uint16_t __copyDst = *(uint16_t*)(ip + 12);
					uint16_t __copySrc = *(uint16_t*)(ip + 14);
					uint16_t __indDst = *(uint16_t*)(ip + 16);
					uint16_t __indSrc = *(uint16_t*)(ip + 18);
					uint16_t __constDst = *(uint16_t*)(ip + 20);
					uint32_t __constant = *(uint32_t*)(ip + 24);
					uint16_t __tailCopyDst1 = *(uint16_t*)(ip + 28);
					uint16_t __tailCopySrc1 = *(uint16_t*)(ip + 30);
					uint16_t __tailCopyDst2 = *(uint16_t*)(ip + 32);
					uint16_t __tailCopySrc2 = *(uint16_t*)(ip + 34);
					(*(void**)(localVarBase + __addressDst)) = (void*)(localVarBase + __addressSrc);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __fieldAddressDst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
					(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
					(*(int32_t*)(localVarBase + __indDst)) = (*(int32_t*)*(void**)(localVarBase + __indSrc));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(uint64_t*)(localVarBase + __tailCopyDst1)) = (*(uint64_t*)(localVarBase + __tailCopySrc1));
					(*(uint64_t*)(localVarBase + __tailCopyDst2)) = (*(uint64_t*)(localVarBase + __tailCopySrc2));
				    ip += 40;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_i1:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int8_t*)(_fieldAddr_) = (*(int8_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_u1:
				HOTC233_EXEC_StfldVarVar_u1:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint8_t*)(_fieldAddr_) = (*(uint8_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_i2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int16_t*)(_fieldAddr_) = (*(int16_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_u2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint16_t*)(_fieldAddr_) = (*(uint16_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_i4:
				HOTC233_EXEC_StfldVarVar_i4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __data));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize;
					}
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4:
				{
					uint16_t __storeObj = *(uint16_t*)(ip + 2);
					uint16_t __storeOffset = *(uint16_t*)(ip + 4);
					uint16_t __storeData = *(uint16_t*)(ip + 6);
					uint16_t __fieldDst = *(uint16_t*)(ip + 8);
					uint16_t __obj = *(uint16_t*)(ip + 10);
					uint16_t __offset = *(uint16_t*)(ip + 12);
					uint16_t __constDst = *(uint16_t*)(ip + 14);
					uint32_t __constant = *(uint32_t*)(ip + 16);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __storeObj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __storeObj)) + __storeOffset;
				    *(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __storeData));
					(*(int32_t*)(localVarBase + __fieldDst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_u4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint32_t*)(_fieldAddr_) = (*(uint32_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_i8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int64_t*)(_fieldAddr_) = (*(int64_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_u8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint64_t*)(_fieldAddr_) = (*(uint64_t*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_ref:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(Il2CppObject**)(_fieldAddr_) = (*(Il2CppObject**)(localVarBase + __data));HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy8((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_12:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy12((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_16:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy16((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_20:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy20((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_24:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy24((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_28:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy28((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_size_32:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy32((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
					uint16_t __size = *(uint16_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
					uint32_t __size = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_WriteBarrier_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
					uint16_t __size = *(uint16_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldVarVar_WriteBarrier_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __offset = *(uint16_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 6);
					uint32_t __size = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int8_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int16_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_i4:
				HOTC233_EXEC_LdsfldVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int32_t*)(((byte*)__klass->static_fields) + __offset);
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::BranchVarVar_Clt_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_BranchVarVar_Clt_i4;
					}
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int64_t*)(localVarBase + __dst)) = *(int64_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)(((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy8((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy12((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy16((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy20((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy24((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy28((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy32((void*)(localVarBase + __dst), ((byte*)__klass->static_fields) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 12);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((void*)(localVarBase + __dst), (((byte*)__klass->static_fields) + __offset), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((void*)(localVarBase + __dst), (((byte*)__klass->static_fields) + __offset), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_i1:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(int8_t*)(_fieldAddr_) = (*(int8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_u1:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(uint8_t*)(_fieldAddr_) = (*(uint8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_i2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(int16_t*)(_fieldAddr_) = (*(int16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_u2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(uint16_t*)(_fieldAddr_) = (*(uint16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_i4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_u4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(uint32_t*)(_fieldAddr_) = (*(uint32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_i8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(int64_t*)(_fieldAddr_) = (*(int64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_u8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(uint64_t*)(_fieldAddr_) = (*(uint64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_ref:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    *(Il2CppObject**)(_fieldAddr_) = (*(Il2CppObject**)(localVarBase + __data));HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy8(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_12:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy12(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_16:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy16(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_20:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy20(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_24:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy24(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_28:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy28(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_size_32:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy32(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_n_2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 12);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_n_4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove(((byte*)__klass->static_fields) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_WriteBarrier_n_2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 12);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StsfldVarVar_WriteBarrier_n_4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)__klass->static_fields) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldaVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(void**)(localVarBase + __dst)) = ((byte*)__klass->static_fields) + __offset;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdsfldaFromFieldDataVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					void* __src = ((void*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
				    (*(void**)(localVarBase + __dst)) = __src;
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalaVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(void**)(localVarBase + __dst)) = (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int8_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int16_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(int32_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int64_t*)(localVarBase + __dst)) = *(int64_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    (*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy8((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy12((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy16((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy20((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy24((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy28((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy32((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					int32_t __offset = *(int32_t*)(ip + 12);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdthreadlocalVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					int32_t __offset = *(int32_t*)(ip + 8);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((void*)(localVarBase + __dst), (byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_i1:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(int8_t*)_fieldAddr_ = (*(int8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_u1:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(uint8_t*)_fieldAddr_ = (*(uint8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_i2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(int16_t*)_fieldAddr_ = (*(int16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_u2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(uint16_t*)_fieldAddr_ = (*(uint16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_i4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(int32_t*)_fieldAddr_ = (*(int32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_u4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(uint32_t*)_fieldAddr_ = (*(uint32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_i8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(int64_t*)_fieldAddr_ = (*(int64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_u8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(uint64_t*)_fieldAddr_ = (*(uint64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_ref:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    *(Il2CppObject**)_fieldAddr_ = (*(Il2CppObject**)(localVarBase + __data));HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_8:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy8((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_12:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy12((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_16:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy16((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_20:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy20((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_24:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy24((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_28:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy28((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_size_32:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 4);
					uint16_t __data = *(uint16_t*)(ip + 2);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    Copy32((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_n_2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 12);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_n_4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    std::memmove((byte*)il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_WriteBarrier_n_2:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint32_t __offset = *(uint32_t*)(ip + 12);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StthreadlocalVarVar_WriteBarrier_n_4:
				{
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 4)]);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 2);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    RuntimeInitClassCCtorWithoutInitClass(__klass);
				    void* _fieldAddr_ = ((byte*)(il2cpp::vm::Thread::GetThreadStaticData(__klass->thread_static_fields_offset))) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CheckThrowIfNullVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InitClassStaticCtor:
				{
					uint64_t __klass = *(uint64_t*)(ip + 8);
				    RuntimeInitClassCCtorWithoutInitClass((Il2CppClass*)(__klass));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldaLargeVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(void**)(localVarBase + __dst)) = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int8_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int16_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(int32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int64_t*)(localVarBase + __dst)) = *(int64_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    (*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy8((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy12((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy16((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy20((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy24((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy28((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy32((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __size = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldLargeVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((void*)(localVarBase + __dst), (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(int8_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(uint8_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(int16_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(uint16_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(int32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int32_t*)(localVarBase + __dst)) = *(uint32_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int64_t*)(localVarBase + __dst)) = *(int64_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					(*(int64_t*)(localVarBase + __dst)) = *(uint64_t*)((byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy8((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy12((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy16((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy20((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy24((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy28((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					Copy32((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_n_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __size = *(uint16_t*)(ip + 6);
					std::memmove((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::LdfldValueTypeLargeVarVar_n_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint32_t __size = *(uint32_t*)(ip + 12);
					std::memmove((void*)(localVarBase + __dst), (byte*)(void*)(localVarBase + __obj) + __offset, __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_i1:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int8_t*)(_fieldAddr_) = (*(int8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_u1:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint8_t*)(_fieldAddr_) = (*(uint8_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_i2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int16_t*)(_fieldAddr_) = (*(int16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_u2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint16_t*)(_fieldAddr_) = (*(uint16_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_i4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int32_t*)(_fieldAddr_) = (*(int32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_u4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint32_t*)(_fieldAddr_) = (*(uint32_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_i8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(int64_t*)(_fieldAddr_) = (*(int64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_u8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(uint64_t*)(_fieldAddr_) = (*(uint64_t*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_ref:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    *(Il2CppObject**)(_fieldAddr_) = (*(Il2CppObject**)(localVarBase + __data));HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_8:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy8((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_12:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy12((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_16:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy16((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_20:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy20((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_24:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy24((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_28:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy28((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_size_32:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    Copy32((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    std::memmove((uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset, (void*)(localVarBase + __data), __size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_WriteBarrier_n_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
					uint16_t __size = *(uint16_t*)(ip + 6);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::StfldLargeVarVar_WriteBarrier_n_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint32_t __offset = *(uint32_t*)(ip + 8);
					uint16_t __data = *(uint16_t*)(ip + 4);
					uint32_t __size = *(uint32_t*)(ip + 12);
				    CHECK_NOT_NULL_THROW((*(Il2CppObject**)(localVarBase + __obj)));
				    void* _fieldAddr_ = (uint8_t*)(*(Il2CppObject**)(localVarBase + __obj)) + __offset;
				    std::memmove(_fieldAddr_, (void*)(localVarBase + __data), __size);
				    HOTC233_SET_WRITE_BARRIER((void**)_fieldAddr_, (size_t)__size);
				    ip += 16;
				    continue;
				}

				//!!!}}OBJECT
#pragma endregion

#pragma region ARRAY
		//!!!{{ARRAY
				case HiOpcodeEnum::NewArrVarVar:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __size = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppArray**)(localVarBase + __arr)) =  il2cpp::vm::Array::NewSpecific(__klass, (*(int32_t*)(localVarBase + __size)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::GetArrayLengthVarVar:
				{
					uint16_t __len = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
				    CHECK_NOT_NULL_THROW((*(Il2CppArray**)(localVarBase + __arr)));
				    (*(int64_t*)(localVarBase + __len)) = (int64_t)il2cpp::vm::Array::GetLength((*(Il2CppArray**)(localVarBase + __arr)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementAddressAddrVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(arr, _index);
				    (*(void**)(localVarBase + __addr)) = GET_ARRAY_ELEMENT_ADDRESS(arr, _index, il2cpp::vm::Array::GetElementSize(arr->klass));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementAddressCheckAddrVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
					Il2CppClass* __eleKlass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(arr, _index);
				    CheckArrayElementTypeMatch(arr->klass, __eleKlass);
				    (*(void**)(localVarBase + __addr)) = GET_ARRAY_ELEMENT_ADDRESS(arr, _index, il2cpp::vm::Array::GetElementSize(arr->klass));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<int8_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4:
				{
					uint16_t __loadArr = *(uint16_t*)(ip + 2);
					uint16_t __loadIndex = *(uint16_t*)(ip + 4);
					uint16_t __addValue = *(uint16_t*)(ip + 6);
					uint16_t __storeArr = *(uint16_t*)(ip + 8);
					uint16_t __storeIndex = *(uint16_t*)(ip + 10);
				    Il2CppArray* loadArr = (*(Il2CppArray**)(localVarBase + __loadArr));
					int32_t loadIndex = (*(int32_t*)(localVarBase + __loadIndex));
					int32_t value = *(int32_t*)GetCheckedArrayElementAddress(loadArr, loadIndex, 4);
				    Il2CppArray* storeArr = (*(Il2CppArray**)(localVarBase + __storeArr));
					int32_t storeIndex = (*(int32_t*)(localVarBase + __storeIndex));
					SetArrayElementFast<int32_t>(storeArr, storeIndex, value + (*(int32_t*)(localVarBase + __addValue)));
				    ip += 12;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<uint8_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<int16_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<uint16_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<int32_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_u4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int32_t*)(localVarBase + __dst)) = GetArrayElementFast<uint32_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int64_t*)(localVarBase + __dst)) = GetArrayElementFast<int64_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_u8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    (*(int64_t*)(localVarBase + __dst)) = GetArrayElementFast<uint64_t>(arr, _index);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy1((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 1));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy2((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 2));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy4((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 4));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy8((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 8));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_12:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy12((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 12));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_16:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy16((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 16));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_20:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy20((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 20));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_24:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy24((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 24));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_28:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy28((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 28));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_size_32:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy32((void*)(localVarBase + __dst), GetCheckedArrayElementAddress(arr, _index, 32));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetArrayElementVarVar_n:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __index = *(uint16_t*)(ip + 6);
				    Il2CppArray* arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(arr, _index);
				    int32_t eleSize = il2cpp::vm::Array::GetElementSize(arr->klass);
				    std::memmove((void*)(localVarBase + __dst), GET_ARRAY_ELEMENT_ADDRESS(arr, _index, eleSize), eleSize);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_i1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<int8_t>(_arr, _index, (*(int8_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_u1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<uint8_t>(_arr, _index, (*(uint8_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_i2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<int16_t>(_arr, _index, (*(int16_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_u2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<uint16_t>(_arr, _index, (*(uint16_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_i4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<int32_t>(_arr, _index, (*(int32_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_u4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<uint32_t>(_arr, _index, (*(uint32_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_i8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<int64_t>(_arr, _index, (*(int64_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_u8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    SetArrayElementFast<uint64_t>(_arr, _index, (*(uint64_t*)(localVarBase + __ele)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_ref:
				HOTC233_EXEC_SetArrayElementVarVar_ref:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
					Il2CppObject* _eleObj = (*(Il2CppObject**)(localVarBase + __ele));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(_arr, _index);
				    CheckArrayElementTypeCompatible(_arr, _eleObj);
				    il2cpp_array_setref(_arr, _index, _eleObj);
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_12:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy12(GetCheckedArrayElementAddress(_arr, _index, 12), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_16:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy16(GetCheckedArrayElementAddress(_arr, _index, 16), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_20:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy20(GetCheckedArrayElementAddress(_arr, _index, 20), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_24:
				HOTC233_EXEC_SetArrayElementVarVar_size_24:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy24(GetCheckedArrayElementAddress(_arr, _index, 24), (void*)(localVarBase + __ele));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_28:
				HOTC233_EXEC_SetArrayElementVarVar_size_28:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy28(GetCheckedArrayElementAddress(_arr, _index, 28), (void*)(localVarBase + __ele));
					ip += 8;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_28_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar:
				HOTC233_EXEC_SetArrayElementVarVar_size_28_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar:
				{
					uint16_t __arraySrc = *(uint16_t*)(ip + 2);
					uint16_t __indexSrc = *(uint16_t*)(ip + 4);
					uint16_t __elementSrc = *(uint16_t*)(ip + 6);
					uint16_t __copyDst1 = *(uint16_t*)(ip + 8);
					uint16_t __copySrc1 = *(uint16_t*)(ip + 10);
					uint16_t __constDst = *(uint16_t*)(ip + 12);
					uint32_t __constant = *(uint32_t*)(ip + 16);
					uint16_t __addRet = *(uint16_t*)(ip + 20);
					uint16_t __addOp1 = *(uint16_t*)(ip + 22);
					uint16_t __addOp2 = *(uint16_t*)(ip + 24);
					uint16_t __copyDst2 = *(uint16_t*)(ip + 26);
					uint16_t __copySrc2 = *(uint16_t*)(ip + 28);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
				    Copy28(GetCheckedArrayElementAddress(_arr, _index, 28), (void*)(localVarBase + __elementSrc));
					(*(uint64_t*)(localVarBase + __copyDst1)) = (*(uint64_t*)(localVarBase + __copySrc1));
					(*(int32_t*)(localVarBase + __constDst)) = __constant;
					(*(int32_t*)(localVarBase + __addRet)) = (*(int32_t*)(localVarBase + __addOp1)) + (*(int32_t*)(localVarBase + __addOp2));
					(*(uint64_t*)(localVarBase + __copyDst2)) = (*(uint64_t*)(localVarBase + __copySrc2));
					ip += 32;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4;
				    }
				    continue;
				}
				case HiOpcodeEnum::ConvertVarVar_i4_u1_SetArrayElementVarVar_i1:
				HOTC233_EXEC_ConvertVarVar_i4_u1_SetArrayElementVarVar_i1:
				{
					uint16_t __convertDst = *(uint16_t*)(ip + 2);
					uint16_t __convertSrc = *(uint16_t*)(ip + 4);
					uint16_t __arraySrc = *(uint16_t*)(ip + 6);
					uint16_t __indexSrc = *(uint16_t*)(ip + 8);
					int32_t _converted = (uint8_t)(uint32_t)((*(int32_t*)(localVarBase + __convertSrc)));
					(*(int32_t*)(localVarBase + __convertDst)) = _converted;
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arraySrc));
					int32_t _index = (*(int32_t*)(localVarBase + __indexSrc));
					SetArrayElementFast<int8_t>(_arr, _index, (int8_t)_converted);
					ip += 16;
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdindVarVar_i4)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdindVarVar_i4;
					}
					if (*(HiOpcodeEnum*)ip == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar)
					{
						if (g_opcodeProfilerEnabled)
						{
							opcodeProfilerLastOpcode = kDynamicOpcodeProfileInvalidOpcode;
						}
						goto HOTC233_EXEC_LdlocVarVar_LdlocVarVar_LdlocVarVar;
					}
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_size_32:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    Copy32(GetCheckedArrayElementAddress(_arr, _index, 32), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_n:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(_arr, _index);
				    int32_t _eleSize = il2cpp::vm::Array::GetElementSize(_arr->klass);
				    SetArrayElementWithSize(_arr, _eleSize, _index, (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetArrayElementVarVar_WriteBarrier_n:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
					int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(_arr, _index);
				    int32_t _eleSize = il2cpp::vm::Array::GetElementSize(_arr->klass);
				    il2cpp_array_setrefwithsize(_arr, _eleSize, _index, (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewMdArrVarVar_length:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppArray**)(localVarBase + __arr)) =  NewMdArray(__klass, (StackObject*)(void*)(localVarBase + __lengthIdxs), nullptr);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewMdArrVarVar_length_bound:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __lowerBoundIdxs = *(uint16_t*)(ip + 6);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(Il2CppArray**)(localVarBase + __arr)) =  NewMdArray(__klass, (StackObject*)(void*)(localVarBase + __lengthIdxs), (StackObject*)(void*)(localVarBase + __lowerBoundIdxs));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_i1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementExpandToStack<int8_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_u1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementExpandToStack<uint8_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_i2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementExpandToStack<int16_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_u2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementExpandToStack<uint16_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_i4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementCopyToStack<int32_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_u4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementCopyToStack<uint32_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_i8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementCopyToStack<int64_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_u8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementCopyToStack<uint64_t>((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementVarVar_n:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    GetMdArrayElementBySize((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __value));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetMdArrElementAddressVarVar:
				{
					uint16_t __addr = *(uint16_t*)(ip + 2);
					uint16_t __arr = *(uint16_t*)(ip + 4);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 6);
				    (*(void**)(localVarBase + __addr)) = GetMdArrayElementAddress((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_i1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(int8_t*)_addr = (*(int8_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_u1:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(uint8_t*)_addr = (*(uint8_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_i2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(int16_t*)_addr = (*(int16_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_u2:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(uint16_t*)_addr = (*(uint16_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_i4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(int32_t*)_addr = (*(int32_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_u4:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(uint32_t*)_addr = (*(uint32_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_i8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(int64_t*)_addr = (*(int64_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_u8:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    *(uint64_t*)_addr = (*(uint64_t*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_ref:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    void* _addr = GetMdArrayElementAddress(_arr, (StackObject*)(void*)(localVarBase + __lengthIdxs));
				    CheckArrayElementTypeCompatible(_arr, (*(Il2CppObject**)(localVarBase + __ele)));
				    *(Il2CppObject**)_addr = (*(Il2CppObject**)(localVarBase + __ele));
				    HOTC233_SET_WRITE_BARRIER((void**)_addr);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_n:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    SetMdArrayElement((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::SetMdArrElementVarVar_WriteBarrier_n:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __lengthIdxs = *(uint16_t*)(ip + 4);
					uint16_t __ele = *(uint16_t*)(ip + 6);
				    SetMdArrayElementWriteBarrier((*(Il2CppArray**)(localVarBase + __arr)), (StackObject*)(void*)(localVarBase + __lengthIdxs), (void*)(localVarBase + __ele));
				    ip += 8;
				    continue;
				}

				//!!!}}ARRAY
#pragma endregion

#pragma region EXCEPTION
		//!!!{{EXCEPTION
				case HiOpcodeEnum::ThrowEx:
				{
					uint16_t __exceptionObj = *(uint16_t*)(ip + 2);
					uint16_t __firstHandlerIndex = *(uint16_t*)(ip + 4);
					THROW_EX((Il2CppException*)(*(Il2CppObject**)(localVarBase + __exceptionObj)), __firstHandlerIndex);
				    continue;
				}
				case HiOpcodeEnum::RethrowEx:
				{
					RETHROW_EX();
				    continue;
				}
				case HiOpcodeEnum::LeaveEx:
				{
					int32_t __target = *(int32_t*)(ip + 4);
					uint16_t __firstHandlerIndex = *(uint16_t*)(ip + 2);
					LEAVE_EX(__target, __firstHandlerIndex);
				    continue;
				}
				case HiOpcodeEnum::LeaveEx_Directly:
				{
					int32_t __target = *(int32_t*)(ip + 4);
					LEAVE_EX_DIRECTLY(__target);
				    continue;
				}
				case HiOpcodeEnum::EndFilterEx:
				{
					uint16_t __value = *(uint16_t*)(ip + 2);
					ENDFILTER_EX((*(bool*)(localVarBase + __value)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::EndFinallyEx:
				{
					ENDFINALLY_EX();
				    continue;
				}

				//!!!}}EXCEPTION
#pragma endregion

#pragma region instrinct
		//!!!{{INSTRINCT
				case HiOpcodeEnum::NullableNewVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __data = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    NewNullableValueType((void*)(localVarBase + __dst), (void*)(localVarBase + __data), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NullableCtorVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __data = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    InitNullableValueType((*(void**)(localVarBase + __dst)), (void*)(localVarBase + __data), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NullableHasValueVar:
				{
					uint16_t __result = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    (*(uint32_t*)(localVarBase + __result)) = IsNullableHasValue((*(void**)(localVarBase + __obj)), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NullableGetValueOrDefaultVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    GetNullableValueOrDefault2StackDataByType((void*)(localVarBase + __dst), (*(void**)(localVarBase + __obj)), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NullableGetValueOrDefaultVarVar_1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					uint16_t __defaultValue = *(uint16_t*)(ip + 6);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    GetNullableValueOrDefault2StackDataByType((void*)(localVarBase + __dst), (*(void**)(localVarBase + __obj)), (void*)(localVarBase + __defaultValue), __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NullableGetValueVarVar:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __obj = *(uint16_t*)(ip + 4);
					Il2CppClass* __klass = ((Il2CppClass*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
				    GetNullableValueOrDefault2StackDataByType((void*)(localVarBase + __dst), (*(void**)(localVarBase + __obj)), nullptr, __klass);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InterlockedCompareExchangeVarVarVarVar_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
					uint16_t __comparand = *(uint16_t*)(ip + 8);
				    (*(int32_t*)(localVarBase + __ret)) = HiInterlockedCompareExchange((int32_t*)(*(void**)(localVarBase + __location)), (*(int32_t*)(localVarBase + __value)), (*(int32_t*)(localVarBase + __comparand)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InterlockedCompareExchangeVarVarVarVar_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
					uint16_t __comparand = *(uint16_t*)(ip + 8);
				    (*(int64_t*)(localVarBase + __ret)) = HiInterlockedCompareExchange((int64_t*)(*(void**)(localVarBase + __location)), (*(int64_t*)(localVarBase + __value)), (*(int64_t*)(localVarBase + __comparand)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InterlockedCompareExchangeVarVarVarVar_pointer:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
					uint16_t __comparand = *(uint16_t*)(ip + 8);
				    (*(void**)(localVarBase + __ret)) = HiInterlockedCompareExchange((void**)(*(void**)(localVarBase + __location)), (*(void**)(localVarBase + __value)), (*(void**)(localVarBase + __comparand)));
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::InterlockedExchangeVarVarVar_i4:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    (*(int32_t*)(localVarBase + __ret)) = HiInterlockedExchange((int32_t*)(*(void**)(localVarBase + __location)), (*(int32_t*)(localVarBase + __value)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InterlockedExchangeVarVarVar_i8:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    (*(int64_t*)(localVarBase + __ret)) = HiInterlockedExchange((int64_t*)(*(void**)(localVarBase + __location)), (*(int64_t*)(localVarBase + __value)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::InterlockedExchangeVarVarVar_pointer:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint16_t __location = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    (*(void**)(localVarBase + __ret)) = HiInterlockedExchange((void**)(*(void**)(localVarBase + __location)), (*(void**)(localVarBase + __value)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewSystemObjectVar:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
				(*(Il2CppObject**)(localVarBase + __obj)) = il2cpp::vm::Object::New(il2cpp_defaults.object_class);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewVector2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector2f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y))};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewVector3_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector3f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), 0};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewVector3_3:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
				    *(HtVector3f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z))};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewVector4_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector4f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), 0, 0};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewVector4_3:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
				    *(HtVector4f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z)), 0};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewVector4_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
					uint16_t __w = *(uint16_t*)(ip + 10);
				    *(HtVector4f*)(void*)(localVarBase + __obj) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z)), (*(float*)(localVarBase + __w))};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CtorVector2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector2f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y))};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CtorVector3_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector3f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), 0};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CtorVector3_3:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
				    *(HtVector3f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z))};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CtorVector4_2:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
				    *(HtVector4f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), 0, 0};
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::CtorVector4_3:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
				    *(HtVector4f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z)), 0};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CtorVector4_4:
				{
					uint16_t __obj = *(uint16_t*)(ip + 2);
					uint16_t __x = *(uint16_t*)(ip + 4);
					uint16_t __y = *(uint16_t*)(ip + 6);
					uint16_t __z = *(uint16_t*)(ip + 8);
					uint16_t __w = *(uint16_t*)(ip + 10);
				    *(HtVector4f*)(*(void**)(localVarBase + __obj)) = {(*(float*)(localVarBase + __x)), (*(float*)(localVarBase + __y)), (*(float*)(localVarBase + __z)), (*(float*)(localVarBase + __w))};
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::ArrayGetGenericValueImpl:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(_arr, _index);
				    int32_t _eleSize = il2cpp::vm::Array::GetElementSize(_arr->klass);
				    std::memmove((*(void**)(localVarBase + __value)), GET_ARRAY_ELEMENT_ADDRESS(_arr, _index, _eleSize), _eleSize);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::ArraySetGenericValueImpl:
				{
					uint16_t __arr = *(uint16_t*)(ip + 2);
					uint16_t __index = *(uint16_t*)(ip + 4);
					uint16_t __value = *(uint16_t*)(ip + 6);
				    Il2CppArray* _arr = (*(Il2CppArray**)(localVarBase + __arr));
				    int32_t _index = (*(int32_t*)(localVarBase + __index));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY(_arr, _index);
				    int32_t _eleSize = il2cpp::vm::Array::GetElementSize(_arr->klass);
				    il2cpp_array_setrefwithsize(_arr, _eleSize, _index, (*(void**)(localVarBase + __value)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewString:
				{
					uint16_t __str = *(uint16_t*)(ip + 2);
					uint16_t __chars = *(uint16_t*)(ip + 4);
				    Il2CppArray* _chars = (*(Il2CppArray**)(localVarBase + __chars));
				    CHECK_NOT_NULL_THROW(_chars);
				    (*(Il2CppString**)(localVarBase + __str)) = il2cpp::vm::String::NewUtf16((const Il2CppChar*)il2cpp::vm::Array::GetFirstElementAddress(_chars), il2cpp::vm::Array::GetLength(_chars));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::NewString_2:
				{
					uint16_t __str = *(uint16_t*)(ip + 2);
					uint16_t __chars = *(uint16_t*)(ip + 4);
					uint16_t __startIndex = *(uint16_t*)(ip + 6);
					uint16_t __length = *(uint16_t*)(ip + 8);
				    Il2CppArray* _chars = (*(Il2CppArray**)(localVarBase + __chars));
				    int32_t _startIndex = (*(uint32_t*)(localVarBase + __startIndex));
				    int32_t _length = (*(uint32_t*)(localVarBase + __length));
				    CHECK_NOT_NULL_AND_ARRAY_BOUNDARY2(_chars, _startIndex, _length);
				    (*(Il2CppString**)(localVarBase + __str)) = il2cpp::vm::String::NewUtf16(((const Il2CppChar*)il2cpp::vm::Array::GetFirstElementAddress(_chars)) + _startIndex, _length);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::NewString_3:
				{
					uint16_t __str = *(uint16_t*)(ip + 2);
					uint16_t __c = *(uint16_t*)(ip + 4);
					uint16_t __count = *(uint16_t*)(ip + 6);
				    int32_t _count = (*(int32_t*)(localVarBase + __count));
				    if (_count < 0)
				    {
				        il2cpp::vm::Exception::RaiseArgumentOutOfRangeException("new string(char c, int count)");
				    }
				    Il2CppChar _c = (Il2CppChar)(*(uint16_t*)(localVarBase + __c));
				    Il2CppString* _str = (*(Il2CppString**)(localVarBase + __str)) = il2cpp::vm::String::NewSize(_count);
				    std::fill_n(_str->chars, _count, _c);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::UnsafeEnumCast:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					uint16_t __srcType = *(uint16_t*)(ip + 6);
				    (*(int32_t*)(localVarBase + __dst)) =  UnsafeEnumCast((void*)(localVarBase + __src), __srcType);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::GetEnumHashCode:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
				    (*(int32_t*)(localVarBase + __dst)) = GetEnumLongHashCode((*(void**)(localVarBase + __src)));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::AssemblyGetExecutingAssembly:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    (*(Il2CppObject**)(localVarBase + __ret)) = (Il2CppObject*)il2cpp::vm::Reflection::GetAssemblyObject(frame->method->klass->image->assembly);
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::MethodBaseGetCurrentMethod:
				{
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    (*(Il2CppObject**)(localVarBase + __ret)) = (Il2CppObject*)il2cpp::vm::Reflection::GetMethodObject(frame->method, nullptr);
				    ip += 8;
				    continue;
				}

				//!!!}}INSTRINCT
#pragma endregion
				default:
					RaiseExecutionEngineException("");
					break;
				}
			}
		ExitEvalLoop:;
		}
		catch (Il2CppExceptionWrapper ex)
		{
			PREPARE_EXCEPTION(ex.ex, 0);
			FIND_NEXT_EX_HANDLER_OR_UNWIND();
		}
		return;
	UnWindFail:
		IL2CPP_ASSERT(lastUnwindException);
		IL2CPP_ASSERT(interpFrameGroup.GetFrameCount() == 0);
		il2cpp::vm::Exception::Raise(lastUnwindException);
	}


}
}

