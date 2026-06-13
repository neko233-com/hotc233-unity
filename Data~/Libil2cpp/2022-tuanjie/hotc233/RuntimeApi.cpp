#include "RuntimeApi.h"

#include "codegen/il2cpp-codegen.h"
#include "vm/InternalCalls.h"
#include "vm/Array.h"
#include "vm/Exception.h"
#include "vm/Class.h"

#include "metadata/MetadataModule.h"
#include "metadata/MetadataUtil.h"
#include "interpreter/InterpreterModule.h"
#include "RuntimeConfig.h"

namespace hotc233
{
	void RuntimeApi::RegisterInternalCalls()
	{
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::LoadMetadataForAOTAssembly(System.Byte[],Hotc233.HomologousImageMode)", (Il2CppMethodPointer)LoadMetadataForAOTAssembly);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::GetRuntimeOption(Hotc233.RuntimeOptionId)", (Il2CppMethodPointer)GetRuntimeOption);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::SetRuntimeOption(Hotc233.RuntimeOptionId,System.Int32)", (Il2CppMethodPointer)SetRuntimeOption);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::PreJitClass(System.Type)", (Il2CppMethodPointer)PreJitClass);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::PreJitMethod(System.Reflection.MethodInfo)", (Il2CppMethodPointer)PreJitMethod);
	}

	int32_t RuntimeApi::LoadMetadataForAOTAssembly(Il2CppArray* dllBytes, int32_t mode)
	{
		if (!dllBytes)
		{
			il2cpp::vm::Exception::RaiseNullReferenceException();
		}
		return (int32_t)hotc233::metadata::Assembly::LoadMetadataForAOTAssembly(il2cpp::vm::Array::GetFirstElementAddress(dllBytes), il2cpp::vm::Array::GetByteLength(dllBytes), (hotc233::metadata::HomologousImageMode)mode);
	}

	int32_t RuntimeApi::GetRuntimeOption(int32_t optionId)
	{
		return hotc233::RuntimeConfig::GetRuntimeOption((hotc233::RuntimeOptionId)optionId);
	}

	void RuntimeApi::SetRuntimeOption(int32_t optionId, int32_t value)
	{
		hotc233::RuntimeConfig::SetRuntimeOption((hotc233::RuntimeOptionId)optionId, value);
	}

	int32_t PreJitMethod0(const MethodInfo* methodInfo);

	int32_t RuntimeApi::PreJitClass(Il2CppReflectionType* type)
	{
		if (metadata::HasNotInstantiatedGenericType(type->type))
		{
			return false;
		}
		Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type->type, false);
		if (!klass)
		{
			return false;
		}
		metadata::Image* image = metadata::MetadataModule::GetImage(klass->image);
		if (!image)
		{
			image = (metadata::Image*)hotc233::metadata::AOTHomologousImage::FindImageByAssembly(
				klass->rank ? il2cpp_defaults.corlib->assembly : klass->image->assembly);
			if (!image)
			{
				return false;
			}
		}
		for (uint16_t i = 0; i < klass->method_count; i++)
		{
			const MethodInfo* methodInfo = klass->methods[i];
			PreJitMethod0(methodInfo);
		}
		return true;
	}

	int32_t PreJitMethod0(const MethodInfo* methodInfo)
	{
		if (!methodInfo->isInterpterImpl)
		{
			return false;
		}
		if (methodInfo->klass->is_generic)
		{
			return false;
		}
		if (!methodInfo->is_inflated)
		{
			if (methodInfo->is_generic)
			{
				return false;
			}
		}
		else
		{
			const Il2CppGenericMethod* genericMethod = methodInfo->genericMethod;
			if (metadata::HasNotInstantiatedGenericType(genericMethod->context.class_inst) || metadata::HasNotInstantiatedGenericType(genericMethod->context.method_inst))
			{
				return false;
			}
		}

		return interpreter::InterpreterModule::GetInterpMethodInfo(methodInfo) != nullptr;
	}

	int32_t RuntimeApi::PreJitMethod(Il2CppReflectionMethod* method)
	{
		return PreJitMethod0(method->method);
	}
}
