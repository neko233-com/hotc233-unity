#include "TransformContext.h"
#include "Hotc233TransformPolicy.h"

#include <cstring>

#include "metadata/GenericMetadata.h"
#include "vm/Class.h"
#include "vm/Exception.h"
#include "vm/String.h"
#include "vm/Field.h"
#include "vm/PlatformInvoke.h"
#include "vm/Reflection.h"
#include "vm/Image.h"
#include "vm/Type.h"
#include "vm/GenericClass.h"
#include "utils/StringUtils.h"
#include "utils/StringView.h"

#include "../metadata/MethodBodyCache.h"
#include "../interpreter/InterpreterUtil.h"

namespace hotc233
{
namespace transform
{
	constexpr int32_t MAX_STACK_SIZE = (2 << 16) - 1;
	constexpr int32_t MAX_VALUE_TYPE_SIZE = (2 << 16) - 1;

	// Pre-touch an AOT callee's native entry (the function the interpreter calls directly via a
	// typed CallCommonNative opcode) at transform time, before the timed first call. See
	// HOTC233_ENABLE_AOT_CODE_PRETOUCH in Hotc233TransformPolicy.h for the measured rationale.
	// The shared primitive (interpreter::PreTouchCodePtr) does the platform-guarded data read.
	static inline void PreTouchNativeCalleeCode(const MethodInfo* method)
	{
#if HOTC233_ENABLE_AOT_CODE_PRETOUCH
		if (method == nullptr)
		{
			return;
		}
		interpreter::PreTouchCodePtr((const void*)(method->methodPointer != nullptr
			? method->methodPointer
			: method->methodPointerCallByInterp));
#else
		(void)method;
#endif
	}

	template<typename T>
	void AllocResolvedData(il2cpp::utils::dynamic_array<uint64_t>& resolvedDatas, int32_t size, int32_t& index, T*& buf)
	{
		if (size > 0)
		{
			int32_t oldSize = index = (int32_t)resolvedDatas.size();
			resolvedDatas.resize_initialized(oldSize + size);
			buf = (T*)&resolvedDatas[oldSize];
		}
		else
		{
			index = 0;
			buf = nullptr;
		}
	}

	IRCommon* CreateInitLocals(TemporaryMemoryArena& pool, uint32_t size, int32_t offset)
	{
		if (size > 32)
		{
			if (offset == 0)
			{
				CreateIR(ir, InitLocals_n_4);
				ir->size = size;
				return ir;
			}
			else
			{
				CreateIR(ir, InitInlineLocals_n_4);
				ir->size = size;
				ir->offset = offset;
				return ir;
			}
		}
		if (offset == 0)
		{
			CreateIR(ir, InitLocals_size_8);
			if (size <= 8)
			{
			}
			else if (size <= 16)
			{
				ir->type = HiOpcodeEnum::InitLocals_size_16;
			}
			else if (size <= 24)
			{
				ir->type = HiOpcodeEnum::InitLocals_size_24;
			}
			else
			{
				IL2CPP_ASSERT(size <= 32);
				ir->type = HiOpcodeEnum::InitLocals_size_32;
			}
			return ir;
		}
		else
		{

			CreateIR(ir, InitInlineLocals_size_8);
			ir->offset = offset;
			if (size <= 8)
			{

			}
			else if (size <= 16)
			{
				ir->type = HiOpcodeEnum::InitInlineLocals_size_16;
			}
			else if (size <= 24)
			{
				ir->type = HiOpcodeEnum::InitInlineLocals_size_24;
			}
			else
			{
				IL2CPP_ASSERT(size <= 32);
				ir->type = HiOpcodeEnum::InitInlineLocals_size_32;
			}
			return ir;
		}
	}

	IRCommon* CreateLoadExpandDataToStackVarVar(TemporaryMemoryArena& pool, int32_t dstOffset, int32_t srcOffset, const Il2CppType* type, int32_t size);
	IRCommon* CreateAssignVarVar(TemporaryMemoryArena& pool, int32_t dstOffset, int32_t srcOffset, int32_t size);

	interpreter::IRCommon* CreateClassLdfld(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, const FieldInfo* fieldInfo);

	interpreter::IRCommon* CreateValueTypeLdfld(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, const FieldInfo* fieldInfo);
	interpreter::IRCommon* CreateStfld(TemporaryMemoryArena& pool, int32_t objIdx, const FieldInfo* fieldInfo, int32_t dataIdx);

	interpreter::IRCommon* CreateLdsfld(TemporaryMemoryArena& pool, int32_t dstIdx, const FieldInfo* fieldInfo, uint32_t parent);
	interpreter::IRCommon* CreateStsfld(TemporaryMemoryArena& pool, const FieldInfo* fieldInfo, uint32_t parent, int32_t dataIdx);
	interpreter::IRCommon* CreateLdthreadlocal(TemporaryMemoryArena& pool, int32_t dstIdx, const FieldInfo* fieldInfo, uint32_t parent);
	interpreter::IRCommon* CreateStthreadlocal(TemporaryMemoryArena& pool, const FieldInfo* fieldInfo, uint32_t parent, int32_t dataIdx);

	EvalStackReduceDataType GetEvalStackReduceDataType(const Il2CppType* type)
	{
		if (type->byref)
		{
			return NATIVE_INT_REDUCE_TYPE;
		}
		switch (type->type)
		{
		case IL2CPP_TYPE_BOOLEAN:
		case IL2CPP_TYPE_I1:
		case IL2CPP_TYPE_U1:
		case IL2CPP_TYPE_CHAR:
		case IL2CPP_TYPE_I2:
		case IL2CPP_TYPE_U2:
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
			return EvalStackReduceDataType::I4;
		case IL2CPP_TYPE_R4:
			return EvalStackReduceDataType::R4;

		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
			return EvalStackReduceDataType::I8;
		case IL2CPP_TYPE_R8:
			return EvalStackReduceDataType::R8;
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
		case IL2CPP_TYPE_FNPTR:
		case IL2CPP_TYPE_PTR:
		case IL2CPP_TYPE_BYREF:
		case IL2CPP_TYPE_STRING:
		case IL2CPP_TYPE_CLASS:
		case IL2CPP_TYPE_ARRAY:
		case IL2CPP_TYPE_SZARRAY:
		case IL2CPP_TYPE_OBJECT:
			return NATIVE_INT_REDUCE_TYPE;
		case IL2CPP_TYPE_TYPEDBYREF:
			return EvalStackReduceDataType::Other;
		case IL2CPP_TYPE_VALUETYPE:
		{
			Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
			return klass->enumtype ? GetEvalStackReduceDataType(&klass->element_class->byval_arg) : EvalStackReduceDataType::Other;
		}
		case IL2CPP_TYPE_GENERICINST:
		{
			Il2CppGenericClass* genericClass = type->data.generic_class;
			if (genericClass->type->type == IL2CPP_TYPE_CLASS)
			{
				return NATIVE_INT_REDUCE_TYPE;
			}
			else
			{
				Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
				return klass->enumtype ? GetEvalStackReduceDataType(&klass->element_class->byval_arg) : EvalStackReduceDataType::Other;
			}
		}
		default:
		{
			RaiseExecutionEngineException("GetEvalStackReduceDataType invalid type");
			return EvalStackReduceDataType::Other;
		}
		}
	}

	int32_t GetSizeByReduceType(EvalStackReduceDataType type)
	{
		switch (type)
		{
		case hotc233::transform::EvalStackReduceDataType::I4:
		case hotc233::transform::EvalStackReduceDataType::R4:
			return 4;
		case hotc233::transform::EvalStackReduceDataType::I8:
		case hotc233::transform::EvalStackReduceDataType::R8:
			return 8;
		default:
		{
			RaiseExecutionEngineException("GetSizeByReduceType not support type");
			return PTR_SIZE;
		}
		}
	}

	LocationDescInfo ComputValueTypeDescInfo(int32_t size, bool hasReference)
	{
#if HOTC233_ENABLE_WRITE_BARRIERS
		if (hasReference)
		{
			return { LocationDescType::StructContainsRef, size };
		}
#endif
		switch (size)
		{
		case 1: return { LocationDescType::U1, 0 };
		case 2: return { LocationDescType::U2, 0 };
		case 4: return { LocationDescType::I4, 0 };
		case 8: return { LocationDescType::I8, 0 };
		default: return { LocationDescType::S, size };
		}
	}

	LocationDescInfo ComputLocationDescInfo(const Il2CppType* type)
	{
		if (type->byref)
		{
			return { NATIVE_INT_DESC_TYPE, 0 };
		}
		switch (type->type)
		{
		case IL2CPP_TYPE_BOOLEAN:
		case IL2CPP_TYPE_U1:
			return{ LocationDescType::U1, 0 };
		case IL2CPP_TYPE_I1:
			return{ LocationDescType::I1, 0 };
		case IL2CPP_TYPE_I2:
			return{ LocationDescType::I2, 0 };
		case IL2CPP_TYPE_CHAR:
		case IL2CPP_TYPE_U2:
			return{ LocationDescType::U2, 0 };
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
		case IL2CPP_TYPE_R4:
			return{ LocationDescType::I4, 0 };
		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
		case IL2CPP_TYPE_R8:
			return{ LocationDescType::I8, 0 };
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
		case IL2CPP_TYPE_FNPTR:
		case IL2CPP_TYPE_PTR:
		case IL2CPP_TYPE_BYREF:
			return{ NATIVE_INT_DESC_TYPE, 0 };
		case IL2CPP_TYPE_STRING:
		case IL2CPP_TYPE_ARRAY:
		case IL2CPP_TYPE_SZARRAY:
		case IL2CPP_TYPE_OBJECT:
		case IL2CPP_TYPE_CLASS:
			return{ LocationDescType::Ref, 0 };
		case IL2CPP_TYPE_TYPEDBYREF:
			return { LocationDescType::S, sizeof(Il2CppTypedRef) };
		case IL2CPP_TYPE_VALUETYPE:
		{
			Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
			IL2CPP_ASSERT(IS_CLASS_VALUE_TYPE(klass));
			if (klass->enumtype)
			{
				return ComputLocationDescInfo(&klass->castClass->byval_arg);
			}
			return ComputValueTypeDescInfo(il2cpp::vm::Class::GetValueSize(klass, nullptr), klass->has_references);
		}
		case IL2CPP_TYPE_GENERICINST:
		{
			Il2CppGenericClass* genericClass = type->data.generic_class;
			if (genericClass->type->type == IL2CPP_TYPE_CLASS)
			{
				IL2CPP_ASSERT(!IS_CLASS_VALUE_TYPE(il2cpp::vm::Class::FromIl2CppType(type)));
				return{ LocationDescType::Ref, 0 };
			}
			else
			{
				Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
				IL2CPP_ASSERT(IS_CLASS_VALUE_TYPE(klass));
				if (klass->enumtype)
				{
					return ComputLocationDescInfo(&klass->castClass->byval_arg);
				}
				return ComputValueTypeDescInfo(il2cpp::vm::Class::GetValueSize(klass, nullptr), klass->has_references);
			}
		}
		default:
		{
			RaiseExecutionEngineException("not support arg type");
			return{ NATIVE_INT_DESC_TYPE, 0 };
		}
		}
	}

	IRCommon* CreateLoadExpandDataToStackVarVar(TemporaryMemoryArena& pool, int32_t dstOffset, int32_t srcOffset, const Il2CppType* type, int32_t size)
	{
		if (type->byref)
		{
			CreateIR(ir, LdlocVarVar);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		switch (type->type)
		{
		case Il2CppTypeEnum::IL2CPP_TYPE_I1:
		{
			CreateIR(ir, LdlocExpandVarVar_i1);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		case Il2CppTypeEnum::IL2CPP_TYPE_BOOLEAN:
		case Il2CppTypeEnum::IL2CPP_TYPE_U1:
		{
			CreateIR(ir, LdlocExpandVarVar_u1);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		case Il2CppTypeEnum::IL2CPP_TYPE_I2:
		{
			CreateIR(ir, LdlocExpandVarVar_i2);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		case IL2CPP_TYPE_CHAR:
		case IL2CPP_TYPE_U2:
		{
			CreateIR(ir, LdlocExpandVarVar_u2);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		case IL2CPP_TYPE_VALUETYPE:
		{
			Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
			if (klass->enumtype)
			{
				return CreateLoadExpandDataToStackVarVar(pool, dstOffset, srcOffset, &klass->element_class->byval_arg, size);
			}
			break;
		}
		default: break;
		}
		if (size <= 8)
		{
			CreateIR(ir, LdlocVarVar);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		else
		{
			IL2CPP_ASSERT(size <= MAX_VALUE_TYPE_SIZE);
			CreateIR(ir, LdlocVarVarSize);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			ir->size = size;
			return ir;
		}
	}

	IRCommon* CreateAssignVarVar(TemporaryMemoryArena& pool, int32_t dstOffset, int32_t srcOffset, int32_t size)
	{
		IL2CPP_ASSERT(dstOffset >= 0 && dstOffset < 0x10000);
		IL2CPP_ASSERT(srcOffset >= 0 && srcOffset < 0x10000);
		if (size <= 8)
		{
			CreateIR(ir, LdlocVarVar);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			return ir;
		}
		else
		{
			IL2CPP_ASSERT(size <= MAX_VALUE_TYPE_SIZE);
			CreateIR(ir, LdlocVarVarSize);
			ir->dst = dstOffset;
			ir->src = srcOffset;
			ir->size = size;
			return ir;
		}
	}

    constexpr uint32_t kMaxShortFieldOffset = 0xFFFF;

	interpreter::IRCommon* CreateClassLdfldSmall(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, uint16_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, LdfldVarVar_i1);
		ir->dst = dstIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdfldVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdfldVarVar_i4, HiOpcodeEnum::LdfldVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdfldVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdfldVarVar_n_4);
				irn->dst = dstIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateClassLdfldLarge(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, uint32_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, LdfldLargeVarVar_i1);
		ir->dst = dstIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdfldLargeVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdfldLargeVarVar_i4, HiOpcodeEnum::LdfldLargeVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdfldLargeVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdfldLargeVarVar_n_4);
				irn->dst = dstIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}


	interpreter::IRCommon* CreateClassLdfld(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, const FieldInfo* fieldInfo)
	{
		uint32_t offset = GetFieldOffset(fieldInfo);
		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);
		if (offset <= kMaxShortFieldOffset)
		{
			return CreateClassLdfldSmall(pool, dstIdx, objIdx, (uint16_t)offset, desc);
		}
		else
		{
			return CreateClassLdfldLarge(pool, dstIdx, objIdx, offset, desc);
		}
	}

	interpreter::IRCommon* CreateValueTypeLdfldSmall(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, uint16_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, LdfldValueTypeVarVar_i1);
		ir->dst = dstIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdfldValueTypeVarVar_i4, HiOpcodeEnum::LdfldValueTypeVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdfldValueTypeVarVar_n_4);
				irn->dst = dstIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateValueTypeLdfldLarge(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, uint32_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, LdfldValueTypeLargeVarVar_i1);
		ir->dst = dstIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdfldValueTypeLargeVarVar_i4, HiOpcodeEnum::LdfldValueTypeLargeVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdfldValueTypeLargeVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdfldValueTypeLargeVarVar_n_4);
				irn->dst = dstIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateValueTypeLdfld(TemporaryMemoryArena& pool, int32_t dstIdx, int32_t objIdx, const FieldInfo* fieldInfo)
	{
		uint32_t offset = GetFieldOffset(fieldInfo);

		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);
		if (offset <= kMaxShortFieldOffset)
		{
			return CreateValueTypeLdfldSmall(pool, dstIdx, objIdx, (uint16_t)offset, desc);
		}
		else
		{
			return CreateValueTypeLdfldLarge(pool, dstIdx, objIdx, offset, desc);
        }
	}

	interpreter::IRCommon* CreateStfldSmall(TemporaryMemoryArena& pool, int32_t objIdx, const FieldInfo* fieldInfo, int32_t dataIdx, uint16_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, StfldVarVar_i1);
		ir->data = dataIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = HiOpcodeEnum::StfldVarVar_ref;
			return ir;
		}
		case LocationDescType::S:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::StfldVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, StfldVarVar_n_4);
				irn->data = dataIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		case LocationDescType::StructContainsRef:
		{
			CreateIR(irn, StfldVarVar_WriteBarrier_n_4);
			irn->data = dataIdx;
			irn->obj = objIdx;
			irn->offset = offset;
			irn->size = desc.size;
			return irn;
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateStfldLarge(TemporaryMemoryArena& pool, int32_t objIdx, const FieldInfo* fieldInfo, int32_t dataIdx, uint32_t offset, LocationDescInfo desc)
	{
		CreateIR(ir, StfldLargeVarVar_i1);
		ir->data = dataIdx;
		ir->obj = objIdx;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = HiOpcodeEnum::StfldLargeVarVar_ref;
			return ir;
		}
		case LocationDescType::S:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::StfldLargeVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, StfldLargeVarVar_n_4);
				irn->data = dataIdx;
				irn->obj = objIdx;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		case LocationDescType::StructContainsRef:
		{
			CreateIR(irn, StfldLargeVarVar_WriteBarrier_n_4);
			irn->data = dataIdx;
			irn->obj = objIdx;
			irn->offset = offset;
			irn->size = desc.size;
			return irn;
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateStfld(TemporaryMemoryArena& pool, int32_t objIdx, const FieldInfo* fieldInfo, int32_t dataIdx)
	{
		uint32_t offset = GetFieldOffset(fieldInfo);

		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);
		if (offset <= kMaxShortFieldOffset)
		{
			return CreateStfldSmall(pool, objIdx, fieldInfo, dataIdx, (uint16_t)offset, desc);
		}
		else
		{
			return CreateStfldLarge(pool, objIdx, fieldInfo, dataIdx, offset,  desc);
        }
	}

	interpreter::IRCommon* CreateLdsfld(TemporaryMemoryArena& pool, int32_t dstIdx, const FieldInfo* fieldInfo, uint32_t parent)
	{
		uint32_t offset = fieldInfo->offset;

		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);

		CreateIR(ir, LdsfldVarVar_i1);
		ir->dst = dstIdx;
		ir->klass = parent;
		ir->offset = offset;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdsfldVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdsfldVarVar_i4, HiOpcodeEnum::LdsfldVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdsfldVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdsfldVarVar_n_4);
				irn->dst = dstIdx;
				irn->klass = parent;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateStsfld(TemporaryMemoryArena& pool, const FieldInfo* fieldInfo, uint32_t parent, int32_t dataIdx)
	{
		uint32_t offset = fieldInfo->offset;


		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);

		CreateIR(ir, StsfldVarVar_i1);
		ir->klass = parent;
		ir->offset = offset;
		ir->data = dataIdx;
		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = HiOpcodeEnum::StsfldVarVar_ref;
			return ir;
		}
		case LocationDescType::S:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_20;
				return ir;
			}
			case 24: 
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_24;
				return ir;
			}
			case 28: 
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_28;
				return ir;
			}
			case 32: 
			{
				ir->type = HiOpcodeEnum::StsfldVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, StsfldVarVar_n_4);
				irn->klass = parent;
				irn->offset = offset;
				irn->data = dataIdx;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		case LocationDescType::StructContainsRef:
		{
			CreateIR(irn, StsfldVarVar_WriteBarrier_n_4);
			irn->klass = parent;
			irn->offset = offset;
			irn->data = dataIdx;
			irn->size = desc.size;
			return irn;
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateLdthreadlocal(TemporaryMemoryArena& pool, int32_t dstIdx, const FieldInfo* fieldInfo, uint32_t parent)
	{
		IL2CPP_ASSERT(fieldInfo->offset == THREAD_STATIC_FIELD_OFFSET);
		int32_t offset = GetThreadStaticFieldOffset(fieldInfo);

		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);

		CreateIR(ir, LdthreadlocalVarVar_i1);
		ir->dst = dstIdx;
		ir->klass = parent;
		ir->offset = offset;

		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::LdthreadlocalVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = ARCH_ARGUMENT(HiOpcodeEnum::LdthreadlocalVarVar_i4, HiOpcodeEnum::LdthreadlocalVarVar_i8);
			return ir;
		}
		case LocationDescType::S:
		case LocationDescType::StructContainsRef:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_16;
				return ir;
			}
			case 20:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_20;
				return ir;
			}
			case 24:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_24;
				return ir;
			}
			case 28:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_28;
				return ir;
			}
			case 32:
			{
				ir->type = HiOpcodeEnum::LdthreadlocalVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, LdthreadlocalVarVar_n_4);
				irn->dst = dstIdx;
				irn->klass = parent;
				irn->offset = offset;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	interpreter::IRCommon* CreateStthreadlocal(TemporaryMemoryArena& pool, const FieldInfo* fieldInfo, uint32_t parent, int32_t dataIdx)
	{
		IL2CPP_ASSERT(fieldInfo->offset == THREAD_STATIC_FIELD_OFFSET);
		int32_t offset = GetThreadStaticFieldOffset(fieldInfo);

		const Il2CppType* type = fieldInfo->type;
		LocationDescInfo desc = ComputLocationDescInfo(type);

		CreateIR(ir, StthreadlocalVarVar_i1);
		ir->klass = parent;
		ir->offset = offset;
		ir->data = dataIdx;

		switch (desc.type)
		{
		case LocationDescType::I1:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_i1;
			return ir;
		}
		case LocationDescType::U1:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_u1;
			return ir;
		}
		case LocationDescType::I2:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_i2;
			return ir;
		}
		case LocationDescType::U2:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_u2;
			return ir;
		}
		case LocationDescType::I4:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_i4;
			return ir;
		}
		case LocationDescType::I8:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_i8;
			return ir;
		}
		case LocationDescType::Ref:
		{
			ir->type = HiOpcodeEnum::StthreadlocalVarVar_ref;
			return ir;
		}
		case LocationDescType::S:
		{
			switch (desc.size)
			{
			case 12:
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_12;
				return ir;
			}
			case 16:
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_16;
				return ir;
			}
			case 20: 
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_20;
				return ir;
			}
			case 24: 
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_24;
				return ir;
			}
			case 28: 
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_28;
				return ir;
			}
			case 32: 
			{
				ir->type = HiOpcodeEnum::StthreadlocalVarVar_size_32;
				return ir;
			}
			default:
			{
				CreateIR(irn, StthreadlocalVarVar_n_4);
				irn->klass = parent;
				irn->offset = offset;
				irn->data = dataIdx;
				irn->size = desc.size;
				return irn;
			}
			}
		}
		case LocationDescType::StructContainsRef:
		{
			CreateIR(irn, StthreadlocalVarVar_WriteBarrier_n_4);
			irn->klass = parent;
			irn->offset = offset;
			irn->data = dataIdx;
			irn->size = desc.size;
			return irn;
		}
		default:
		{
			RaiseExecutionEngineException("field");
			return ir;
		}
		}
	}

	// TransformContext implements


	HiOpcodeEnum TransformContext::CalcGetMdArrElementVarVarOpcode(const Il2CppType* type)
	{
		LocationDescInfo desc = ComputLocationDescInfo(type);
		switch (desc.type)
		{
		case LocationDescType::I1: return HiOpcodeEnum::GetMdArrElementVarVar_i1;
		case LocationDescType::U1: return HiOpcodeEnum::GetMdArrElementVarVar_u1;
		case LocationDescType::I2: return HiOpcodeEnum::GetMdArrElementVarVar_i2;
		case LocationDescType::U2: return HiOpcodeEnum::GetMdArrElementVarVar_u2;
		case LocationDescType::I4: return HiOpcodeEnum::GetMdArrElementVarVar_i4;
		case LocationDescType::I8: return HiOpcodeEnum::GetMdArrElementVarVar_i8;
		case LocationDescType::Ref: return ARCH_ARGUMENT(HiOpcodeEnum::GetMdArrElementVarVar_i4, HiOpcodeEnum::GetMdArrElementVarVar_i8);
		case LocationDescType::S:
		case LocationDescType::StructContainsRef: return HiOpcodeEnum::GetMdArrElementVarVar_n;
		default:
		{
			RaiseExecutionEngineException("CalcGetMdArrElementVarVarOpcode");
			return (HiOpcodeEnum)0;
		}
		}
	}

	TransformContext::TransformContext(hotc233::metadata::Image* image, const MethodInfo* methodInfo, metadata::MethodBody& body, TemporaryMemoryArena& pool, il2cpp::utils::dynamic_array<uint64_t>& resolveDatas)
		: image(image), methodInfo(methodInfo), body(body), pool(pool), resolveDatas(resolveDatas),
		actualParamCount(0), ip2bb(nullptr), curbb(nullptr), args(nullptr), locals(nullptr), evalStack(nullptr),
		evalStackTop(0), evalStackBaseOffset(0), curStackSize(0), maxStackSize(0),
		nextFlowIdx(0), ipBase(nullptr), ip(nullptr), ipOffset(0), ir2offsetMap(nullptr),
		prefixFlags(0), shareMethod(nullptr), totalIRSize(0), totalArgSize(0), totalArgLocalSize(0), initLocals(false),
		typedRegisterEligibleI32Instructions(0), typedRegisterLongestI32Sequence(0), typedRegisterI32SequenceCount(0),
		godDomainFastPathKindOverride(0), godDomainFastPathParamOverride(0)
	{

	}

	TransformContext::~TransformContext()
	{
		for (IRBasicBlock* bb : irbbs)
		{
			bb->~IRBasicBlock();
		}
		if (ir2offsetMap)
		{
			delete ir2offsetMap;
		}
	}

	uint32_t TransformContext::AllocAndBakeNativeThunkSlot(const MethodInfo* method, interpreter::Hotc233DirectCallKind kind)
	{
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
		if (method == nullptr)
		{
			return 0;
		}
		int32_t index = 0;
		uint64_t* buf = nullptr;
		AllocResolvedData(resolveDatas, 1, index, buf);
		PreTouchNativeCalleeCode(method);
		il2cpp::vm::Class::Init(method->klass);
		InitAndGetInterpreterDirectlyCallMethodPointer(method);
		Il2CppMethodPointer directPtr = interpreter::ResolveDirectNativeMethodPointer(method, kind);
		buf[0] = directPtr != nullptr ? (uint64_t)directPtr : 0;
		return (uint32_t)index;
#else
		(void)method;
		(void)kind;
		return 0;
#endif
	}

	uint32_t TransformContext::GetOrAddResolveDataIndex(const void* ptr)
	{
		auto it = ptr2DataIdxs.find(ptr);
		if (it != ptr2DataIdxs.end())
		{
			return it->second;
		}
		else
		{
			uint32_t newIndex = (uint32_t)resolveDatas.size();
			resolveDatas.push_back((uint64_t)ptr);
			ptr2DataIdxs.insert({ ptr, newIndex });
			return newIndex;
		}
	}

	void TransformContext::PushStackByType(const Il2CppType* type)
	{
		int32_t byteSize = GetTypeValueSize(type);
		int32_t stackSize = GetStackSizeByByteSize(byteSize);
		evalStack[evalStackTop].reduceType = GetEvalStackReduceDataType(type);
		evalStack[evalStackTop].byteSize = byteSize;
		evalStack[evalStackTop].locOffset = GetEvalStackNewTopOffset();
		evalStackTop++;
		curStackSize += stackSize;
		maxStackSize = std::max(curStackSize, maxStackSize);
		IL2CPP_ASSERT(maxStackSize < MAX_STACK_SIZE);
	}

	void TransformContext::PushStackByReduceType(EvalStackReduceDataType t)
	{
		int32_t byteSize = GetSizeByReduceType(t);
		int32_t stackSize = GetStackSizeByByteSize(byteSize);
		evalStack[evalStackTop].reduceType = t;
		evalStack[evalStackTop].byteSize = byteSize;
		evalStack[evalStackTop].locOffset = GetEvalStackNewTopOffset();
		evalStackTop++; curStackSize += stackSize;
		maxStackSize = std::max(curStackSize, maxStackSize);
		IL2CPP_ASSERT(maxStackSize < MAX_STACK_SIZE);
	}

	void TransformContext::DuplicateStack()
	{
		IL2CPP_ASSERT(evalStackTop > 0);
		EvalStackVarInfo& oldTop = evalStack[evalStackTop - 1];
		int32_t stackSize = GetStackSizeByByteSize(oldTop.byteSize);
		EvalStackVarInfo& newTop = evalStack[evalStackTop++];
		newTop.reduceType = oldTop.reduceType;
		newTop.byteSize = oldTop.byteSize;
		newTop.locOffset = curStackSize;
		curStackSize += stackSize;
		maxStackSize = std::max(curStackSize, maxStackSize);
		IL2CPP_ASSERT(maxStackSize < MAX_STACK_SIZE);
	}

	void TransformContext::PopStack()
	{
		IL2CPP_ASSERT(evalStackTop > 0);
		--evalStackTop;
		curStackSize = evalStack[evalStackTop].locOffset;
	}

	void TransformContext::PopStackN(int32_t n)
	{
		IL2CPP_ASSERT(evalStackTop >= n && n >= 0);
		if (n > 0)
		{
			evalStackTop -= n;
			curStackSize = evalStack[evalStackTop].locOffset;
		}
	}

	void TransformContext::PopAllStack()
	{
		if (evalStackTop > 0)
		{
			evalStackTop = 0;
			curStackSize = evalStackBaseOffset;
		}
		else
		{
			IL2CPP_ASSERT(curStackSize == evalStackBaseOffset);
		}
	}

	void TransformContext::InsertMemoryBarrier()
	{
		if (prefixFlags & (int32_t)PrefixFlags::Volatile)
		{
			CreateAddIR(_mb, MemoryBarrier);
		}
	}

	void TransformContext::ResetPrefixFlags()
	{
		prefixFlags = 0;
	}

	void TransformContext::Add_ldind(HiOpcodeEnum opCode, EvalStackReduceDataType dataType)
	{
		CreateAddIR(ir, LdindVarVar_i1);
		ir->type = opCode;
		ir->dst = ir->src = GetEvalStackTopOffset();
		PopStack();
		PushStackByReduceType(dataType);
		InsertMemoryBarrier();
		ResetPrefixFlags();
		ip++;
	}

	void TransformContext::Add_stind(HiOpcodeEnum opCode)
	{
		IL2CPP_ASSERT(evalStackTop >= 2);
		InsertMemoryBarrier();
		ResetPrefixFlags();
		CreateAddIR(ir, StindVarVar_i1);
		ir->type = opCode;
		ir->dst = evalStack[evalStackTop - 2].locOffset;
		ir->src = evalStack[evalStackTop - 1].locOffset;
		PopStackN(2);
		ip++;
	}

	void TransformContext::PushOffset(int32_t* offsetPtr)
	{
		IL2CPP_ASSERT(splitOffsets.find(*(offsetPtr)) != splitOffsets.end());
		relocationOffsets.push_back(offsetPtr);
	}

	void TransformContext::PushBranch(int32_t targetOffset)
	{
		IL2CPP_ASSERT(splitOffsets.find(targetOffset) != splitOffsets.end());
		IRBasicBlock* targetBb = ip2bb[targetOffset];
		if (!targetBb->inPending)
		{
			targetBb->inPending = true;
			FlowInfo* fi = pool.NewAny<FlowInfo>();
			fi->offset = targetOffset;
			fi->curStackSize = curStackSize;
			if (evalStackTop > 0)
			{
				fi->evalStack.insert(fi->evalStack.end(), evalStack, evalStack + evalStackTop);
			}
			else
			{
				IL2CPP_ASSERT(curStackSize == evalStackBaseOffset);
			}
			pendingFlows.push_back(fi);
		}
	}

	bool TransformContext::FindNextFlow()
	{
		for (; nextFlowIdx < (int32_t)pendingFlows.size(); )
		{
			FlowInfo* fi = pendingFlows[nextFlowIdx++];
			IRBasicBlock* nextBb = ip2bb[fi->offset];
			if (!nextBb->visited)
			{
				ip = ipBase + fi->offset;
				if (!fi->evalStack.empty()) {

					std::memcpy(evalStack, &fi->evalStack[0], sizeof(EvalStackVarInfo) * fi->evalStack.size());
				}
				curStackSize = fi->curStackSize;
				IL2CPP_ASSERT(curStackSize >= evalStackBaseOffset);
				evalStackTop = (int32_t)fi->evalStack.size();
				return true;
			}
		}
		return false;
	}

	static bool IsNoOpTransformInstruction(IRCommon* ir)
	{
		if (ir->type == HiOpcodeEnum::LdlocVarVar)
		{
			IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
			return copy->dst == copy->src;
		}
		if (ir->type == HiOpcodeEnum::LdlocVarVarSize)
		{
			IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)ir;
			return copy->dst == copy->src;
		}
		switch (ir->type)
		{
		case HiOpcodeEnum::ConvertVarVar_i4_i4:
		case HiOpcodeEnum::ConvertVarVar_u4_u4:
		case HiOpcodeEnum::ConvertVarVar_i8_i8:
		case HiOpcodeEnum::ConvertVarVar_u8_u8:
		case HiOpcodeEnum::ConvertVarVar_f4_f4:
		case HiOpcodeEnum::ConvertVarVar_f8_f8:
		{
			IRConvertVarVar_i4_i4* conv = (IRConvertVarVar_i4_i4*)ir;
			return conv->dst == conv->src;
		}
		default:
			return false;
		}
	}

	static bool TrySkipPostStaticCallLocalStore(
		std::vector<IRCommon*>& insts,
		size_t& scanIdx,
		uint16_t callRet,
		uint16_t* outStoreDst,
		uint16_t* outStoreSrc)
	{
		if (outStoreDst)
		{
			*outStoreDst = 0xffff;
		}
		if (outStoreSrc)
		{
			*outStoreSrc = 0;
		}
		while (scanIdx < insts.size() && IsNoOpTransformInstruction(insts[scanIdx]))
		{
			scanIdx++;
		}
		if (scanIdx >= insts.size())
		{
			return false;
		}

		IRCommon* post = insts[scanIdx];
		if (post->type == HiOpcodeEnum::LdlocVarVar)
		{
			IRLdlocVarVar* copy = (IRLdlocVarVar*)post;
			if (copy->src != callRet)
			{
				return false;
			}
			if (outStoreDst)
			{
				*outStoreDst = copy->dst;
			}
			if (outStoreSrc)
			{
				*outStoreSrc = copy->src;
			}
			scanIdx++;
			return true;
		}
		if (post->type == HiOpcodeEnum::RegI32Copy)
		{
			IRRegI32Copy* copy = (IRRegI32Copy*)post;
			if (copy->src != callRet)
			{
				return false;
			}
			if (outStoreDst)
			{
				*outStoreDst = copy->dst;
			}
			if (outStoreSrc)
			{
				*outStoreSrc = copy->src;
			}
			scanIdx++;
			return true;
		}
		return false;
	}

	static bool IsDirectI4BinOp(HiOpcodeEnum type)
	{
		switch (type)
		{
		case HiOpcodeEnum::BinOpVarVarVar_Add_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Sub_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Mul_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Div_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Rem_i4:
		case HiOpcodeEnum::BinOpVarVarVar_And_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Or_i4:
		case HiOpcodeEnum::BinOpVarVarVar_Xor_i4:
			return true;
		default:
			return false;
		}
	}

	static bool IsDirectShiftOp(HiOpcodeEnum type)
	{
		switch (type)
		{
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i4_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i4_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i4_i8:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i8:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i4_i8:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i8_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i8_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i8_i4:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i8_i8:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i8_i8:
		case HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i8_i8:
			return true;
		default:
			return false;
		}
	}

	static bool TryFoldTwoCopiesIntoBinaryConsumer(IRCommon* first, IRCommon* second, IRCommon* consumer)
	{
		if (first->type != HiOpcodeEnum::LdlocVarVar || second->type != HiOpcodeEnum::LdlocVarVar)
		{
			return false;
		}
		IRLdlocVarVar* copy1 = (IRLdlocVarVar*)first;
		IRLdlocVarVar* copy2 = (IRLdlocVarVar*)second;
		if (copy1->dst == copy1->src || copy2->dst == copy2->src || copy1->dst == copy2->dst)
		{
			return false;
		}
		// Original order copies source2 after source1. If source2 reads the first
		// temporary, removing both copies would read the pre-copy value instead.
		if (copy2->src == copy1->dst)
		{
			return false;
		}
		if (IsDirectI4BinOp(consumer->type))
		{
			IRBinOpVarVarVar_Add_i4* op = (IRBinOpVarVarVar_Add_i4*)consumer;
			if (op->ret == copy1->dst && op->op1 == copy1->dst && op->op2 == copy2->dst)
			{
				op->op1 = copy1->src;
				op->op2 = copy2->src;
				return true;
			}
			return false;
		}
		if (IsDirectShiftOp(consumer->type))
		{
			IRBitShiftBinOpVarVarVar_Shl_i4_i4* op = (IRBitShiftBinOpVarVarVar_Shl_i4_i4*)consumer;
			if (op->ret == copy1->dst && op->value == copy1->dst && op->shiftAmount == copy2->dst)
			{
				op->value = copy1->src;
				op->shiftAmount = copy2->src;
				return true;
			}
		}
		return false;
	}

	static bool IsOpcodeInRange(HiOpcodeEnum type, HiOpcodeEnum first, HiOpcodeEnum last)
	{
		uint16_t value = (uint16_t)type;
		return value >= (uint16_t)first && value <= (uint16_t)last;
	}

	static bool IsCopyPropBinaryOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::BinOpVarVarVar_Add_i4, HiOpcodeEnum::BinOpVarVarVar_Rem_f8)
			|| IsOpcodeInRange(type, HiOpcodeEnum::BinOpOverflowVarVarVar_Add_i4, HiOpcodeEnum::BinOpOverflowVarVarVar_Mul_u8)
			|| type == HiOpcodeEnum::MathMinVarVarVar_i4
			|| type == HiOpcodeEnum::MathMaxVarVarVar_i4
			|| type == HiOpcodeEnum::MathMinVarVarVar_i8
			|| type == HiOpcodeEnum::MathMaxVarVarVar_i8;
	}

	static bool IsCopyPropShiftOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::BitShiftBinOpVarVarVar_Shl_i4_i4, HiOpcodeEnum::BitShiftBinOpVarVarVar_ShrUn_i8_i8);
	}

	static bool IsCopyPropCompareOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::CompOpVarVarVar_Ceq_i4, HiOpcodeEnum::CompOpVarVarVar_CltUn_f8);
	}

	static bool IsCopyPropUnaryOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::UnaryOpVarVar_Neg_i4, HiOpcodeEnum::UnaryOpVarVar_Neg_f8);
	}

	static bool IsCopyPropLdindOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::LdindVarVar_i1, HiOpcodeEnum::LdindVarVar_f8);
	}

	static bool IsCopyPropStindOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::StindVarVar_i1, HiOpcodeEnum::StindVarVar_ref);
	}

	static bool IsCopyPropFieldLoadOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::LdfldVarVar_i1, HiOpcodeEnum::LdfldVarVar_n_4)
			|| IsOpcodeInRange(type, HiOpcodeEnum::LdfldValueTypeVarVar_i1, HiOpcodeEnum::LdfldValueTypeVarVar_n_4);
	}

	static bool IsCopyPropArrayLoadOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::GetArrayElementVarVar_i1, HiOpcodeEnum::GetArrayElementVarVar_n);
	}

	static bool IsCopyPropArrayStoreOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::SetArrayElementVarVar_i1, HiOpcodeEnum::SetArrayElementVarVar_WriteBarrier_n);
	}

	static bool IsCopyPropInstanceFieldStoreOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::StfldVarVar_i1, HiOpcodeEnum::StfldVarVar_WriteBarrier_n_4);
	}

	static bool IsCopyPropStaticFieldStoreOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::StsfldVarVar_i1, HiOpcodeEnum::StsfldVarVar_WriteBarrier_n_4);
	}

	static bool IsCopyPropThreadStaticFieldStoreOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::StthreadlocalVarVar_i1, HiOpcodeEnum::StthreadlocalVarVar_WriteBarrier_n_4);
	}

	static bool IsCopyPropBranchOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::BranchTrueVar_i4, HiOpcodeEnum::BranchSwitch)
			|| type == HiOpcodeEnum::LeaveEx
			|| type == HiOpcodeEnum::LeaveEx_Directly;
	}

	static bool IsCopyPropCallOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::CallNativeInstance_void, HiOpcodeEnum::CallDelegateEndInvoke_ret)
			|| IsOpcodeInRange(type, HiOpcodeEnum::CallCommonNativeInstance_v_0, HiOpcodeEnum::CallCommonNativeStatic_f8_f8_4);
	}

	static bool IsCopyPropRetOp(HiOpcodeEnum type)
	{
		return IsOpcodeInRange(type, HiOpcodeEnum::RetVar_ret_1, HiOpcodeEnum::RetVar_ret_n);
	}

	static bool IsCopyPropStoreBarrierOp(HiOpcodeEnum type)
	{
		return IsCopyPropStindOp(type)
			|| IsCopyPropArrayStoreOp(type)
			|| IsCopyPropInstanceFieldStoreOp(type)
			|| IsCopyPropStaticFieldStoreOp(type)
			|| IsCopyPropThreadStaticFieldStoreOp(type)
			|| type == HiOpcodeEnum::MemoryBarrier
			|| type == HiOpcodeEnum::CpblkVarVar
			|| type == HiOpcodeEnum::InitblkVarVarVar;
	}

	static bool IsOioCopyPropagationUnsupportedOp(HiOpcodeEnum type)
	{
		return IsCopyPropCallOp(type)
			|| type == HiOpcodeEnum::LdstrVar
			|| type == HiOpcodeEnum::NewString
			|| type == HiOpcodeEnum::NewString_2
			|| type == HiOpcodeEnum::NewString_3
			|| type == HiOpcodeEnum::NewDelegate
			|| IsOpcodeInRange(type, HiOpcodeEnum::NewClassVar, HiOpcodeEnum::AdjustValueTypeRefVar)
			|| IsOpcodeInRange(type, HiOpcodeEnum::BoxVarVar, HiOpcodeEnum::UnBoxAnyVarVar)
			|| type == HiOpcodeEnum::CastclassVar
			|| type == HiOpcodeEnum::IsInstVar
			|| type == HiOpcodeEnum::LdtokenVar;
	}

	struct OioCopyAlias
	{
		int32_t src;
		IRLdlocVarVar* copy;
	};

	static bool IsValidCopyPropSlot(const std::vector<OioCopyAlias>& aliases, int32_t slot)
	{
		return slot >= 0 && (size_t)slot < aliases.size();
	}

	static int32_t ResolveCopyPropSlot(const std::vector<OioCopyAlias>& aliases, int32_t slot)
	{
		int32_t cur = slot;
		for (int32_t depth = 0; depth < 16 && IsValidCopyPropSlot(aliases, cur) && aliases[cur].src >= 0; depth++)
		{
			cur = aliases[cur].src;
		}
		return cur;
	}

	static void ClearCopyAlias(std::vector<OioCopyAlias>& aliases, int32_t slot)
	{
		if (IsValidCopyPropSlot(aliases, slot))
		{
			aliases[slot].src = -1;
			aliases[slot].copy = nullptr;
		}
	}

	static void CountCopyPropRead(std::vector<int32_t>& reads, uint16_t slot)
	{
		if ((size_t)slot < reads.size())
		{
			reads[slot]++;
		}
	}

	static void ConsumeCopyPropRead(std::vector<int32_t>& reads, uint16_t slot)
	{
		if ((size_t)slot < reads.size() && reads[slot] > 0)
		{
			reads[slot]--;
		}
	}

	static void MaterializeCopyAlias(std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, int32_t slot, bool requireFutureRead, const std::vector<int32_t>& remainingReads)
	{
		if (!IsValidCopyPropSlot(aliases, slot) || aliases[slot].src < 0)
		{
			return;
		}
		if (requireFutureRead && ((size_t)slot >= remainingReads.size() || remainingReads[slot] <= 0))
		{
			ClearCopyAlias(aliases, slot);
			return;
		}
		IRLdlocVarVar* copy = aliases[slot].copy;
		copy->src = (uint16_t)ResolveCopyPropSlot(aliases, aliases[slot].src);
		output.push_back(copy);
		ClearCopyAlias(aliases, slot);
	}

	static void MaterializeAliasesReadingSlot(std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, int32_t writtenSlot, const std::vector<int32_t>& remainingReads)
	{
		for (size_t slot = 0; slot < aliases.size(); slot++)
		{
			if (aliases[slot].src >= 0 && ResolveCopyPropSlot(aliases, aliases[slot].src) == writtenSlot)
			{
				MaterializeCopyAlias(output, aliases, (int32_t)slot, true, remainingReads);
			}
		}
	}

	static void MaterializeAllCopyAliases(std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, bool requireFutureRead, const std::vector<int32_t>& remainingReads)
	{
		for (size_t slot = 0; slot < aliases.size(); slot++)
		{
			MaterializeCopyAlias(output, aliases, (int32_t)slot, requireFutureRead, remainingReads);
		}
	}

	static void RewriteCopyPropRead(std::vector<OioCopyAlias>& aliases, std::vector<int32_t>& remainingReads, uint16_t& slot)
	{
		uint16_t original = slot;
		ConsumeCopyPropRead(remainingReads, original);
		if (IsValidCopyPropSlot(aliases, original) && aliases[original].src >= 0)
		{
			slot = (uint16_t)ResolveCopyPropSlot(aliases, aliases[original].src);
			if ((size_t)original < remainingReads.size() && remainingReads[original] <= 0)
			{
				ClearCopyAlias(aliases, original);
			}
		}
	}

	static void MaterializeAddressRead(std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, std::vector<int32_t>& remainingReads, uint16_t slot)
	{
		ConsumeCopyPropRead(remainingReads, slot);
		MaterializeCopyAlias(output, aliases, slot, false, remainingReads);
	}

	static void DefineCopyPropSlot(std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, const std::vector<int32_t>& remainingReads, uint16_t slot)
	{
		MaterializeAliasesReadingSlot(output, aliases, slot, remainingReads);
		ClearCopyAlias(aliases, slot);
	}

	template<typename TRead>
	static void ForEachCopyPropRead(IRCommon* ir, TRead read)
	{
		if (IsCopyPropBinaryOp(ir->type))
		{
			IRBinOpVarVarVar_Add_i4* op = (IRBinOpVarVarVar_Add_i4*)ir;
			read(op->op1);
			read(op->op2);
			return;
		}
		if (IsCopyPropShiftOp(ir->type))
		{
			IRBitShiftBinOpVarVarVar_Shl_i4_i4* op = (IRBitShiftBinOpVarVarVar_Shl_i4_i4*)ir;
			read(op->value);
			read(op->shiftAmount);
			return;
		}
		if (IsCopyPropCompareOp(ir->type))
		{
			IRCompOpVarVarVar_Ceq_i4* op = (IRCompOpVarVarVar_Ceq_i4*)ir;
			read(op->c1);
			read(op->c2);
			return;
		}
		if (IsCopyPropUnaryOp(ir->type))
		{
			IRUnaryOpVarVar_Neg_i4* op = (IRUnaryOpVarVar_Neg_i4*)ir;
			read(op->src);
			return;
		}
		switch (ir->type)
		{
		case HiOpcodeEnum::LdlocVarVar:
		case HiOpcodeEnum::LdlocExpandVarVar_i1:
		case HiOpcodeEnum::LdlocExpandVarVar_u1:
		case HiOpcodeEnum::LdlocExpandVarVar_i2:
		case HiOpcodeEnum::LdlocExpandVarVar_u2:
		case HiOpcodeEnum::LdlocVarAddress:
		{
			IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
			read(copy->src);
			return;
		}
		case HiOpcodeEnum::LdlocVarVarSize:
		{
			IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)ir;
			read(copy->src);
			return;
		}
		case HiOpcodeEnum::CheckFiniteVar_f4:
		case HiOpcodeEnum::CheckFiniteVar_f8:
		{
			IRCheckFiniteVar_f4* check = (IRCheckFiniteVar_f4*)ir;
			read(check->src);
			return;
		}
		case HiOpcodeEnum::BranchTrueVar_i4:
		case HiOpcodeEnum::BranchTrueVar_i8:
		case HiOpcodeEnum::BranchFalseVar_i4:
		case HiOpcodeEnum::BranchFalseVar_i8:
		{
			IRBranchTrueVar_i4* branch = (IRBranchTrueVar_i4*)ir;
			read(branch->op);
			return;
		}
		default:
			break;
		}
		if (IsOpcodeInRange(ir->type, HiOpcodeEnum::BranchVarVar_Ceq_i4, HiOpcodeEnum::BranchVarVar_CleUn_f8))
		{
			IRBranchVarVar_Ceq_i4* branch = (IRBranchVarVar_Ceq_i4*)ir;
			read(branch->op1);
			read(branch->op2);
			return;
		}
		if (IsCopyPropLdindOp(ir->type))
		{
			IRLdindVarVar_i1* ind = (IRLdindVarVar_i1*)ir;
			read(ind->src);
			return;
		}
		if (IsCopyPropStindOp(ir->type))
		{
			IRStindVarVar_i1* store = (IRStindVarVar_i1*)ir;
			read(store->dst);
			read(store->src);
			return;
		}
		if (IsCopyPropFieldLoadOp(ir->type))
		{
			IRLdfldVarVar_i1* field = (IRLdfldVarVar_i1*)ir;
			read(field->obj);
			return;
		}
		if (IsCopyPropArrayLoadOp(ir->type))
		{
			IRGetArrayElementVarVar_i1* element = (IRGetArrayElementVarVar_i1*)ir;
			read(element->arr);
			read(element->index);
			return;
		}
		if (IsCopyPropArrayStoreOp(ir->type))
		{
			IRSetArrayElementVarVar_i1* element = (IRSetArrayElementVarVar_i1*)ir;
			read(element->arr);
			read(element->index);
			read(element->ele);
			return;
		}
		if (IsCopyPropInstanceFieldStoreOp(ir->type))
		{
			IRStfldVarVar_i1* field = (IRStfldVarVar_i1*)ir;
			read(field->obj);
			read(field->data);
			return;
		}
		if (IsCopyPropStaticFieldStoreOp(ir->type))
		{
			IRStsfldVarVar_i1* field = (IRStsfldVarVar_i1*)ir;
			read(field->data);
			return;
		}
		if (IsCopyPropThreadStaticFieldStoreOp(ir->type))
		{
			IRStthreadlocalVarVar_i1* field = (IRStthreadlocalVarVar_i1*)ir;
			read(field->data);
			return;
		}
		if (IsCopyPropRetOp(ir->type))
		{
			IRRetVar_ret_1* ret = (IRRetVar_ret_1*)ir;
			read(ret->ret);
			return;
		}
	}

	template<typename TDef>
	static void ForEachCopyPropDef(IRCommon* ir, TDef define)
	{
		if (IsCopyPropBinaryOp(ir->type))
		{
			IRBinOpVarVarVar_Add_i4* op = (IRBinOpVarVarVar_Add_i4*)ir;
			define(op->ret);
			return;
		}
		if (IsCopyPropShiftOp(ir->type))
		{
			IRBitShiftBinOpVarVarVar_Shl_i4_i4* op = (IRBitShiftBinOpVarVarVar_Shl_i4_i4*)ir;
			define(op->ret);
			return;
		}
		if (IsCopyPropCompareOp(ir->type))
		{
			IRCompOpVarVarVar_Ceq_i4* op = (IRCompOpVarVarVar_Ceq_i4*)ir;
			define(op->ret);
			return;
		}
		if (IsCopyPropUnaryOp(ir->type))
		{
			IRUnaryOpVarVar_Neg_i4* op = (IRUnaryOpVarVar_Neg_i4*)ir;
			define(op->dst);
			return;
		}
		switch (ir->type)
		{
		case HiOpcodeEnum::LdcVarConst_1:
		{
			IRLdcVarConst_1* constant = (IRLdcVarConst_1*)ir;
			define(constant->dst);
			return;
		}
		case HiOpcodeEnum::LdcVarConst_2:
		case HiOpcodeEnum::LdcVarConst_4:
		case HiOpcodeEnum::LdcVarConst_8:
		case HiOpcodeEnum::LdnullVar:
		{
			IRLdcVarConst_2* constant = (IRLdcVarConst_2*)ir;
			define(constant->dst);
			return;
		}
		case HiOpcodeEnum::LdlocVarVar:
		case HiOpcodeEnum::LdlocExpandVarVar_i1:
		case HiOpcodeEnum::LdlocExpandVarVar_u1:
		case HiOpcodeEnum::LdlocExpandVarVar_i2:
		case HiOpcodeEnum::LdlocExpandVarVar_u2:
		case HiOpcodeEnum::LdlocVarAddress:
		{
			IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
			define(copy->dst);
			return;
		}
		case HiOpcodeEnum::LdlocVarVarSize:
		{
			IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)ir;
			define(copy->dst);
			return;
		}
		default:
			break;
		}
		if (IsCopyPropLdindOp(ir->type))
		{
			IRLdindVarVar_i1* ind = (IRLdindVarVar_i1*)ir;
			define(ind->dst);
			return;
		}
		if (IsCopyPropFieldLoadOp(ir->type))
		{
			IRLdfldVarVar_i1* field = (IRLdfldVarVar_i1*)ir;
			define(field->dst);
			return;
		}
		if (IsCopyPropArrayLoadOp(ir->type))
		{
			IRGetArrayElementVarVar_i1* element = (IRGetArrayElementVarVar_i1*)ir;
			define(element->dst);
			return;
		}
	}

	static bool TryRewriteCopyPropReads(IRCommon* ir, std::vector<IRCommon*>& output, std::vector<OioCopyAlias>& aliases, std::vector<int32_t>& remainingReads)
	{
		if (IsCopyPropBinaryOp(ir->type))
		{
			IRBinOpVarVarVar_Add_i4* op = (IRBinOpVarVarVar_Add_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, op->op1);
			RewriteCopyPropRead(aliases, remainingReads, op->op2);
			return true;
		}
		if (IsCopyPropShiftOp(ir->type))
		{
			IRBitShiftBinOpVarVarVar_Shl_i4_i4* op = (IRBitShiftBinOpVarVarVar_Shl_i4_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, op->value);
			RewriteCopyPropRead(aliases, remainingReads, op->shiftAmount);
			return true;
		}
		if (IsCopyPropCompareOp(ir->type))
		{
			IRCompOpVarVarVar_Ceq_i4* op = (IRCompOpVarVarVar_Ceq_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, op->c1);
			RewriteCopyPropRead(aliases, remainingReads, op->c2);
			return true;
		}
		if (IsCopyPropUnaryOp(ir->type))
		{
			IRUnaryOpVarVar_Neg_i4* op = (IRUnaryOpVarVar_Neg_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, op->src);
			return true;
		}
		switch (ir->type)
		{
		case HiOpcodeEnum::LdcVarConst_1:
		case HiOpcodeEnum::LdcVarConst_2:
		case HiOpcodeEnum::LdcVarConst_4:
		case HiOpcodeEnum::LdcVarConst_8:
		case HiOpcodeEnum::LdnullVar:
			return true;
		case HiOpcodeEnum::LdlocVarVar:
		case HiOpcodeEnum::LdlocExpandVarVar_i1:
		case HiOpcodeEnum::LdlocExpandVarVar_u1:
		case HiOpcodeEnum::LdlocExpandVarVar_i2:
		case HiOpcodeEnum::LdlocExpandVarVar_u2:
		{
			IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
			RewriteCopyPropRead(aliases, remainingReads, copy->src);
			return true;
		}
		case HiOpcodeEnum::LdlocVarVarSize:
		{
			IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)ir;
			RewriteCopyPropRead(aliases, remainingReads, copy->src);
			return true;
		}
		case HiOpcodeEnum::LdlocVarAddress:
		{
			IRLdlocVarAddress* address = (IRLdlocVarAddress*)ir;
			MaterializeAddressRead(output, aliases, remainingReads, address->src);
			return true;
		}
		case HiOpcodeEnum::CheckFiniteVar_f4:
		case HiOpcodeEnum::CheckFiniteVar_f8:
		{
			IRCheckFiniteVar_f4* check = (IRCheckFiniteVar_f4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, check->src);
			return true;
		}
		case HiOpcodeEnum::BranchTrueVar_i4:
		case HiOpcodeEnum::BranchTrueVar_i8:
		case HiOpcodeEnum::BranchFalseVar_i4:
		case HiOpcodeEnum::BranchFalseVar_i8:
		{
			IRBranchTrueVar_i4* branch = (IRBranchTrueVar_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, branch->op);
			return true;
		}
		default:
			break;
		}
		if (IsOpcodeInRange(ir->type, HiOpcodeEnum::BranchVarVar_Ceq_i4, HiOpcodeEnum::BranchVarVar_CleUn_f8))
		{
			IRBranchVarVar_Ceq_i4* branch = (IRBranchVarVar_Ceq_i4*)ir;
			RewriteCopyPropRead(aliases, remainingReads, branch->op1);
			RewriteCopyPropRead(aliases, remainingReads, branch->op2);
			return true;
		}
		if (IsCopyPropLdindOp(ir->type))
		{
			IRLdindVarVar_i1* ind = (IRLdindVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, ind->src);
			return true;
		}
		if (IsCopyPropStindOp(ir->type))
		{
			IRStindVarVar_i1* store = (IRStindVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, store->dst);
			RewriteCopyPropRead(aliases, remainingReads, store->src);
			return true;
		}
		if (IsCopyPropFieldLoadOp(ir->type))
		{
			IRLdfldVarVar_i1* field = (IRLdfldVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, field->obj);
			return true;
		}
		if (IsCopyPropArrayLoadOp(ir->type))
		{
			IRGetArrayElementVarVar_i1* element = (IRGetArrayElementVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, element->arr);
			RewriteCopyPropRead(aliases, remainingReads, element->index);
			return true;
		}
		if (IsCopyPropArrayStoreOp(ir->type))
		{
			IRSetArrayElementVarVar_i1* element = (IRSetArrayElementVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, element->arr);
			RewriteCopyPropRead(aliases, remainingReads, element->index);
			RewriteCopyPropRead(aliases, remainingReads, element->ele);
			return true;
		}
		if (IsCopyPropInstanceFieldStoreOp(ir->type))
		{
			IRStfldVarVar_i1* field = (IRStfldVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, field->obj);
			RewriteCopyPropRead(aliases, remainingReads, field->data);
			return true;
		}
		if (IsCopyPropStaticFieldStoreOp(ir->type))
		{
			IRStsfldVarVar_i1* field = (IRStsfldVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, field->data);
			return true;
		}
		if (IsCopyPropThreadStaticFieldStoreOp(ir->type))
		{
			IRStthreadlocalVarVar_i1* field = (IRStthreadlocalVarVar_i1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, field->data);
			return true;
		}
		if (IsCopyPropRetOp(ir->type))
		{
			IRRetVar_ret_1* ret = (IRRetVar_ret_1*)ir;
			RewriteCopyPropRead(aliases, remainingReads, ret->ret);
			return true;
		}
		return false;
	}

	static void ApplyOioCopyPropagation(std::vector<IRCommon*>& insts, int32_t evalStackBaseOffset, int32_t maxStackSize)
	{
		if (insts.empty() || maxStackSize <= 0)
		{
			return;
		}
		for (IRCommon* ir : insts)
		{
			if (IsOioCopyPropagationUnsupportedOp(ir->type))
			{
				return;
			}
		}
		std::vector<int32_t> remainingReads((size_t)maxStackSize + 1, 0);
		for (IRCommon* ir : insts)
		{
			ForEachCopyPropRead(ir, [&](uint16_t slot) { CountCopyPropRead(remainingReads, slot); });
		}

		std::vector<OioCopyAlias> aliases((size_t)maxStackSize + 1);
		for (OioCopyAlias& alias : aliases)
		{
			alias.src = -1;
			alias.copy = nullptr;
		}

		std::vector<IRCommon*> output;
		output.reserve(insts.size());
		for (IRCommon* ir : insts)
		{
			if (ir->type == HiOpcodeEnum::LdlocVarVar)
			{
				IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
				RewriteCopyPropRead(aliases, remainingReads, copy->src);
				DefineCopyPropSlot(output, aliases, remainingReads, copy->dst);
				if (copy->dst >= evalStackBaseOffset && IsValidCopyPropSlot(aliases, copy->dst) && copy->dst != copy->src)
				{
					aliases[copy->dst].src = copy->src;
					aliases[copy->dst].copy = copy;
					continue;
				}
				output.push_back(ir);
				continue;
			}

			bool rewritten = TryRewriteCopyPropReads(ir, output, aliases, remainingReads);
			if (!rewritten || IsCopyPropCallOp(ir->type) || ir->type == HiOpcodeEnum::ThrowEx || ir->type == HiOpcodeEnum::RethrowEx)
			{
				MaterializeAllCopyAliases(output, aliases, false, remainingReads);
			}
			if (IsCopyPropBranchOp(ir->type))
			{
				MaterializeAllCopyAliases(output, aliases, false, remainingReads);
			}
			if (IsCopyPropStoreBarrierOp(ir->type))
			{
				MaterializeAllCopyAliases(output, aliases, true, remainingReads);
			}
			ForEachCopyPropDef(ir, [&](uint16_t slot) { DefineCopyPropSlot(output, aliases, remainingReads, slot); });
			output.push_back(ir);
		}
		MaterializeAllCopyAliases(output, aliases, false, remainingReads);
		insts.swap(output);
	}

	void TransformContext::AddInst(IRCommon* ir)
	{
		IL2CPP_ASSERT(ir->type != HiOpcodeEnum::None);
		if (IsNoOpTransformInstruction(ir))
		{
			return;
		}
		curbb->insts.push_back(ir);
		if (ir2offsetMap)
		{
			ir2offsetMap->insert({ ir, ipOffset });
		}
	}

	void TransformContext::ApplyCommunityPeepholeFusion()
	{
		for (IRBasicBlock* bb : irbbs)
		{
			std::vector<IRCommon*>& insts = bb->insts;
			size_t writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (IsNoOpTransformInstruction(ir))
				{
					continue;
				}
				if (readIdx + 2 < insts.size() && TryFoldTwoCopiesIntoBinaryConsumer(ir, insts[readIdx + 1], insts[readIdx + 2]))
				{
					readIdx += 2;
					ir = insts[readIdx];
				}
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::ConvertVarVar_i4_u1 && next->type == HiOpcodeEnum::SetArrayElementVarVar_i1)
					{
						IRConvertVarVar_i4_u1* convert = (IRConvertVarVar_i4_u1*)ir;
						IRSetArrayElementVarVar_i1* store = (IRSetArrayElementVarVar_i1*)next;
						if (store->ele == convert->dst)
						{
							CreateIR(fused, ConvertVarVar_i4_u1_SetArrayElementVarVar_i1);
							fused->convertDst = convert->dst;
							fused->convertSrc = convert->src;
							fused->arraySrc = store->arr;
							fused->indexSrc = store->index;
							insts[writeIdx++] = fused;
							readIdx++;
							continue;
						}
					}
					if (readIdx + 2 < insts.size()
						&& ir->type == HiOpcodeEnum::GetArrayElementVarVar_i4
						&& next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4
						&& insts[readIdx + 2]->type == HiOpcodeEnum::SetArrayElementVarVar_i4)
					{
						IRGetArrayElementVarVar_i4* load = (IRGetArrayElementVarVar_i4*)ir;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)next;
						IRSetArrayElementVarVar_i4* store = (IRSetArrayElementVarVar_i4*)insts[readIdx + 2];
						if (store->ele == add->ret && add->op1 == load->dst)
						{
							CreateIR(fused, GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4);
							fused->loadArraySrc = load->arr;
							fused->loadIndexSrc = load->index;
							fused->addValueSrc = add->op2;
							fused->storeArraySrc = store->arr;
							fused->storeIndexSrc = store->index;
							insts[writeIdx++] = fused;
							readIdx += 2;
							continue;
						}
					}
					if (readIdx + 3 < insts.size()
						&& ir->type == HiOpcodeEnum::GetArrayElementVarVar_i4
						&& next->type == HiOpcodeEnum::LdlocVarVar
						&& insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4
						&& insts[readIdx + 3]->type == HiOpcodeEnum::SetArrayElementVarVar_i4
						&& !IsNoOpTransformInstruction(next))
					{
						IRGetArrayElementVarVar_i4* load = (IRGetArrayElementVarVar_i4*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
						IRSetArrayElementVarVar_i4* store = (IRSetArrayElementVarVar_i4*)insts[readIdx + 3];
						bool sameValueFlow = store->ele == add->ret;
						bool loadPlusCopy = add->op1 == load->dst && add->op2 == copy->dst;
						bool copyPlusLoad = add->op1 == copy->dst && add->op2 == load->dst;
						if (sameValueFlow && (loadPlusCopy || copyPlusLoad))
						{
							CreateIR(fused, GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4);
							fused->loadArraySrc = load->arr;
							fused->loadIndexSrc = load->index;
							fused->addValueSrc = copy->src;
							fused->storeArraySrc = store->arr;
							fused->storeIndexSrc = store->index;
							insts[writeIdx++] = fused;
							readIdx += 3;
							continue;
						}
					}
					if (readIdx + 4 < insts.size() && ir->type == HiOpcodeEnum::SetArrayElementVarVar_size_28 && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 3]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && insts[readIdx + 4]->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(insts[readIdx + 4]))
					{
						IRSetArrayElementVarVar_size_12* store = (IRSetArrayElementVarVar_size_12*)ir;
						IRLdlocVarVar* copy1 = (IRLdlocVarVar*)next;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 2];
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 3];
						IRLdlocVarVar* copy2 = (IRLdlocVarVar*)insts[readIdx + 4];
						CreateIR(fused, SetArrayElementVarVar_size_28_LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar);
						fused->arraySrc = store->arr;
						fused->indexSrc = store->index;
						fused->elementSrc = store->ele;
						fused->copyDst1 = copy1->dst;
						fused->copySrc1 = copy1->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->copyDst2 = copy2->dst;
						fused->copySrc2 = copy2->src;
						insts[writeIdx++] = fused;
						readIdx += 4;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::BranchVarVar_CneUn_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRBranchVarVar_CneUn_i4* branch = (IRBranchVarVar_CneUn_i4*)next;
						CreateIR(fused, LdlocVarVar_BranchVarVar_CneUn_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offset = branch->offset;
						relocationOffsets.push_back(&fused->offset);
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4 && insts[readIdx + 2]->type == HiOpcodeEnum::StindVarVar_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRBinOpVarVarVar_Sub_i4* sub = (IRBinOpVarVarVar_Sub_i4*)next;
						IRStindVarVar_i4* store = (IRStindVarVar_i4*)insts[readIdx + 2];
						if (store->src == sub->ret && store->dst != sub->ret)
						{
							CreateIR(fused, LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4);
							fused->copyDst = copy->dst;
							fused->copySrc = copy->src;
							fused->subRet = sub->ret;
							fused->subOp1 = sub->op1;
							fused->subOp2 = sub->op2;
							fused->storeAddress = store->dst;
							insts[writeIdx++] = fused;
							readIdx += 2;
							continue;
						}
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::BranchVarVar_Clt_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRBranchVarVar_Clt_i4* branch = (IRBranchVarVar_Clt_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BranchVarVar_Clt_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offset = branch->offset;
						relocationOffsets.push_back(&fused->offset);
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && insts[readIdx + 3]->type == HiOpcodeEnum::LdlocVarVar)
					{
						IRLdlocVarVar* copy1 = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
						IRLdlocVarVar* copy2 = (IRLdlocVarVar*)insts[readIdx + 3];
						if (!IsNoOpTransformInstruction(copy2))
						{
							CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar);
							fused->copyDst1 = copy1->dst;
							fused->copySrc1 = copy1->src;
							fused->constDst = constant->dst;
							fused->constant = constant->src;
							fused->addRet = add->ret;
							fused->addOp1 = add->op1;
							fused->addOp2 = add->op2;
							fused->copyDst2 = copy2->dst;
							fused->copySrc2 = copy2->src;
							insts[writeIdx++] = fused;
							readIdx += 3;
							continue;
						}
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpAdd_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Rem_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRBinOpVarVarVar_Rem_i4* rem = (IRBinOpVarVarVar_Rem_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpRem_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->remRet = rem->ret;
						fused->remOp1 = rem->op1;
						fused->remOp2 = rem->op2;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Mul_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRBinOpVarVarVar_Mul_i4* mul = (IRBinOpVarVarVar_Mul_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpMul_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->mulRet = mul->ret;
						fused->mulOp1 = mul->op1;
						fused->mulOp2 = mul->op2;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdcVarConst_4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						CreateIR(fused, LdlocVarVar_LdcVarConst_4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && insts[readIdx + 2]->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 3]->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4)
					{
						IRBinOpVarVarVar_Sub_i4* sub = (IRBinOpVarVarVar_Sub_i4*)ir;
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)next;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 2];
						IRBinOpVarVarVar_Div_i4* div = (IRBinOpVarVarVar_Div_i4*)insts[readIdx + 3];
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4);
						fused->subRet = sub->ret;
						fused->subOp1 = sub->op1;
						fused->subOp2 = sub->op2;
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						fused->divRet = div->ret;
						fused->divOp1 = div->op1;
						fused->divOp2 = div->op2;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4 && next->type == HiOpcodeEnum::StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4* sub = (IRBinOpVarVarVar_Sub_i4*)ir;
						IRStindVarVar_i4* store = (IRStindVarVar_i4*)next;
						if (store->src == sub->ret && store->dst != sub->ret)
						{
							CreateIR(fused, BinOpVarVarVar_Sub_i4_StindVarVar_i4);
							fused->subRet = sub->ret;
							fused->subOp1 = sub->op1;
							fused->subOp2 = sub->op2;
							fused->storeAddress = store->dst;
							insts[writeIdx++] = fused;
							readIdx++;
							continue;
						}
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4 && next->type == HiOpcodeEnum::MathMaxVarVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4* sub = (IRBinOpVarVarVar_Sub_i4*)ir;
						IRBinOpVarVarVar_Add_i4* max = (IRBinOpVarVarVar_Add_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4);
						fused->subRet = sub->ret;
						fused->subOp1 = sub->op1;
						fused->subOp2 = sub->op2;
						fused->maxRet = max->ret;
						fused->maxOp1 = max->op1;
						fused->maxOp2 = max->op2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 4 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarAddress && next->type == HiOpcodeEnum::LdfldaVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::LdindVarVar_i4 && insts[readIdx + 4]->type == HiOpcodeEnum::LdcVarConst_4 && !IsNoOpTransformInstruction(insts[readIdx + 2]))
					{
						IRLdlocVarAddress* address = (IRLdlocVarAddress*)ir;
						IRLdfldaVarVar* fieldAddress = (IRLdfldaVarVar*)next;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)insts[readIdx + 2];
						IRLdindVarVar_i4* ind = (IRLdindVarVar_i4*)insts[readIdx + 3];
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 4];
						CreateIR(fused, LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4);
						fused->addressDst = address->dst;
						fused->addressSrc = address->src;
						fused->fieldAddressDst = fieldAddress->dst;
						fused->obj = fieldAddress->obj;
						fused->offset = fieldAddress->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->indDst = ind->dst;
						fused->indSrc = ind->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx += 4;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarAddress && next->type == HiOpcodeEnum::LdfldaVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::LdindVarVar_i4 && !IsNoOpTransformInstruction(insts[readIdx + 2]))
					{
						IRLdlocVarAddress* address = (IRLdlocVarAddress*)ir;
						IRLdfldaVarVar* fieldAddress = (IRLdfldaVarVar*)next;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)insts[readIdx + 2];
						IRLdindVarVar_i4* ind = (IRLdindVarVar_i4*)insts[readIdx + 3];
						CreateIR(fused, LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4);
						fused->addressDst = address->dst;
						fused->addressSrc = address->src;
						fused->fieldAddressDst = fieldAddress->dst;
						fused->obj = fieldAddress->obj;
						fused->offset = fieldAddress->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->indDst = ind->dst;
						fused->indSrc = ind->src;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarAddress && next->type == HiOpcodeEnum::LdcVarConst_4)
					{
						IRLdlocVarAddress* address = (IRLdlocVarAddress*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						CreateIR(fused, LdlocVarAddress_LdcVarConst_4);
						fused->addressDst = address->dst;
						fused->addressSrc = address->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdindVarVar_i4)
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdindVarVar_i4* ind = (IRLdindVarVar_i4*)next;
						CreateIR(fused, LdlocVarVar_LdindVarVar_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->indDst = ind->dst;
						fused->indSrc = ind->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 4 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdfldVarVar_i4 && insts[readIdx + 3]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 4]->type == HiOpcodeEnum::BranchVarVar_CneUn_i4 && !IsNoOpTransformInstruction(next) && !IsNoOpTransformInstruction(insts[readIdx + 3]))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRLdfldVarVar_i4* field = (IRLdfldVarVar_i4*)insts[readIdx + 2];
						IRLdlocVarVar* branchCopy = (IRLdlocVarVar*)insts[readIdx + 3];
						IRBranchVarVar_CneUn_i4* branch = (IRBranchVarVar_CneUn_i4*)insts[readIdx + 4];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->fieldOffset = field->offset;
						fused->copyDst3 = branchCopy->dst;
						fused->copySrc3 = branchCopy->src;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offsetBranch = branch->offset;
						relocationOffsets.push_back(&fused->offsetBranch);
						insts[writeIdx++] = fused;
						readIdx += 4;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && insts[readIdx + 3]->type == HiOpcodeEnum::LdcVarConst_4 && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 3];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::GetArrayElementVarVar_size_24 && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRGetArrayElementVarVar_size_24* element = (IRGetArrayElementVarVar_size_24*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->elementDst = element->dst;
						fused->arraySrc = element->arr;
						fused->indexSrc = element->index;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::GetArrayElementVarVar_i4 && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRGetArrayElementVarVar_i4* element = (IRGetArrayElementVarVar_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_i4);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->elementDst = element->dst;
						fused->arraySrc = element->arr;
						fused->indexSrc = element->index;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::GetArrayLengthVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::BranchVarVar_Clt_i4 && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRGetArrayLengthVarVar* length = (IRGetArrayLengthVarVar*)insts[readIdx + 2];
						IRBranchVarVar_Clt_i4* branch = (IRBranchVarVar_Clt_i4*)insts[readIdx + 3];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->lengthDst = length->len;
						fused->arraySrc = length->arr;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offset = branch->offset;
						relocationOffsets.push_back(&fused->offset);
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::GetArrayLengthVarVar && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRGetArrayLengthVarVar* length = (IRGetArrayLengthVarVar*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar);
						fused->copyDst1 = first->dst;
						fused->copySrc1 = first->src;
						fused->copyDst2 = second->dst;
						fused->copySrc2 = second->src;
						fused->lengthDst = length->len;
						fused->arraySrc = length->arr;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::LdindVarVar_i4 && !IsNoOpTransformInstruction(next) && !IsNoOpTransformInstruction(insts[readIdx + 2]))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRLdlocVarVar* third = (IRLdlocVarVar*)insts[readIdx + 2];
						IRLdindVarVar_i4* ind = (IRLdindVarVar_i4*)insts[readIdx + 3];
						if (third->dst >= evalStackBaseOffset
							&& third->dst != first->dst
							&& third->dst != second->dst
							&& third->dst != third->src
							&& ind->dst == third->dst
							&& ind->src == third->dst)
						{
							CreateIR(fused, LdlocVarVar_LdlocVarVar);
							fused->dst1 = first->dst;
							fused->src1 = first->src;
							fused->dst2 = second->dst;
							fused->src2 = second->src;
							ind->src = third->src;
							insts[writeIdx++] = fused;
							insts[writeIdx++] = ind;
							readIdx += 3;
							continue;
						}
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(next) && !IsNoOpTransformInstruction(insts[readIdx + 2]))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						IRLdlocVarVar* third = (IRLdlocVarVar*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdlocVarVar);
						fused->dst1 = first->dst;
						fused->src1 = first->src;
						fused->dst2 = second->dst;
						fused->src2 = second->src;
						fused->dst3 = third->dst;
						fused->src3 = third->src;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(next))
					{
						IRLdlocVarVar* first = (IRLdlocVarVar*)ir;
						IRLdlocVarVar* second = (IRLdlocVarVar*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar);
						fused->dst1 = first->dst;
						fused->src1 = first->src;
						fused->dst2 = second->dst;
						fused->src2 = second->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVarSize && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4)
					{
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)ir;
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)next;
						if (field->obj == copy->dst)
						{
							if (readIdx + 3 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 3]->type == HiOpcodeEnum::BranchVarVar_Cle_i4)
							{
								IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 2];
								IRBranchVarVar_Cle_i4* branch = (IRBranchVarVar_Cle_i4*)insts[readIdx + 3];
								CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4);
								fused->fieldDst = field->dst;
								fused->obj = copy->src;
								fused->offset = field->offset;
								fused->constDst = constant->dst;
								fused->constant = constant->src;
								fused->branchOp1 = branch->op1;
								fused->branchOp2 = branch->op2;
								fused->offsetBranch = branch->offset;
								relocationOffsets.push_back(&fused->offsetBranch);
								insts[writeIdx++] = fused;
								readIdx += 3;
								continue;
							}
							if (readIdx + 3 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::BranchVarVar_CneUn_i4 && !IsNoOpTransformInstruction(insts[readIdx + 2]))
							{
								IRLdlocVarVar* branchCopy = (IRLdlocVarVar*)insts[readIdx + 2];
								IRBranchVarVar_CneUn_i4* branch = (IRBranchVarVar_CneUn_i4*)insts[readIdx + 3];
								CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4);
								fused->fieldDst = field->dst;
								fused->obj = copy->src;
								fused->offset = field->offset;
								fused->copyDst = branchCopy->dst;
								fused->copySrc = branchCopy->src;
								fused->branchOp1 = branch->op1;
								fused->branchOp2 = branch->op2;
								fused->offsetBranch = branch->offset;
								relocationOffsets.push_back(&fused->offsetBranch);
								insts[writeIdx++] = fused;
								readIdx += 3;
								continue;
							}
							if (readIdx + 2 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4)
							{
								IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
								CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4);
								fused->fieldDst = field->dst;
								fused->obj = copy->src;
								fused->offset = field->offset;
								fused->addRet = add->ret;
								fused->addOp1 = add->op1;
								fused->addOp2 = add->op2;
								insts[writeIdx++] = fused;
								readIdx += 2;
								continue;
							}
							if (readIdx + 3 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 3]->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4)
							{
								IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 2];
								IRBinOpVarVarVar_Div_i4* div = (IRBinOpVarVarVar_Div_i4*)insts[readIdx + 3];
								CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4);
								fused->fieldDst = field->dst;
								fused->obj = copy->src;
								fused->offset = field->offset;
								fused->constDst = constant->dst;
								fused->constant = constant->src;
								fused->divRet = div->ret;
								fused->divOp1 = div->op1;
								fused->divOp2 = div->op2;
								insts[writeIdx++] = fused;
								readIdx += 3;
								continue;
							}
							if (readIdx + 2 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::LdcVarConst_4)
							{
								IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 2];
								CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4);
								fused->fieldDst = field->dst;
								fused->obj = copy->src;
								fused->offset = field->offset;
								fused->constDst = constant->dst;
								fused->constant = constant->src;
								insts[writeIdx++] = fused;
								readIdx += 2;
								continue;
							}
							CreateIR(directField, LdfldValueTypeVarVar_i4);
							directField->dst = field->dst;
							directField->obj = copy->src;
							directField->offset = field->offset;
							insts[writeIdx++] = directField;
							readIdx++;
							continue;
						}
					}
					if (ir->type == HiOpcodeEnum::LdfldVarVar_u1 && next->type == HiOpcodeEnum::BranchFalseVar_i4)
					{
						IRLdfldVarVar_u1* field = (IRLdfldVarVar_u1*)ir;
						IRBranchFalseVar_i4* branch = (IRBranchFalseVar_i4*)next;
						CreateIR(fused, LdfldVarVar_u1_BranchFalseVar_i4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->branchOp = branch->op;
						fused->offsetBranch = branch->offset;
						relocationOffsets.push_back(&fused->offsetBranch);
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdfldVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::BranchVarVar_CneUn_i4 && !IsNoOpTransformInstruction(next))
					{
						IRLdfldVarVar_i4* field = (IRLdfldVarVar_i4*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						IRBranchVarVar_CneUn_i4* branch = (IRBranchVarVar_CneUn_i4*)insts[readIdx + 2];
						CreateIR(fused, LdfldVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offsetBranch = branch->offset;
						relocationOffsets.push_back(&fused->offsetBranch);
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::BranchVarVar_CneUn_i4 && !IsNoOpTransformInstruction(next))
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						IRBranchVarVar_CneUn_i4* branch = (IRBranchVarVar_CneUn_i4*)insts[readIdx + 2];
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_BranchVarVar_CneUn_i4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->branchOp1 = branch->op1;
						fused->branchOp2 = branch->op2;
						fused->offsetBranch = branch->offset;
						relocationOffsets.push_back(&fused->offsetBranch);
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::LdcVarConst_4)
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						if (readIdx + 2 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::BranchVarVar_Cle_i4)
						{
							IRBranchVarVar_Cle_i4* branch = (IRBranchVarVar_Cle_i4*)insts[readIdx + 2];
							CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BranchVarVar_Cle_i4);
							fused->fieldDst = field->dst;
							fused->obj = field->obj;
							fused->offset = field->offset;
							fused->constDst = constant->dst;
							fused->constant = constant->src;
							fused->branchOp1 = branch->op1;
							fused->branchOp2 = branch->op2;
							fused->offsetBranch = branch->offset;
							relocationOffsets.push_back(&fused->offsetBranch);
							insts[writeIdx++] = fused;
							readIdx += 2;
							continue;
						}
						if (readIdx + 2 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4)
						{
							IRBinOpVarVarVar_Div_i4* div = (IRBinOpVarVarVar_Div_i4*)insts[readIdx + 2];
							CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4);
							fused->fieldDst = field->dst;
							fused->obj = field->obj;
							fused->offset = field->offset;
							fused->constDst = constant->dst;
							fused->constant = constant->src;
							fused->divRet = div->ret;
							fused->divOp1 = div->op1;
							fused->divOp2 = div->op2;
							insts[writeIdx++] = fused;
							readIdx += 2;
							continue;
						}
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4)
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)next;
						if (readIdx + 3 < insts.size() && insts[readIdx + 2]->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && insts[readIdx + 3]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4)
						{
							IRLdfldValueTypeVarVar_i4* field2 = (IRLdfldValueTypeVarVar_i4*)insts[readIdx + 2];
							IRBinOpVarVarVar_Add_i4* add2 = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + 3];
							CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
							fused->fieldDst1 = field->dst;
							fused->obj1 = field->obj;
							fused->offset1 = field->offset;
							fused->addRet1 = add->ret;
							fused->addOp11 = add->op1;
							fused->addOp21 = add->op2;
							fused->fieldDst2 = field2->dst;
							fused->obj2 = field2->obj;
							fused->offset2 = field2->offset;
							fused->addRet2 = add2->ret;
							fused->addOp12 = add2->op1;
							fused->addOp22 = add2->op2;
							insts[writeIdx++] = fused;
							readIdx += 3;
							continue;
						}
						CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdfldaVarVar && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdindVarVar_i4 && !IsNoOpTransformInstruction(next))
					{
						IRLdfldaVarVar* fieldAddress = (IRLdfldaVarVar*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						IRLdindVarVar_i4* ind = (IRLdindVarVar_i4*)insts[readIdx + 2];
						CreateIR(fused, LdfldaVarVar_LdlocVarVar_LdindVarVar_i4);
						fused->fieldAddressDst = fieldAddress->dst;
						fused->obj = fieldAddress->obj;
						fused->offset = fieldAddress->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->indDst = ind->dst;
						fused->indSrc = ind->src;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldaVarVar && next->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(next))
					{
						IRLdfldaVarVar* fieldAddress = (IRLdfldaVarVar*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						CreateIR(fused, LdfldaVarVar_LdlocVarVar);
						fused->fieldAddressDst = fieldAddress->dst;
						fused->obj = fieldAddress->obj;
						fused->offset = fieldAddress->offset;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && next->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVar && insts[readIdx + 3]->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(next) && !IsNoOpTransformInstruction(insts[readIdx + 2]) && !IsNoOpTransformInstruction(insts[readIdx + 3]))
					{
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)ir;
						IRLdlocVarVar* copy1 = (IRLdlocVarVar*)next;
						IRLdlocVarVar* copy2 = (IRLdlocVarVar*)insts[readIdx + 2];
						IRLdlocVarVar* copy3 = (IRLdlocVarVar*)insts[readIdx + 3];
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar);
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->copyDst1 = copy1->dst;
						fused->copySrc1 = copy1->src;
						fused->copyDst2 = copy2->dst;
						fused->copySrc2 = copy2->src;
						fused->copyDst3 = copy3->dst;
						fused->copySrc3 = copy3->src;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && next->type == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(next))
					{
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)ir;
						IRLdlocVarVar* copy = (IRLdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar);
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4 && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarAddress && insts[readIdx + 3]->type == HiOpcodeEnum::LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)ir;
						IRStfldVarVar_i4* store = (IRStfldVarVar_i4*)next;
						IRLdlocVarAddress* address = (IRLdlocVarAddress*)insts[readIdx + 2];
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)insts[readIdx + 3];
						CreateIR(fused, BinOpVarVarVar_Add_i4_StfldVarVar_i4_LdlocVarAddress_LdcVarConst_4);
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->storeObj = store->obj;
						fused->storeOffset = store->offset;
						fused->storeData = store->data;
						fused->addressDst = address->dst;
						fused->addressSrc = address->src;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4)
					{
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)ir;
						IRStfldVarVar_i4* store = (IRStfldVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_StfldVarVar_i4);
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->storeObj = store->obj;
						fused->storeOffset = store->offset;
						fused->storeData = store->data;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4 && next->type == HiOpcodeEnum::LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdcVarConst_4);
						fused->addRet = add->ret;
						fused->addOp1 = add->op1;
						fused->addOp2 = add->op2;
						fused->constDst = constant->dst;
						fused->constant = constant->src;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4 && next->type == HiOpcodeEnum::MathMinVarVarVar_i4)
					{
						IRBinOpVarVarVar_Div_i4* div = (IRBinOpVarVarVar_Div_i4*)ir;
						IRBinOpVarVarVar_Add_i4* min = (IRBinOpVarVarVar_Add_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4);
						fused->divRet = div->ret;
						fused->divOp1 = div->op1;
						fused->divOp2 = div->op2;
						fused->minRet = min->ret;
						fused->minOp1 = min->op1;
						fused->minOp2 = min->op2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4 && next->type == HiOpcodeEnum::GetArrayElementVarVar_i4)
					{
						IRBinOpVarVarVar_And_i4* andOp = (IRBinOpVarVarVar_And_i4*)ir;
						IRGetArrayElementVarVar_i4* element = (IRGetArrayElementVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4);
						fused->andRet = andOp->ret;
						fused->andOp1 = andOp->op1;
						fused->andOp2 = andOp->op2;
						fused->elementDst = element->dst;
						fused->arraySrc = element->arr;
						fused->indexSrc = element->index;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Rem_i4 && next->type == HiOpcodeEnum::BranchTrueVar_i4)
					{
						IRBinOpVarVarVar_Rem_i4* rem = (IRBinOpVarVarVar_Rem_i4*)ir;
						IRBranchTrueVar_i4* branch = (IRBranchTrueVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Rem_i4_BranchTrueVar_i4);
						fused->remRet = rem->ret;
						fused->remOp1 = rem->op1;
						fused->remOp2 = rem->op2;
						fused->branchOp = branch->op;
						fused->offsetBranch = branch->offset;
						relocationOffsets.push_back(&fused->offsetBranch);
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				{
					insts[writeIdx++] = ir;
				}
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4* fieldConst = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->fieldDst = fieldConst->fieldDst;
						fused->obj = fieldConst->obj;
						fused->offset = fieldConst->offset;
						fused->constDst = fieldConst->constDst;
						fused->constant = fieldConst->constant;
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar)
					{
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* fieldDiv = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->fieldDst = fieldDiv->fieldDst;
						fused->obj = fieldDiv->obj;
						fused->offset = fieldDiv->offset;
						fused->constDst = fieldDiv->constDst;
						fused->constant = fieldDiv->constant;
						fused->divRet = fieldDiv->divRet;
						fused->divOp1 = fieldDiv->divOp1;
						fused->divOp2 = fieldDiv->divOp2;
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRLdlocVarAddress_LdcVarConst_4* addressConst = (IRLdlocVarAddress_LdcVarConst_4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4* fieldConst = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->addressDst = addressConst->addressDst;
						fused->addressSrc = addressConst->addressSrc;
						fused->addressConstDst = addressConst->constDst;
						fused->addressConstant = addressConst->constant;
						fused->fieldDst = fieldConst->fieldDst;
						fused->obj = fieldConst->obj;
						fused->offset = fieldConst->offset;
						fused->fieldConstDst = fieldConst->constDst;
						fused->fieldConstant = fieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4)
					{
						IRBinOpVarVarVar_Add_i4_LdcVarConst_4* addConst = (IRBinOpVarVarVar_Add_i4_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdcVarConst_4_BinOpAnd_i4_GetArrayElementVarVar_i4);
						fused->addRet = addConst->addRet;
						fused->addOp1 = addConst->addOp1;
						fused->addOp2 = addConst->addOp2;
						fused->constDst = addConst->constDst;
						fused->constant = addConst->constant;
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4)
					{
						IRLdlocVarVar_LdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar_LdlocVarVar*)ir;
						IRBinOpVarVarVar_Add_i4_LdcVarConst_4* addConst = (IRBinOpVarVarVar_Add_i4_LdcVarConst_4*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4);
						fused->copyDst1 = copies->dst1;
						fused->copySrc1 = copies->src1;
						fused->copyDst2 = copies->dst2;
						fused->copySrc2 = copies->src2;
						fused->copyDst3 = copies->dst3;
						fused->copySrc3 = copies->src3;
						fused->addRet = addConst->addRet;
						fused->addOp1 = addConst->addOp1;
						fused->addOp2 = addConst->addOp2;
						fused->constDst = addConst->constDst;
						fused->constant = addConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_StindVarVar_i4* subStore = (IRBinOpVarVarVar_Sub_i4_StindVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->tailSubRet = subStore->subRet;
						fused->tailSubOp1 = subStore->subOp1;
						fused->tailSubOp2 = subStore->subOp2;
						fused->storeAddress = subStore->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4* storeFieldConst = (IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->storeObj = storeFieldConst->storeObj;
						fused->storeOffset = storeFieldConst->storeOffset;
						fused->storeData = storeFieldConst->storeData;
						fused->fieldDst = storeFieldConst->fieldDst;
						fused->obj = storeFieldConst->obj;
						fused->offset = storeFieldConst->offset;
						fused->constDst = storeFieldConst->constDst;
						fused->constant = storeFieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* subFieldDiv = (IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->subRet = subFieldDiv->subRet;
						fused->subOp1 = subFieldDiv->subOp1;
						fused->subOp2 = subFieldDiv->subOp2;
						fused->fieldDst = subFieldDiv->fieldDst;
						fused->obj = subFieldDiv->obj;
						fused->offset = subFieldDiv->offset;
						fused->constDst = subFieldDiv->constDst;
						fused->constant = subFieldDiv->constant;
						fused->divRet = subFieldDiv->divRet;
						fused->divOp1 = subFieldDiv->divOp1;
						fused->divOp2 = subFieldDiv->divOp2;
						fused->tailSubRet = subMax->subRet;
						fused->tailSubOp1 = subMax->subOp1;
						fused->tailSubOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* fieldDivAddCopy = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						fused->fieldDst = fieldDivAddCopy->fieldDst;
						fused->obj = fieldDivAddCopy->obj;
						fused->offset = fieldDivAddCopy->offset;
						fused->constDst = fieldDivAddCopy->constDst;
						fused->constant = fieldDivAddCopy->constant;
						fused->divRet = fieldDivAddCopy->divRet;
						fused->divOp1 = fieldDivAddCopy->divOp1;
						fused->divOp2 = fieldDivAddCopy->divOp2;
						fused->addRet = fieldDivAddCopy->addRet;
						fused->addOp1 = fieldDivAddCopy->addOp1;
						fused->addOp2 = fieldDivAddCopy->addOp2;
						fused->copyDst = fieldDivAddCopy->copyDst;
						fused->copySrc = fieldDivAddCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4* divMin = (IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->copyDst = fieldCopyConst->copyDst;
						fused->copySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						fused->divRet = divMin->divRet;
						fused->divOp1 = divMin->divOp1;
						fused->divOp2 = divMin->divOp2;
						fused->minRet = divMin->minRet;
						fused->minOp1 = divMin->minOp1;
						fused->minOp2 = divMin->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4 && next->type == HiOpcodeEnum::RetVar_ret_4)
					{
						IRLdlocVarVar_LdcVarConst_4_BinOpMul_i4* mul = (IRLdlocVarVar_LdcVarConst_4_BinOpMul_i4*)ir;
						IRRetVar_ret_4* ret = (IRRetVar_ret_4*)next;
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpMul_i4_RetVar_ret_4);
						fused->copyDst = mul->copyDst;
						fused->copySrc = mul->copySrc;
						fused->constDst = mul->constDst;
						fused->constant = mul->constant;
						fused->mulRet = mul->mulRet;
						fused->mulOp1 = mul->mulOp1;
						fused->mulOp2 = mul->mulOp2;
						fused->ret = ret->ret;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 3 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4 && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::CompOpVarVarVar_Ceq_i4 && insts[readIdx + 3]->type == HiOpcodeEnum::RetVar_ret_1)
					{
						IRLdlocVarVar_LdcVarConst_4_BinOpRem_i4* rem = (IRLdlocVarVar_LdcVarConst_4_BinOpRem_i4*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRCompOpVarVarVar_Ceq_i4* compare = (IRCompOpVarVarVar_Ceq_i4*)insts[readIdx + 2];
						IRRetVar_ret_1* ret = (IRRetVar_ret_1*)insts[readIdx + 3];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpRem_i4_LdcVarConst_4_CompOpCeq_i4_RetVar_ret_1);
						fused->copyDst = rem->copyDst;
						fused->copySrc = rem->copySrc;
						fused->remConstDst = rem->constDst;
						fused->remConstant = rem->constant;
						fused->remRet = rem->remRet;
						fused->remOp1 = rem->remOp1;
						fused->remOp2 = rem->remOp2;
						fused->compareConstDst = constant->dst;
						fused->compareConstant = constant->src;
						fused->compareRet = compare->ret;
						fused->compareOp1 = compare->c1;
						fused->compareOp2 = compare->c2;
						fused->ret = ret->ret;
						insts[writeIdx++] = fused;
						readIdx += 3;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpRem_i4 && next->type == HiOpcodeEnum::LdcVarConst_4 && insts[readIdx + 2]->type == HiOpcodeEnum::CompOpVarVarVar_Ceq_i4)
					{
						IRLdlocVarVar_LdcVarConst_4_BinOpRem_i4* rem = (IRLdlocVarVar_LdcVarConst_4_BinOpRem_i4*)ir;
						IRLdcVarConst_4* constant = (IRLdcVarConst_4*)next;
						IRCompOpVarVarVar_Ceq_i4* compare = (IRCompOpVarVarVar_Ceq_i4*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpRem_i4_LdcVarConst_4_CompOpCeq_i4);
						fused->copyDst = rem->copyDst;
						fused->copySrc = rem->copySrc;
						fused->remConstDst = rem->constDst;
						fused->remConstant = rem->constant;
						fused->remRet = rem->remRet;
						fused->remOp1 = rem->remOp1;
						fused->remOp2 = rem->remOp2;
						fused->compareConstDst = constant->dst;
						fused->compareConstant = constant->src;
						fused->compareRet = compare->ret;
						fused->compareOp1 = compare->c1;
						fused->compareOp2 = compare->c2;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVarSize)
					{
						IRLdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar*)ir;
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdlocVarVarSize);
						fused->copyDst1 = copies->dst1;
						fused->copySrc1 = copies->src1;
						fused->copyDst2 = copies->dst2;
						fused->copySrc2 = copies->src2;
						fused->sizedCopyDst = copy->dst;
						fused->sizedCopySrc = copy->src;
						fused->sizedCopySize = copy->size;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::GetArrayElementVarVar_size_28 && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVarSize)
					{
						IRLdlocVarVar_LdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar_LdlocVarVar*)ir;
						IRGetArrayElementVarVar_size_28* element = (IRGetArrayElementVarVar_size_28*)next;
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize);
						fused->copyDst1 = copies->dst1;
						fused->copySrc1 = copies->src1;
						fused->copyDst2 = copies->dst2;
						fused->copySrc2 = copies->src2;
						fused->copyDst3 = copies->dst3;
						fused->copySrc3 = copies->src3;
						fused->elementDst = element->dst;
						fused->arraySrc = element->arr;
						fused->indexSrc = element->index;
						fused->sizedCopyDst = copy->dst;
						fused->sizedCopySrc = copy->src;
						fused->sizedCopySize = copy->size;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24 && next->type == HiOpcodeEnum::LdlocVarVarSize)
					{
						IRLdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24* element = (IRLdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24*)ir;
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_24_LdlocVarVarSize);
						fused->copyDst1 = element->copyDst1;
						fused->copySrc1 = element->copySrc1;
						fused->copyDst2 = element->copyDst2;
						fused->copySrc2 = element->copySrc2;
						fused->elementDst = element->elementDst;
						fused->arraySrc = element->arraySrc;
						fused->indexSrc = element->indexSrc;
						fused->sizedCopyDst = copy->dst;
						fused->sizedCopySrc = copy->src;
						fused->sizedCopySize = copy->size;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::GetArrayElementVarVar_size_28 && insts[readIdx + 2]->type == HiOpcodeEnum::LdlocVarVarSize)
					{
						IRLdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar*)ir;
						IRGetArrayElementVarVar_size_28* element = (IRGetArrayElementVarVar_size_28*)next;
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)insts[readIdx + 2];
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize);
						fused->copyDst1 = copies->dst1;
						fused->copySrc1 = copies->src1;
						fused->copyDst2 = copies->dst2;
						fused->copySrc2 = copies->src2;
						fused->elementDst = element->dst;
						fused->arraySrc = element->arr;
						fused->indexSrc = element->index;
						fused->sizedCopyDst = copy->dst;
						fused->sizedCopySrc = copy->src;
						fused->sizedCopySize = copy->size;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVarSize)
					{
						IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar* addCopies = (IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar*)ir;
						IRLdlocVarVarSize* copy = (IRLdlocVarVarSize*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize);
						fused->addRet = addCopies->addRet;
						fused->addOp1 = addCopies->addOp1;
						fused->addOp2 = addCopies->addOp2;
						fused->copyDst1 = addCopies->copyDst1;
						fused->copySrc1 = addCopies->copySrc1;
						fused->copyDst2 = addCopies->copyDst2;
						fused->copySrc2 = addCopies->copySrc2;
						fused->copyDst3 = addCopies->copyDst3;
						fused->copySrc3 = addCopies->copySrc3;
						fused->sizedCopyDst = copy->dst;
						fused->sizedCopySrc = copy->src;
						fused->sizedCopySize = copy->size;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_StindVarVar_i4* subStore = (IRBinOpVarVarVar_Sub_i4_StindVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->tailSubRet = subStore->subRet;
						fused->tailSubOp1 = subStore->subOp1;
						fused->tailSubOp2 = subStore->subOp2;
						fused->storeAddress = subStore->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4* storeFieldConst = (IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->storeObj = storeFieldConst->storeObj;
						fused->storeOffset = storeFieldConst->storeOffset;
						fused->storeData = storeFieldConst->storeData;
						fused->fieldDst = storeFieldConst->fieldDst;
						fused->obj = storeFieldConst->obj;
						fused->offset = storeFieldConst->offset;
						fused->constDst = storeFieldConst->constDst;
						fused->constant = storeFieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* subFieldDiv = (IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->subRet = subFieldDiv->subRet;
						fused->subOp1 = subFieldDiv->subOp1;
						fused->subOp2 = subFieldDiv->subOp2;
						fused->fieldDst = subFieldDiv->fieldDst;
						fused->obj = subFieldDiv->obj;
						fused->offset = subFieldDiv->offset;
						fused->constDst = subFieldDiv->constDst;
						fused->constant = subFieldDiv->constant;
						fused->divRet = subFieldDiv->divRet;
						fused->divOp1 = subFieldDiv->divOp1;
						fused->divOp2 = subFieldDiv->divOp2;
						fused->tailSubRet = subMax->subRet;
						fused->tailSubOp1 = subMax->subOp1;
						fused->tailSubOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* fieldDivAddCopy = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						fused->fieldDst = fieldDivAddCopy->fieldDst;
						fused->obj = fieldDivAddCopy->obj;
						fused->offset = fieldDivAddCopy->offset;
						fused->constDst = fieldDivAddCopy->constDst;
						fused->constant = fieldDivAddCopy->constant;
						fused->divRet = fieldDivAddCopy->divRet;
						fused->divOp1 = fieldDivAddCopy->divOp1;
						fused->divOp2 = fieldDivAddCopy->divOp2;
						fused->addRet = fieldDivAddCopy->addRet;
						fused->addOp1 = fieldDivAddCopy->addOp1;
						fused->addOp2 = fieldDivAddCopy->addOp2;
						fused->copyDst = fieldDivAddCopy->copyDst;
						fused->copySrc = fieldDivAddCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4* divMin = (IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->copyDst = fieldCopyConst->copyDst;
						fused->copySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						fused->divRet = divMin->divRet;
						fused->divOp1 = divMin->divOp1;
						fused->divOp2 = divMin->divOp2;
						fused->minRet = divMin->minRet;
						fused->minOp1 = divMin->minOp1;
						fused->minOp2 = divMin->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->fieldDst0 = fieldAdd->fieldDst0;
						fused->obj0 = fieldAdd->obj0;
						fused->offset0 = fieldAdd->offset0;
						fused->fieldDst1 = fieldAdd->fieldDst1;
						fused->obj1 = fieldAdd->obj1;
						fused->offset1 = fieldAdd->offset1;
						fused->addRet1 = fieldAdd->addRet1;
						fused->addOp11 = fieldAdd->addOp11;
						fused->addOp21 = fieldAdd->addOp21;
						fused->fieldDst2 = fieldAdd->fieldDst2;
						fused->obj2 = fieldAdd->obj2;
						fused->offset2 = fieldAdd->offset2;
						fused->addRet2 = fieldAdd->addRet2;
						fused->addOp12 = fieldAdd->addOp12;
						fused->addOp22 = fieldAdd->addOp22;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4);
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->fieldCopyDst = fieldCopyConst->copyDst;
						fused->fieldCopySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_StindVarVar_i4* subStore = (IRBinOpVarVarVar_Sub_i4_StindVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->tailSubRet = subStore->subRet;
						fused->tailSubOp1 = subStore->subOp1;
						fused->tailSubOp2 = subStore->subOp2;
						fused->storeAddress = subStore->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4* storeFieldConst = (IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->storeObj = storeFieldConst->storeObj;
						fused->storeOffset = storeFieldConst->storeOffset;
						fused->storeData = storeFieldConst->storeData;
						fused->fieldDst = storeFieldConst->fieldDst;
						fused->obj = storeFieldConst->obj;
						fused->offset = storeFieldConst->offset;
						fused->constDst = storeFieldConst->constDst;
						fused->constant = storeFieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* subFieldDiv = (IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->subRet = subFieldDiv->subRet;
						fused->subOp1 = subFieldDiv->subOp1;
						fused->subOp2 = subFieldDiv->subOp2;
						fused->fieldDst = subFieldDiv->fieldDst;
						fused->obj = subFieldDiv->obj;
						fused->offset = subFieldDiv->offset;
						fused->constDst = subFieldDiv->constDst;
						fused->constant = subFieldDiv->constant;
						fused->divRet = subFieldDiv->divRet;
						fused->divOp1 = subFieldDiv->divOp1;
						fused->divOp2 = subFieldDiv->divOp2;
						fused->tailSubRet = subMax->subRet;
						fused->tailSubOp1 = subMax->subOp1;
						fused->tailSubOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* fieldDivAddCopy = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						fused->fieldDst = fieldDivAddCopy->fieldDst;
						fused->obj = fieldDivAddCopy->obj;
						fused->offset = fieldDivAddCopy->offset;
						fused->constDst = fieldDivAddCopy->constDst;
						fused->constant = fieldDivAddCopy->constant;
						fused->divRet = fieldDivAddCopy->divRet;
						fused->divOp1 = fieldDivAddCopy->divOp1;
						fused->divOp2 = fieldDivAddCopy->divOp2;
						fused->addRet = fieldDivAddCopy->addRet;
						fused->addOp1 = fieldDivAddCopy->addOp1;
						fused->addOp2 = fieldDivAddCopy->addOp2;
						fused->copyDst = fieldDivAddCopy->copyDst;
						fused->copySrc = fieldDivAddCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4* divMin = (IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->copyDst = fieldCopyConst->copyDst;
						fused->copySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						fused->divRet = divMin->divRet;
						fused->divOp1 = divMin->divOp1;
						fused->divOp2 = divMin->divOp2;
						fused->minRet = divMin->minRet;
						fused->minOp1 = divMin->minOp1;
						fused->minOp2 = divMin->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_StindVarVar_i4* subStore = (IRBinOpVarVarVar_Sub_i4_StindVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->tailSubRet = subStore->subRet;
						fused->tailSubOp1 = subStore->subOp1;
						fused->tailSubOp2 = subStore->subOp2;
						fused->storeAddress = subStore->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4* storeFieldConst = (IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->storeObj = storeFieldConst->storeObj;
						fused->storeOffset = storeFieldConst->storeOffset;
						fused->storeData = storeFieldConst->storeData;
						fused->fieldDst = storeFieldConst->fieldDst;
						fused->obj = storeFieldConst->obj;
						fused->offset = storeFieldConst->offset;
						fused->constDst = storeFieldConst->constDst;
						fused->constant = storeFieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* subFieldDiv = (IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->subRet = subFieldDiv->subRet;
						fused->subOp1 = subFieldDiv->subOp1;
						fused->subOp2 = subFieldDiv->subOp2;
						fused->fieldDst = subFieldDiv->fieldDst;
						fused->obj = subFieldDiv->obj;
						fused->offset = subFieldDiv->offset;
						fused->constDst = subFieldDiv->constDst;
						fused->constant = subFieldDiv->constant;
						fused->divRet = subFieldDiv->divRet;
						fused->divOp1 = subFieldDiv->divOp1;
						fused->divOp2 = subFieldDiv->divOp2;
						fused->tailSubRet = subMax->subRet;
						fused->tailSubOp1 = subMax->subOp1;
						fused->tailSubOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* fieldDivAddCopy = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						fused->fieldDst = fieldDivAddCopy->fieldDst;
						fused->obj = fieldDivAddCopy->obj;
						fused->offset = fieldDivAddCopy->offset;
						fused->constDst = fieldDivAddCopy->constDst;
						fused->constant = fieldDivAddCopy->constant;
						fused->divRet = fieldDivAddCopy->divRet;
						fused->divOp1 = fieldDivAddCopy->divOp1;
						fused->divOp2 = fieldDivAddCopy->divOp2;
						fused->addRet = fieldDivAddCopy->addRet;
						fused->addOp1 = fieldDivAddCopy->addOp1;
						fused->addOp2 = fieldDivAddCopy->addOp2;
						fused->copyDst = fieldDivAddCopy->copyDst;
						fused->copySrc = fieldDivAddCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4* divMin = (IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->copyDst = fieldCopyConst->copyDst;
						fused->copySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						fused->divRet = divMin->divRet;
						fused->divOp1 = divMin->divOp1;
						fused->divOp2 = divMin->divOp2;
						fused->minRet = divMin->minRet;
						fused->minOp1 = divMin->minOp1;
						fused->minOp2 = divMin->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)ir;
						IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize* addCopies = (IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize);
						fused->fieldDst = fieldAdd->fieldDst;
						fused->obj = fieldAdd->obj;
						fused->offset = fieldAdd->offset;
						fused->fieldAddRet = fieldAdd->addRet;
						fused->fieldAddOp1 = fieldAdd->addOp1;
						fused->fieldAddOp2 = fieldAdd->addOp2;
						fused->addRet = addCopies->addRet;
						fused->addOp1 = addCopies->addOp1;
						fused->addOp2 = addCopies->addOp2;
						fused->copyDst1 = addCopies->copyDst1;
						fused->copySrc1 = addCopies->copySrc1;
						fused->copyDst2 = addCopies->copyDst2;
						fused->copySrc2 = addCopies->copySrc2;
						fused->copyDst3 = addCopies->copyDst3;
						fused->copySrc3 = addCopies->copySrc3;
						fused->sizedCopyDst = addCopies->sizedCopyDst;
						fused->sizedCopySrc = addCopies->sizedCopySrc;
						fused->sizedCopySize = addCopies->sizedCopySize;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* leadingCopy = (IRLdlocVarVar*)ir;
						IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4* addressLoad = (IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4);
						fused->leadingCopyDst = leadingCopy->dst;
						fused->leadingCopySrc = leadingCopy->src;
						fused->addressDst = addressLoad->addressDst;
						fused->addressSrc = addressLoad->addressSrc;
						fused->fieldAddressDst = addressLoad->fieldAddressDst;
						fused->obj = addressLoad->obj;
						fused->offset = addressLoad->offset;
						fused->copyDst = addressLoad->copyDst;
						fused->copySrc = addressLoad->copySrc;
						fused->indDst = addressLoad->indDst;
						fused->indSrc = addressLoad->indSrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4 && next->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar)
					{
						IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4* addressLoad = (IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4*)ir;
						IRLdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar*)next;
						CreateIR(fused, LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar);
						fused->addressDst = addressLoad->addressDst;
						fused->addressSrc = addressLoad->addressSrc;
						fused->fieldAddressDst = addressLoad->fieldAddressDst;
						fused->obj = addressLoad->obj;
						fused->offset = addressLoad->offset;
						fused->copyDst = addressLoad->copyDst;
						fused->copySrc = addressLoad->copySrc;
						fused->indDst = addressLoad->indDst;
						fused->indSrc = addressLoad->indSrc;
						fused->constDst = addressLoad->constDst;
						fused->constant = addressLoad->constant;
						fused->tailCopyDst1 = copies->dst1;
						fused->tailCopySrc1 = copies->src1;
						fused->tailCopyDst2 = copies->dst2;
						fused->tailCopySrc2 = copies->src2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->fieldDst0 = fieldAdd->fieldDst0;
						fused->obj0 = fieldAdd->obj0;
						fused->offset0 = fieldAdd->offset0;
						fused->fieldDst1 = fieldAdd->fieldDst1;
						fused->obj1 = fieldAdd->obj1;
						fused->offset1 = fieldAdd->offset1;
						fused->addRet1 = fieldAdd->addRet1;
						fused->addOp11 = fieldAdd->addOp11;
						fused->addOp21 = fieldAdd->addOp21;
						fused->fieldDst2 = fieldAdd->fieldDst2;
						fused->obj2 = fieldAdd->obj2;
						fused->offset2 = fieldAdd->offset2;
						fused->addRet2 = fieldAdd->addRet2;
						fused->addOp12 = fieldAdd->addOp12;
						fused->addOp22 = fieldAdd->addOp22;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::StfldVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRStfldVarVar_i4* store = (IRStfldVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4* fieldConst = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->storeObj = store->obj;
						fused->storeOffset = store->offset;
						fused->storeData = store->data;
						fused->fieldDst = fieldConst->fieldDst;
						fused->obj = fieldConst->obj;
						fused->offset = fieldConst->offset;
						fused->constDst = fieldConst->constDst;
						fused->constant = fieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4);
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->fieldCopyDst = fieldCopyConst->copyDst;
						fused->fieldCopySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4)
					{
						IRBinOpVarVarVar_Sub_i4* sub = (IRBinOpVarVarVar_Sub_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* fieldDiv = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4);
						fused->subRet = sub->ret;
						fused->subOp1 = sub->op1;
						fused->subOp2 = sub->op2;
						fused->fieldDst = fieldDiv->fieldDst;
						fused->obj = fieldDiv->obj;
						fused->offset = fieldDiv->offset;
						fused->constDst = fieldDiv->constDst;
						fused->constant = fieldDiv->constant;
						fused->divRet = fieldDiv->divRet;
						fused->divOp1 = fieldDiv->divOp1;
						fused->divOp2 = fieldDiv->divOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4)
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRLdlocVarVar_LdcVarConst_4* copyConst = (IRLdlocVarVar_LdcVarConst_4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4);
						fused->fieldDst = field->dst;
						fused->obj = field->obj;
						fused->offset = field->offset;
						fused->copyDst = copyConst->copyDst;
						fused->copySrc = copyConst->copySrc;
						fused->constDst = copyConst->constDst;
						fused->constant = copyConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (readIdx + 2 < insts.size() && ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4 && insts[readIdx + 2]->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4)
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* first = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* second = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)insts[readIdx + 2];
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->fieldDst0 = field->dst;
						fused->obj0 = field->obj;
						fused->offset0 = field->offset;
						fused->fieldDst1 = first->fieldDst;
						fused->obj1 = first->obj;
						fused->offset1 = first->offset;
						fused->addRet1 = first->addRet;
						fused->addOp11 = first->addOp1;
						fused->addOp21 = first->addOp2;
						fused->fieldDst2 = second->fieldDst;
						fused->obj2 = second->obj;
						fused->offset2 = second->offset;
						fused->addRet2 = second->addRet;
						fused->addOp12 = second->addOp1;
						fused->addOp22 = second->addOp2;
						insts[writeIdx++] = fused;
						readIdx += 2;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4)
					{
						IRLdfldValueTypeVarVar_i4* field = (IRLdfldValueTypeVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* doubleAdd = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->fieldDst0 = field->dst;
						fused->obj0 = field->obj;
						fused->offset0 = field->offset;
						fused->fieldDst1 = doubleAdd->fieldDst1;
						fused->obj1 = doubleAdd->obj1;
						fused->offset1 = doubleAdd->offset1;
						fused->addRet1 = doubleAdd->addRet1;
						fused->addOp11 = doubleAdd->addOp11;
						fused->addOp21 = doubleAdd->addOp21;
						fused->fieldDst2 = doubleAdd->fieldDst2;
						fused->obj2 = doubleAdd->obj2;
						fused->offset2 = doubleAdd->offset2;
						fused->addRet2 = doubleAdd->addRet2;
						fused->addOp12 = doubleAdd->addOp12;
						fused->addOp22 = doubleAdd->addOp22;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4)
					{
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* first = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)ir;
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* second = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->fieldDst1 = first->fieldDst;
						fused->obj1 = first->obj;
						fused->offset1 = first->offset;
						fused->addRet1 = first->addRet;
						fused->addOp11 = first->addOp1;
						fused->addOp21 = first->addOp2;
						fused->fieldDst2 = second->fieldDst;
						fused->obj2 = second->obj;
						fused->offset2 = second->offset;
						fused->addRet2 = second->addRet;
						fused->addOp12 = second->addOp1;
						fused->addOp22 = second->addOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->fieldDst0 = fieldAdd->fieldDst0;
						fused->obj0 = fieldAdd->obj0;
						fused->offset0 = fieldAdd->offset0;
						fused->fieldDst1 = fieldAdd->fieldDst1;
						fused->obj1 = fieldAdd->obj1;
						fused->offset1 = fieldAdd->offset1;
						fused->addRet1 = fieldAdd->addRet1;
						fused->addOp11 = fieldAdd->addOp11;
						fused->addOp21 = fieldAdd->addOp21;
						fused->fieldDst2 = fieldAdd->fieldDst2;
						fused->obj2 = fieldAdd->obj2;
						fused->offset2 = fieldAdd->offset2;
						fused->addRet2 = fieldAdd->addRet2;
						fused->addOp12 = fieldAdd->addOp12;
						fused->addOp22 = fieldAdd->addOp22;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4);
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->fieldCopyDst = fieldCopyConst->copyDst;
						fused->fieldCopySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4* fieldConst = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpSub_i4_MathMaxVarVarVar_i4);
						fused->fieldDst = fieldConst->fieldDst;
						fused->obj = fieldConst->obj;
						fused->offset = fieldConst->offset;
						fused->constDst = fieldConst->constDst;
						fused->constant = fieldConst->constant;
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar)
					{
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* fieldDiv = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->fieldDst = fieldDiv->fieldDst;
						fused->obj = fieldDiv->obj;
						fused->offset = fieldDiv->offset;
						fused->constDst = fieldDiv->constDst;
						fused->constant = fieldDiv->constant;
						fused->divRet = fieldDiv->divRet;
						fused->divOp1 = fieldDiv->divOp1;
						fused->divOp2 = fieldDiv->divOp2;
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarAddress_LdcVarConst_4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRLdlocVarAddress_LdcVarConst_4* addressConst = (IRLdlocVarAddress_LdcVarConst_4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4* fieldConst = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, LdlocVarAddress_LdcVarConst_4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->addressDst = addressConst->addressDst;
						fused->addressSrc = addressConst->addressSrc;
						fused->addressConstDst = addressConst->constDst;
						fused->addressConstant = addressConst->constant;
						fused->fieldDst = fieldConst->fieldDst;
						fused->obj = fieldConst->obj;
						fused->offset = fieldConst->offset;
						fused->fieldConstDst = fieldConst->constDst;
						fused->fieldConstant = fieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4)
					{
						IRBinOpVarVarVar_Add_i4_LdcVarConst_4* addConst = (IRBinOpVarVarVar_Add_i4_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdcVarConst_4_BinOpAnd_i4_GetArrayElementVarVar_i4);
						fused->addRet = addConst->addRet;
						fused->addOp1 = addConst->addOp1;
						fused->addOp2 = addConst->addOp2;
						fused->constDst = addConst->constDst;
						fused->constant = addConst->constant;
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdcVarConst_4)
					{
						IRLdlocVarVar_LdlocVarVar_LdlocVarVar* copies = (IRLdlocVarVar_LdlocVarVar_LdlocVarVar*)ir;
						IRBinOpVarVarVar_Add_i4_LdcVarConst_4* addConst = (IRBinOpVarVarVar_Add_i4_LdcVarConst_4*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4);
						fused->copyDst1 = copies->dst1;
						fused->copySrc1 = copies->src1;
						fused->copyDst2 = copies->dst2;
						fused->copySrc2 = copies->src2;
						fused->copyDst3 = copies->dst3;
						fused->copySrc3 = copies->src3;
						fused->addRet = addConst->addRet;
						fused->addOp1 = addConst->addOp1;
						fused->addOp2 = addConst->addOp2;
						fused->constDst = addConst->constDst;
						fused->constant = addConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4*)ir;
						IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize* addCopies = (IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize);
						fused->fieldDst = fieldAdd->fieldDst;
						fused->obj = fieldAdd->obj;
						fused->offset = fieldAdd->offset;
						fused->fieldAddRet = fieldAdd->addRet;
						fused->fieldAddOp1 = fieldAdd->addOp1;
						fused->fieldAddOp2 = fieldAdd->addOp2;
						fused->addRet = addCopies->addRet;
						fused->addOp1 = addCopies->addOp1;
						fused->addOp2 = addCopies->addOp2;
						fused->copyDst1 = addCopies->copyDst1;
						fused->copySrc1 = addCopies->copySrc1;
						fused->copyDst2 = addCopies->copyDst2;
						fused->copySrc2 = addCopies->copySrc2;
						fused->copyDst3 = addCopies->copyDst3;
						fused->copySrc3 = addCopies->copySrc3;
						fused->sizedCopyDst = addCopies->sizedCopyDst;
						fused->sizedCopySrc = addCopies->sizedCopySrc;
						fused->sizedCopySize = addCopies->sizedCopySize;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4 && !IsNoOpTransformInstruction(ir))
					{
						IRLdlocVarVar* copy = (IRLdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* fieldAdd = (IRLdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4);
						fused->copyDst = copy->dst;
						fused->copySrc = copy->src;
						fused->fieldDst0 = fieldAdd->fieldDst0;
						fused->obj0 = fieldAdd->obj0;
						fused->offset0 = fieldAdd->offset0;
						fused->fieldDst1 = fieldAdd->fieldDst1;
						fused->obj1 = fieldAdd->obj1;
						fused->offset1 = fieldAdd->offset1;
						fused->addRet1 = fieldAdd->addRet1;
						fused->addOp11 = fieldAdd->addOp11;
						fused->addOp21 = fieldAdd->addOp21;
						fused->fieldDst2 = fieldAdd->fieldDst2;
						fused->obj2 = fieldAdd->obj2;
						fused->offset2 = fieldAdd->offset2;
						fused->addRet2 = fieldAdd->addRet2;
						fused->addOp12 = fieldAdd->addOp12;
						fused->addOp22 = fieldAdd->addOp22;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Add_i4_LdlocVarVar* addCopy = (IRBinOpVarVarVar_Add_i4_LdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Add_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4);
						fused->addRet = addCopy->addRet;
						fused->addOp1 = addCopy->addOp1;
						fused->addOp2 = addCopy->addOp2;
						fused->copyDst = addCopy->copyDst;
						fused->copySrc = addCopy->copySrc;
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->fieldCopyDst = fieldCopyConst->copyDst;
						fused->fieldCopySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4 && next->type == HiOpcodeEnum::StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4)
					{
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4* subMax = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4*)ir;
						IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4* storeFieldConst = (IRStfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_StfldVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4);
						fused->subRet = subMax->subRet;
						fused->subOp1 = subMax->subOp1;
						fused->subOp2 = subMax->subOp2;
						fused->maxRet = subMax->maxRet;
						fused->maxOp1 = subMax->maxOp1;
						fused->maxOp2 = subMax->maxOp2;
						fused->storeObj = storeFieldConst->storeObj;
						fused->storeOffset = storeFieldConst->storeOffset;
						fused->storeData = storeFieldConst->storeData;
						fused->fieldDst = storeFieldConst->fieldDst;
						fused->obj = storeFieldConst->obj;
						fused->offset = storeFieldConst->offset;
						fused->constDst = storeFieldConst->constDst;
						fused->constant = storeFieldConst->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4* andElement = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4*)ir;
						IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* fieldDivAddCopy = (IRLdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar);
						fused->andRet = andElement->andRet;
						fused->andOp1 = andElement->andOp1;
						fused->andOp2 = andElement->andOp2;
						fused->elementDst = andElement->elementDst;
						fused->arraySrc = andElement->arraySrc;
						fused->indexSrc = andElement->indexSrc;
						fused->fieldDst = fieldDivAddCopy->fieldDst;
						fused->obj = fieldDivAddCopy->obj;
						fused->offset = fieldDivAddCopy->offset;
						fused->constDst = fieldDivAddCopy->constDst;
						fused->constant = fieldDivAddCopy->constant;
						fused->divRet = fieldDivAddCopy->divRet;
						fused->divOp1 = fieldDivAddCopy->divOp1;
						fused->divOp2 = fieldDivAddCopy->divOp2;
						fused->addRet = fieldDivAddCopy->addRet;
						fused->addOp1 = fieldDivAddCopy->addOp1;
						fused->addOp2 = fieldDivAddCopy->addOp2;
						fused->copyDst = fieldDivAddCopy->copyDst;
						fused->copySrc = fieldDivAddCopy->copySrc;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Div_i4_MathMinVarVarVar_i4)
					{
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4* fieldCopyConst = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4*)ir;
						IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4* divMin = (IRBinOpVarVarVar_Div_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->fieldDst = fieldCopyConst->fieldDst;
						fused->obj = fieldCopyConst->obj;
						fused->offset = fieldCopyConst->offset;
						fused->copyDst = fieldCopyConst->copyDst;
						fused->copySrc = fieldCopyConst->copySrc;
						fused->constDst = fieldCopyConst->constDst;
						fused->constant = fieldCopyConst->constant;
						fused->divRet = divMin->divRet;
						fused->divOp1 = divMin->divOp1;
						fused->divOp2 = divMin->divOp2;
						fused->minRet = divMin->minRet;
						fused->minOp1 = divMin->minOp1;
						fused->minOp2 = divMin->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 3 < insts.size()
					&& ir->type == HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4
					&& insts[readIdx + 1]->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4
					&& insts[readIdx + 2]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize
					&& insts[readIdx + 3]->type == HiOpcodeEnum::SetArrayElementVarVar_size_28)
				{
					IRLdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* head = (IRLdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)ir;
					IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* mid = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)insts[readIdx + 1];
					IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize* tail = (IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize*)insts[readIdx + 2];
					IRSetArrayElementVarVar_size_12* store = (IRSetArrayElementVarVar_size_12*)insts[readIdx + 3];
					CreateIR(fused, Hotc233FieldAddPair_SetArrayElement_size_28);
					fused->copyDst = head->copyDst;
					fused->copySrc = head->copySrc;
					fused->fieldDst0 = head->fieldDst0;
					fused->obj0 = head->obj0;
					fused->offset0 = head->offset0;
					fused->fieldDst1 = head->fieldDst1;
					fused->obj1 = head->obj1;
					fused->offset1 = head->offset1;
					fused->addRet1 = head->addRet1;
					fused->addOp11 = head->addOp11;
					fused->addOp21 = head->addOp21;
					fused->fieldDst2 = head->fieldDst2;
					fused->obj2 = head->obj2;
					fused->offset2 = head->offset2;
					fused->addRet2 = head->addRet2;
					fused->addOp12 = head->addOp12;
					fused->addOp22 = head->addOp22;
					fused->midFieldDst1 = mid->fieldDst1;
					fused->midObj1 = mid->obj1;
					fused->midOffset1 = mid->offset1;
					fused->midAddRet1 = mid->addRet1;
					fused->midAddOp11 = mid->addOp11;
					fused->midAddOp21 = mid->addOp21;
					fused->midFieldDst2 = mid->fieldDst2;
					fused->midObj2 = mid->obj2;
					fused->midOffset2 = mid->offset2;
					fused->midAddRet2 = mid->addRet2;
					fused->midAddOp12 = mid->addOp12;
					fused->midAddOp22 = mid->addOp22;
					fused->tailAddRet = tail->addRet;
					fused->tailAddOp1 = tail->addOp1;
					fused->tailAddOp2 = tail->addOp2;
					fused->tailCopyDst1 = tail->copyDst1;
					fused->tailCopySrc1 = tail->copySrc1;
					fused->tailCopyDst2 = tail->copyDst2;
					fused->tailCopySrc2 = tail->copySrc2;
					fused->tailCopyDst3 = tail->copyDst3;
					fused->tailCopySrc3 = tail->copySrc3;
					fused->tailSizedCopyDst = tail->sizedCopyDst;
					fused->tailSizedCopySrc = tail->sizedCopySrc;
					fused->tailSizedCopySize = tail->sizedCopySize;
					fused->arraySrc = store->arr;
					fused->indexSrc = store->index;
					fused->elementSrc = store->ele;
					insts[writeIdx++] = fused;
					readIdx += 3;
					continue;
				}
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4 && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize)
					{
						IRLdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4* head = (IRLdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4*)ir;
						IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize* tail = (IRLdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize*)next;
						CreateIR(fused, LdlocVarVar_LdfldValueTypeVarVar_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_LdfldValueTypeVarVar_i4_BinOpAdd_i4_BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_LdlocVarVarSize);
						fused->copyDst = head->copyDst;
						fused->copySrc = head->copySrc;
						fused->fieldDst0 = head->fieldDst0;
						fused->obj0 = head->obj0;
						fused->offset0 = head->offset0;
						fused->fieldDst1 = head->fieldDst1;
						fused->obj1 = head->obj1;
						fused->offset1 = head->offset1;
						fused->addRet1 = head->addRet1;
						fused->addOp11 = head->addOp11;
						fused->addOp21 = head->addOp21;
						fused->fieldDst2 = head->fieldDst2;
						fused->obj2 = head->obj2;
						fused->offset2 = head->offset2;
						fused->addRet2 = head->addRet2;
						fused->addOp12 = head->addOp12;
						fused->addOp22 = head->addOp22;
						fused->tailFieldDst = tail->fieldDst;
						fused->tailObj = tail->obj;
						fused->tailOffset = tail->offset;
						fused->tailFieldAddRet = tail->fieldAddRet;
						fused->tailFieldAddOp1 = tail->fieldAddOp1;
						fused->tailFieldAddOp2 = tail->fieldAddOp2;
						fused->tailAddRet = tail->addRet;
						fused->tailAddOp1 = tail->addOp1;
						fused->tailAddOp2 = tail->addOp2;
						fused->tailCopyDst1 = tail->copyDst1;
						fused->tailCopySrc1 = tail->copySrc1;
						fused->tailCopyDst2 = tail->copyDst2;
						fused->tailCopySrc2 = tail->copySrc2;
						fused->tailCopyDst3 = tail->copyDst3;
						fused->tailCopySrc3 = tail->copySrc3;
						fused->tailSizedCopyDst = tail->sizedCopyDst;
						fused->tailSizedCopySrc = tail->sizedCopySrc;
						fused->tailSizedCopySize = tail->sizedCopySize;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize && next->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4)
					{
						IRLdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize* element = (IRLdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize*)ir;
						IRLdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4* tail = (IRLdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarVar_GetArrayElementVarVar_size_28_LdlocVarVarSize_LdlocVarVar_LdlocVarVar_LdlocVarVar_BinOpAdd_i4_LdcVarConst_4);
						fused->elementCopyDst1 = element->copyDst1;
						fused->elementCopySrc1 = element->copySrc1;
						fused->elementCopyDst2 = element->copyDst2;
						fused->elementCopySrc2 = element->copySrc2;
						fused->elementDst = element->elementDst;
						fused->arraySrc = element->arraySrc;
						fused->indexSrc = element->indexSrc;
						fused->sizedCopyDst = element->sizedCopyDst;
						fused->sizedCopySrc = element->sizedCopySrc;
						fused->sizedCopySize = element->sizedCopySize;
						fused->tailCopyDst1 = tail->copyDst1;
						fused->tailCopySrc1 = tail->copySrc1;
						fused->tailCopyDst2 = tail->copyDst2;
						fused->tailCopySrc2 = tail->copySrc2;
						fused->tailCopyDst3 = tail->copyDst3;
						fused->tailCopySrc3 = tail->copySrc3;
						fused->addRet = tail->addRet;
						fused->addOp1 = tail->addOp1;
						fused->addOp2 = tail->addOp2;
						fused->constDst = tail->constDst;
						fused->constant = tail->constant;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar)
					{
						IRLdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4* head = (IRLdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4*)ir;
						IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar* tail = (IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar*)next;
						CreateIR(fused, LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar);
						fused->copyDst = head->copyDst;
						fused->copySrc = head->copySrc;
						fused->subRet = head->subRet;
						fused->subOp1 = head->subOp1;
						fused->subOp2 = head->subOp2;
						fused->storeAddress = head->storeAddress;
						fused->addressDst = tail->addressDst;
						fused->addressSrc = tail->addressSrc;
						fused->fieldAddressDst = tail->fieldAddressDst;
						fused->obj = tail->obj;
						fused->offset = tail->offset;
						fused->addressCopyDst = tail->copyDst;
						fused->addressCopySrc = tail->copySrc;
						fused->indDst = tail->indDst;
						fused->indSrc = tail->indSrc;
						fused->constDst = tail->constDst;
						fused->constant = tail->constant;
						fused->tailCopyDst1 = tail->tailCopyDst1;
						fused->tailCopySrc1 = tail->tailCopySrc1;
						fused->tailCopyDst2 = tail->tailCopyDst2;
						fused->tailCopySrc2 = tail->tailCopySrc2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4 && next->type == HiOpcodeEnum::BinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4)
					{
						IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4* head = (IRBinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4*)ir;
						IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4* tail = (IRBinOpVarVarVar_Sub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_Sub_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpSub_i4_MathMaxVarVarVar_i4_BinOpSub_i4_StindVarVar_i4);
						fused->headSubRet = head->subRet;
						fused->headSubOp1 = head->subOp1;
						fused->headSubOp2 = head->subOp2;
						fused->fieldDst = head->fieldDst;
						fused->obj = head->obj;
						fused->offset = head->offset;
						fused->constDst = head->constDst;
						fused->constant = head->constant;
						fused->divRet = head->divRet;
						fused->divOp1 = head->divOp1;
						fused->divOp2 = head->divOp2;
						fused->chainSubRet = tail->subRet;
						fused->chainSubOp1 = tail->subOp1;
						fused->chainSubOp2 = tail->subOp2;
						fused->maxRet = tail->maxRet;
						fused->maxOp1 = tail->maxOp1;
						fused->maxOp2 = tail->maxOp2;
						fused->tailSubRet = tail->tailSubRet;
						fused->tailSubOp1 = tail->tailSubOp1;
						fused->tailSubOp2 = tail->tailSubOp2;
						fused->storeAddress = tail->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4)
					{
						IRLdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4* head = (IRLdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4*)ir;
						IRLdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4* tail = (IRLdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4);
						fused->leadingCopyDst = head->leadingCopyDst;
						fused->leadingCopySrc = head->leadingCopySrc;
						fused->addressDst = head->addressDst;
						fused->addressSrc = head->addressSrc;
						fused->fieldAddressDst = head->fieldAddressDst;
						fused->obj = head->obj;
						fused->offset = head->offset;
						fused->copyDst = head->copyDst;
						fused->copySrc = head->copySrc;
						fused->indDst = head->indDst;
						fused->indSrc = head->indSrc;
						fused->tailCopyDst = tail->copyDst;
						fused->tailCopySrc = tail->copySrc;
						fused->subRet = tail->subRet;
						fused->subOp1 = tail->subOp1;
						fused->subOp2 = tail->subOp2;
						fused->storeAddress = tail->storeAddress;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4)
					{
						IRLdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar* head = (IRLdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar*)ir;
						IRLdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4* tail = (IRLdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4*)next;
						CreateIR(fused, LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar_GetArrayLengthVarVar_BranchVarVar_Clt_i4);
						fused->headCopyDst1 = head->copyDst1;
						fused->headCopySrc1 = head->copySrc1;
						fused->headConstDst = head->constDst;
						fused->headConstant = head->constant;
						fused->headAddRet = head->addRet;
						fused->headAddOp1 = head->addOp1;
						fused->headAddOp2 = head->addOp2;
						fused->headCopyDst2 = head->copyDst2;
						fused->headCopySrc2 = head->copySrc2;
						fused->lengthCopyDst1 = tail->copyDst1;
						fused->lengthCopySrc1 = tail->copySrc1;
						fused->lengthCopyDst2 = tail->copyDst2;
						fused->lengthCopySrc2 = tail->copySrc2;
						fused->lengthDst = tail->lengthDst;
						fused->arraySrc = tail->arraySrc;
						fused->branchOp1 = tail->branchOp1;
						fused->branchOp2 = tail->branchOp2;
						fused->offset = tail->offset;
						relocationOffsets.push_back(&fused->offset);
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
			writeIdx = 0;
			for (size_t readIdx = 0; readIdx < insts.size(); readIdx++)
			{
				IRCommon* ir = insts[readIdx];
				if (readIdx + 1 < insts.size())
				{
					IRCommon* next = insts[readIdx + 1];
					if (ir->type == HiOpcodeEnum::LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4 && next->type == HiOpcodeEnum::LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar)
					{
						IRLdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4* head = (IRLdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4*)ir;
						IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar* tail = (IRLdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar*)next;
						CreateIR(fused, LdlocVarVar_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdlocVarVar_BinOpVarVarVar_Sub_i4_StindVarVar_i4_LdlocVarAddress_LdfldaVarVar_LdlocVarVar_LdindVarVar_i4_LdcVarConst_4_LdlocVarVar_LdlocVarVar);
						fused->leadingCopyDst = head->leadingCopyDst;
						fused->leadingCopySrc = head->leadingCopySrc;
						fused->firstAddressDst = head->addressDst;
						fused->firstAddressSrc = head->addressSrc;
						fused->firstFieldAddressDst = head->fieldAddressDst;
						fused->firstObj = head->obj;
						fused->firstOffset = head->offset;
						fused->firstCopyDst = head->copyDst;
						fused->firstCopySrc = head->copySrc;
						fused->firstIndDst = head->indDst;
						fused->firstIndSrc = head->indSrc;
						fused->tailCopyDst = head->tailCopyDst;
						fused->tailCopySrc = head->tailCopySrc;
						fused->subRet = head->subRet;
						fused->subOp1 = head->subOp1;
						fused->subOp2 = head->subOp2;
						fused->storeAddress = head->storeAddress;
						fused->secondAddressDst = tail->addressDst;
						fused->secondAddressSrc = tail->addressSrc;
						fused->secondFieldAddressDst = tail->fieldAddressDst;
						fused->secondObj = tail->obj;
						fused->secondOffset = tail->offset;
						fused->secondCopyDst = tail->copyDst;
						fused->secondCopySrc = tail->copySrc;
						fused->secondIndDst = tail->indDst;
						fused->secondIndSrc = tail->indSrc;
						fused->constDst = tail->constDst;
						fused->constant = tail->constant;
						fused->secondTailCopyDst1 = tail->tailCopyDst1;
						fused->secondTailCopySrc1 = tail->tailCopySrc1;
						fused->secondTailCopyDst2 = tail->tailCopyDst2;
						fused->secondTailCopySrc2 = tail->tailCopySrc2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
					if (ir->type == HiOpcodeEnum::BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar && next->type == HiOpcodeEnum::LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4)
					{
						IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar* head = (IRBinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar*)ir;
						IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4* tail = (IRLdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4*)next;
						CreateIR(fused, BinOpVarVarVar_And_i4_GetArrayElementVarVar_i4_LdfldValueTypeVarVar_i4_LdcVarConst_4_BinOpDiv_i4_BinOpAdd_i4_LdlocVarVar_LdfldValueTypeVarVar_i4_LdlocVarVar_LdcVarConst_4_BinOpDiv_i4_MathMinVarVarVar_i4);
						fused->andRet = head->andRet;
						fused->andOp1 = head->andOp1;
						fused->andOp2 = head->andOp2;
						fused->elementDst = head->elementDst;
						fused->arraySrc = head->arraySrc;
						fused->indexSrc = head->indexSrc;
						fused->firstFieldDst = head->fieldDst;
						fused->firstObj = head->obj;
						fused->firstOffset = head->offset;
						fused->firstConstDst = head->constDst;
						fused->firstConstant = head->constant;
						fused->firstDivRet = head->divRet;
						fused->firstDivOp1 = head->divOp1;
						fused->firstDivOp2 = head->divOp2;
						fused->addRet = head->addRet;
						fused->addOp1 = head->addOp1;
						fused->addOp2 = head->addOp2;
						fused->copyDst = head->copyDst;
						fused->copySrc = head->copySrc;
						fused->secondFieldDst = tail->fieldDst;
						fused->secondObj = tail->obj;
						fused->secondOffset = tail->offset;
						fused->secondCopyDst = tail->copyDst;
						fused->secondCopySrc = tail->copySrc;
						fused->secondConstDst = tail->constDst;
						fused->secondConstant = tail->constant;
						fused->secondDivRet = tail->divRet;
						fused->secondDivOp1 = tail->divOp1;
						fused->secondDivOp2 = tail->divOp2;
						fused->minRet = tail->minRet;
						fused->minOp1 = tail->minOp1;
						fused->minOp2 = tail->minOp2;
						insts[writeIdx++] = fused;
						readIdx++;
						continue;
					}
				}
				insts[writeIdx++] = ir;
			}
			insts.resize(writeIdx);
#if HOTC233_ENABLE_PRO_CALL_TRACE
			FoldRunArrayI4IncrementTrace(insts);
			std::vector<IRCommon*> staticF4CallTraceOutput;
			staticF4CallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeStatic_f4_0
					|| ir->type == HiOpcodeEnum::CallCommonNativeStatic_f4_0Cached)
				{
					IRCallCommonNativeStatic_f4_0* firstCall = (IRCallCommonNativeStatic_f4_0*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeStatic_f4_0
							&& callIr->type != HiOpcodeEnum::CallCommonNativeStatic_f4_0Cached)
						{
							break;
						}
						IRCallCommonNativeStatic_f4_0* call = (IRCallCommonNativeStatic_f4_0*)callIr;
						if (call->method != firstCall->method || callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
						TrySkipPostStaticCallLocalStore(insts, scanIdx, call->ret, nullptr, nullptr);
					}
					if (callCount >= 10)
					{
						int32_t traceDataIndex = 0;
						uint64_t* traceData = nullptr;
						AllocResolvedData(resolveDatas, (int32_t)callCount, traceDataIndex, traceData);
						size_t writeStep = 0;
						scanIdx = readIdx;
						while (writeStep < callCount)
						{
							IRCallCommonNativeStatic_f4_0* call = (IRCallCommonNativeStatic_f4_0*)insts[scanIdx++];
							uint16_t copyDst = 0xffff;
							uint16_t copySrc = 0;
							TrySkipPostStaticCallLocalStore(insts, scanIdx, call->ret, &copyDst, &copySrc);
							traceData[writeStep++] =
								((uint64_t)call->ret) |
								((uint64_t)copyDst << 16) |
								((uint64_t)copySrc << 32);
						}
						CreateIR(trace, RunStaticF4CallTrace);
						trace->stepCount = (uint16_t)callCount;
						trace->traceData = (uint32_t)traceDataIndex;
						trace->method = firstCall->method;
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::StaticF4OrNoArg);
#else
						trace->thunkCache = 0;
#endif
						staticF4CallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				staticF4CallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(staticF4CallTraceOutput);
			std::vector<IRCommon*> staticI4CallTraceOutput;
			staticI4CallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeStatic_i4_0
					|| ir->type == HiOpcodeEnum::CallCommonNativeStatic_i4_0Cached)
				{
					IRCallCommonNativeStatic_i4_0* firstCall = (IRCallCommonNativeStatic_i4_0*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeStatic_i4_0
							&& callIr->type != HiOpcodeEnum::CallCommonNativeStatic_i4_0Cached)
						{
							break;
						}
						IRCallCommonNativeStatic_i4_0* call = (IRCallCommonNativeStatic_i4_0*)callIr;
						if (call->method != firstCall->method || callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
						TrySkipPostStaticCallLocalStore(insts, scanIdx, call->ret, nullptr, nullptr);
					}
					if (callCount >= 10)
					{
						int32_t traceDataIndex = 0;
						uint64_t* traceData = nullptr;
						AllocResolvedData(resolveDatas, (int32_t)callCount, traceDataIndex, traceData);
						size_t writeStep = 0;
						scanIdx = readIdx;
						while (writeStep < callCount)
						{
							IRCallCommonNativeStatic_i4_0* call = (IRCallCommonNativeStatic_i4_0*)insts[scanIdx++];
							uint16_t copyDst = 0xffff;
							uint16_t copySrc = 0;
							TrySkipPostStaticCallLocalStore(insts, scanIdx, call->ret, &copyDst, &copySrc);
							traceData[writeStep++] =
								((uint64_t)call->ret) |
								((uint64_t)copyDst << 16) |
								((uint64_t)copySrc << 32);
						}
						CreateIR(trace, RunStaticI4CallTrace);
						trace->stepCount = (uint16_t)callCount;
						trace->traceData = (uint32_t)traceDataIndex;
						trace->method = firstCall->method;
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::StaticI4OrNoArg);
						staticI4CallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				staticI4CallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(staticI4CallTraceOutput);
			std::vector<IRCommon*> instanceI4x5CallTraceOutput;
			instanceI4x5CallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeInstance_v_i4_5
					|| ir->type == HiOpcodeEnum::CallCommonNativeInstance_v_i4_5Cached)
				{
					IRCallCommonNativeInstance_v_i4_5* firstCall = (IRCallCommonNativeInstance_v_i4_5*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						while (scanIdx < insts.size() && IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						while (scanIdx < insts.size()
							&& insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVar
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						while (scanIdx < insts.size()
							&& insts[scanIdx]->type == HiOpcodeEnum::RegI32Copy
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						int32_t ldcSkipped = 0;
						while (scanIdx < insts.size() && ldcSkipped < 5)
						{
							IRCommon* skipIr = insts[scanIdx];
							if (IsNoOpTransformInstruction(skipIr))
							{
								scanIdx++;
								continue;
							}
							if (skipIr->type == HiOpcodeEnum::LdcVarConst_4
								|| skipIr->type == HiOpcodeEnum::LdcVarConst_1
								|| skipIr->type == HiOpcodeEnum::LdcVarConst_2)
							{
								ldcSkipped++;
								scanIdx++;
								continue;
							}
							if (skipIr->type == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4)
							{
								ldcSkipped++;
								scanIdx++;
								continue;
							}
							break;
						}
						if (scanIdx >= insts.size())
						{
							break;
						}
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeInstance_v_i4_5
							&& callIr->type != HiOpcodeEnum::CallCommonNativeInstance_v_i4_5Cached)
						{
							break;
						}
						IRCallCommonNativeInstance_v_i4_5* call = (IRCallCommonNativeInstance_v_i4_5*)callIr;
						if (call->method != firstCall->method
							|| call->self != firstCall->self
							|| callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
					}
					if (callCount >= 10)
					{
						uint16_t stepCount = (uint16_t)callCount;
						CreateIR(trace, RunInstanceVoidI4x5CallTrace);
						trace->stepCount = stepCount;
						trace->self = firstCall->self;
						trace->param0 = firstCall->param0;
						trace->param1 = firstCall->param1;
						trace->param2 = firstCall->param2;
						trace->param3 = firstCall->param3;
						trace->param4 = firstCall->param4;
						trace->method = firstCall->method;
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::InstanceVoidI4x5);
						instanceI4x5CallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				instanceI4x5CallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(instanceI4x5CallTraceOutput);
			std::vector<IRCommon*> instanceV3x4CallTraceOutput;
			instanceV3x4CallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeInstance_v_v3_4Cached)
				{
					IRCallCommonNativeInstance_v_v3_4Cached* firstCall = (IRCallCommonNativeInstance_v_v3_4Cached*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						while (scanIdx < insts.size() && IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						while (scanIdx < insts.size()
							&& (insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVar
								|| insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVarSize
								|| insts[scanIdx]->type == HiOpcodeEnum::RegVector3Copy)
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						if (scanIdx >= insts.size())
						{
							break;
						}
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeInstance_v_v3_4Cached)
						{
							break;
						}
						IRCallCommonNativeInstance_v_v3_4Cached* call = (IRCallCommonNativeInstance_v_v3_4Cached*)callIr;
						if (call->method != firstCall->method
							|| call->self != firstCall->self
							|| callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
					}
					if (callCount >= 10)
					{
						CreateIR(trace, RunInstanceVoidV3x4CallTrace);
						trace->stepCount = (uint16_t)callCount;
						trace->self = firstCall->self;
						trace->param0 = firstCall->param0;
						trace->param1 = firstCall->param1;
						trace->param2 = firstCall->param2;
						trace->param3 = firstCall->param3;
						trace->method = firstCall->method;
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::InstanceVoidV3x4);
						instanceV3x4CallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				instanceV3x4CallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(instanceV3x4CallTraceOutput);
			std::vector<IRCommon*> instanceV3x1CallTraceOutput;
			instanceV3x1CallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeInstance_v_v3_1Cached)
				{
					IRCallCommonNativeInstance_v_v3_1Cached* firstCall = (IRCallCommonNativeInstance_v_v3_1Cached*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						if (scanIdx < insts.size()
							&& insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVar
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
						if (scanIdx >= insts.size())
						{
							break;
						}
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeInstance_v_v3_1Cached)
						{
							break;
						}
						IRCallCommonNativeInstance_v_v3_1Cached* call = (IRCallCommonNativeInstance_v_v3_1Cached*)callIr;
						if (call->method != firstCall->method
							|| call->self != firstCall->self
							|| call->param0 != firstCall->param0
							|| callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
					}
					if (callCount >= 3)
					{
						CreateIR(trace, RunInstanceVoidV3x1CallTrace);
						trace->stepCount = (uint16_t)callCount;
						trace->self = firstCall->self;
						trace->param0 = firstCall->param0;
						trace->method = firstCall->method;
						trace->thunkCache = 0;
						instanceV3x1CallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				instanceV3x1CallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(instanceV3x1CallTraceOutput);
			std::vector<IRCommon*> getTransformSetV3CallTraceOutput;
			getTransformSetV3CallTraceOutput.reserve(insts.size());
			auto tryParseRefGetter = [this](IRCommon* callIr, uint16_t* self, uint16_t* ret, uint32_t* method) -> bool
			{
				if (callIr->type == HiOpcodeEnum::CallCommonNativeInstance_ref_0Cached)
				{
					IRCallCommonNativeInstance_ref_0Cached* getCall = (IRCallCommonNativeInstance_ref_0Cached*)callIr;
					*self = getCall->self;
					*ret = getCall->ret;
					*method = getCall->method;
					return true;
				}
				if (callIr->type == HiOpcodeEnum::CallCommonNativeInstance_i4_0
					|| callIr->type == HiOpcodeEnum::CallCommonNativeInstance_i8_0)
				{
					IRCallCommonNativeInstance_i4_0* getCall = (IRCallCommonNativeInstance_i4_0*)callIr;
					*self = getCall->self;
					*ret = getCall->ret;
					*method = getCall->method;
					return true;
				}
				if (callIr->type == HiOpcodeEnum::CallNativeInstance_ret
					|| callIr->type == HiOpcodeEnum::CallNativeInstance_ret_expand)
				{
					IRCallNativeInstance_ret* getCall = (IRCallNativeInstance_ret*)callIr;
					uint16_t* argIdxs = (uint16_t*)&resolveDatas[getCall->argIdxs];
					*self = argIdxs[0];
					*ret = getCall->ret;
					*method = getCall->methodInfo;
					return true;
				}
				return false;
			};
			auto tryParseV3Setter = [this](IRCommon* callIr, uint16_t* self, uint16_t* paramV3, uint32_t* method) -> bool
			{
				if (callIr->type == HiOpcodeEnum::CallCommonNativeInstance_v_v3_1Cached)
				{
					IRCallCommonNativeInstance_v_v3_1Cached* setCall = (IRCallCommonNativeInstance_v_v3_1Cached*)callIr;
					*self = setCall->self;
					*paramV3 = setCall->param0;
					*method = setCall->method;
					return true;
				}
				if (callIr->type == HiOpcodeEnum::CallNativeInstance_void)
				{
					IRCallNativeInstance_void* setCall = (IRCallNativeInstance_void*)callIr;
					uint16_t* argIdxs = (uint16_t*)&resolveDatas[setCall->argIdxs];
					*self = argIdxs[0];
					*paramV3 = argIdxs[1];
					*method = setCall->methodInfo;
					return true;
				}
				return false;
			};
			auto isIgnorableBeforeGetTransformSetPair = [](HiOpcodeEnum op) -> bool
			{
				switch (op)
				{
				case HiOpcodeEnum::LdlocVarVar:
				case HiOpcodeEnum::BranchUncondition_4:
				case HiOpcodeEnum::BranchTrueVar_i4:
				case HiOpcodeEnum::BranchFalseVar_i4:
					return true;
				default:
					return false;
				}
			};
			auto skipBetweenGetTransformSetPair = [&](size_t& idx) -> void
			{
				while (idx < insts.size())
				{
					IRCommon* skipIr = insts[idx];
					if (IsNoOpTransformInstruction(skipIr))
					{
						idx++;
						continue;
					}
					HiOpcodeEnum op = skipIr->type;
					if (isIgnorableBeforeGetTransformSetPair(op))
					{
						idx++;
						continue;
					}
					if (op == HiOpcodeEnum::LdlocVarVar && !IsNoOpTransformInstruction(skipIr))
					{
						idx++;
						continue;
					}
					if (op == HiOpcodeEnum::LdlocVarVarSize && !IsNoOpTransformInstruction(skipIr))
					{
						idx++;
						continue;
					}
					if (op == HiOpcodeEnum::RegVector3Copy)
					{
						idx++;
						continue;
					}
					break;
				}
			};
			auto setSelfMatchesGetRet = [&insts](size_t getIrIdx, uint16_t getRet, uint16_t setSelf, size_t setIrIdx) -> bool
			{
				if (setSelf == getRet)
				{
					return true;
				}
				uint16_t frontier[8];
				size_t frontierCount = 1;
				frontier[0] = getRet;
				for (size_t hop = 0; hop < 4 && frontierCount > 0; hop++)
				{
					uint16_t next[8];
					size_t nextCount = 0;
					for (size_t fi = 0; fi < frontierCount; fi++)
					{
						if (frontier[fi] == setSelf)
						{
							return true;
						}
						for (size_t mid = getIrIdx + 1; mid < setIrIdx; mid++)
						{
							IRCommon* midIr = insts[mid];
							if (midIr->type == HiOpcodeEnum::LdlocVarVar)
							{
								IRLdlocVarVar* ld = (IRLdlocVarVar*)midIr;
								if (ld->src != frontier[fi])
								{
									continue;
								}
								if (ld->dst == setSelf)
								{
									return true;
								}
								for (size_t ni = 0; ni < nextCount; ni++)
								{
									if (next[ni] == ld->dst)
									{
										goto skipNextPush;
									}
								}
								if (nextCount < 8)
								{
									next[nextCount++] = ld->dst;
								}
							}
							else if (midIr->type == HiOpcodeEnum::LdlocVarVarSize)
							{
								IRLdlocVarVarSize* ld = (IRLdlocVarVarSize*)midIr;
								if (ld->src != frontier[fi])
								{
									continue;
								}
								if (ld->dst == setSelf)
								{
									return true;
								}
								for (size_t ni = 0; ni < nextCount; ni++)
								{
									if (next[ni] == ld->dst)
									{
										goto skipNextPush;
									}
								}
								if (nextCount < 8)
								{
									next[nextCount++] = ld->dst;
								}
							}
							else
							{
								continue;
							}
						skipNextPush:;
						}
					}
					frontierCount = nextCount;
					for (size_t ni = 0; ni < nextCount; ni++)
					{
						frontier[ni] = next[ni];
					}
				}
				return false;
			};
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				while (readIdx < insts.size() && isIgnorableBeforeGetTransformSetPair(insts[readIdx]->type))
				{
					uint16_t probeSelf = 0;
					uint16_t probeRet = 0;
					uint32_t probeMethod = 0;
					if (tryParseRefGetter(insts[readIdx], &probeSelf, &probeRet, &probeMethod))
					{
						break;
					}
					getTransformSetV3CallTraceOutput.push_back(insts[readIdx]);
					readIdx++;
				}
				if (readIdx >= insts.size())
				{
					break;
				}

				uint16_t firstSelf = 0;
				uint16_t firstRet = 0;
				uint32_t firstGetMethod = 0;
				if (!tryParseRefGetter(insts[readIdx], &firstSelf, &firstRet, &firstGetMethod))
				{
					getTransformSetV3CallTraceOutput.push_back(insts[readIdx]);
					readIdx++;
					continue;
				}

				size_t scanIdx = readIdx;
				size_t pairCount = 0;
				uint16_t paramV3 = 0;
				uint32_t setMethod = 0;
				while (scanIdx < insts.size())
				{
					skipBetweenGetTransformSetPair(scanIdx);
					if (scanIdx >= insts.size())
					{
						break;
					}
					uint16_t getSelf = 0;
					uint16_t getRet = 0;
					uint32_t getMethod = 0;
					if (!tryParseRefGetter(insts[scanIdx], &getSelf, &getRet, &getMethod))
					{
						break;
					}
					if (getSelf != firstSelf || getMethod != firstGetMethod)
					{
						break;
					}
					size_t getIrIdx = scanIdx;
					scanIdx++;
					skipBetweenGetTransformSetPair(scanIdx);
					if (scanIdx >= insts.size())
					{
						break;
					}
					uint16_t setSelf = 0;
					uint16_t setParamV3 = 0;
					uint32_t setMethodLocal = 0;
					if (!tryParseV3Setter(insts[scanIdx], &setSelf, &setParamV3, &setMethodLocal))
					{
						break;
					}
					if (!setSelfMatchesGetRet(getIrIdx, getRet, setSelf, scanIdx) || pairCount >= 0xffff)
					{
						break;
					}
					if (pairCount == 0)
					{
						paramV3 = setParamV3;
						setMethod = setMethodLocal;
					}
					else if (setParamV3 != paramV3 || setMethodLocal != setMethod)
					{
						break;
					}
					pairCount++;
					scanIdx++;
				}
				if (pairCount >= 2)
				{
					uint16_t stepCount = (uint16_t)pairCount;
					CreateIR(trace, RunInstanceGetTransformSetV3CallTrace);
					trace->stepCount = stepCount;
					trace->selfGo = firstSelf;
					trace->paramV3 = paramV3;
					trace->getMethod = firstGetMethod;
					trace->setMethod = setMethod;
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
					trace->getThunkCache = AllocAndBakeNativeThunkSlot(
						(const MethodInfo*)resolveDatas[firstGetMethod],
						interpreter::Hotc233DirectCallKind::StaticF4OrNoArg);
					trace->setThunkCache = AllocAndBakeNativeThunkSlot(
						(const MethodInfo*)resolveDatas[setMethod],
						interpreter::Hotc233DirectCallKind::InstanceVoidV3Setter);
#else
					trace->getThunkCache = 0;
					trace->setThunkCache = 0;
#endif
					getTransformSetV3CallTraceOutput.push_back(trace);
					readIdx = scanIdx;
					continue;
				}

				getTransformSetV3CallTraceOutput.push_back(insts[readIdx]);
				readIdx++;
			}
			insts.swap(getTransformSetV3CallTraceOutput);
#if 1 // V3 return trace folding: return-shape only.
			std::vector<IRCommon*> instanceV3ReturnCallTraceOutput;
			instanceV3ReturnCallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeInstance_v3_0Cached)
				{
					IRCallCommonNativeInstance_v3_0Cached* firstCall = (IRCallCommonNativeInstance_v3_0Cached*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeInstance_v3_0Cached)
						{
							break;
						}
						IRCallCommonNativeInstance_v3_0Cached* call = (IRCallCommonNativeInstance_v3_0Cached*)callIr;
						if (call->method != firstCall->method
							|| call->self != firstCall->self
							|| callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
						if (scanIdx < insts.size()
							&& (insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVar
								|| insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVarSize)
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
					}
					if (callCount >= 10)
					{
						uint16_t stepCount = (uint16_t)callCount;
						CreateIR(trace, RunInstanceV3ReturnCallTrace);
						trace->stepCount = stepCount;
						trace->self = firstCall->self;
						trace->ret = firstCall->ret;
						trace->method = firstCall->method;
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::InstanceV3Return);
						instanceV3ReturnCallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				instanceV3ReturnCallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(instanceV3ReturnCallTraceOutput);
#endif
#if 1 // I4 return trace folding: return-shape only.
			std::vector<IRCommon*> instanceI4ReturnCallTraceOutput;
			instanceI4ReturnCallTraceOutput.reserve(insts.size());
			for (size_t readIdx = 0; readIdx < insts.size();)
			{
				IRCommon* ir = insts[readIdx];
				if (ir->type == HiOpcodeEnum::CallCommonNativeInstance_i4_0)
				{
					IRCallCommonNativeInstance_i4_0* firstCall = (IRCallCommonNativeInstance_i4_0*)ir;
					size_t scanIdx = readIdx;
					size_t callCount = 0;
					while (scanIdx < insts.size())
					{
						IRCommon* callIr = insts[scanIdx];
						if (callIr->type != HiOpcodeEnum::CallCommonNativeInstance_i4_0)
						{
							break;
						}
						IRCallCommonNativeInstance_i4_0* call = (IRCallCommonNativeInstance_i4_0*)callIr;
						if (call->method != firstCall->method
							|| call->self != firstCall->self
							|| callCount >= 0xffff)
						{
							break;
						}
						callCount++;
						scanIdx++;
						if (scanIdx < insts.size()
							&& insts[scanIdx]->type == HiOpcodeEnum::LdlocVarVar
							&& !IsNoOpTransformInstruction(insts[scanIdx]))
						{
							scanIdx++;
						}
					}
					if (callCount >= 10)
					{
						uint16_t stepCount = (uint16_t)callCount;
						CreateIR(trace, RunInstanceI4ReturnCallTrace);
						trace->stepCount = stepCount;
						trace->self = firstCall->self;
						trace->ret = firstCall->ret;
						trace->method = firstCall->method;
						trace->thunkCache = AllocAndBakeNativeThunkSlot(
							(const MethodInfo*)resolveDatas[firstCall->method],
							interpreter::Hotc233DirectCallKind::StaticI4OrNoArg);
						instanceI4ReturnCallTraceOutput.push_back(trace);
						readIdx = scanIdx;
						continue;
					}
				}
				instanceI4ReturnCallTraceOutput.push_back(ir);
				readIdx++;
			}
			insts.swap(instanceI4ReturnCallTraceOutput);
#endif
#endif
		}
	}

	void TransformContext::OptimizeBasicBlocks()
	{
#if HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM && HOTC233_ENABLE_PRO_TRACE_FOLDING
		for (IRBasicBlock* bb : irbbs)
		{
			FoldBinOpI4AddChainTrace(bb->insts);
		}
#endif
		ApplyCommunityPeepholeFusion();
#if HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM
		for (IRBasicBlock* bb : irbbs)
		{
			std::vector<IRCommon*>& insts = bb->insts;
#if HOTC233_ENABLE_PRO_TRACE_FOLDING
			FoldFused920I4AddCopyTrace(insts);
			FoldBinOpI4AddChainTrace(insts);
#endif
			RecordTypedRegisterCoverage(insts);
			LowerTypedRegisterI32(insts);
#if HOTC233_ENABLE_PRO_TRACE_FOLDING
			FoldRegI32NumericTrace(insts);
			FoldRegI32AddCopyTrace(insts);
#endif
			LowerTypedRegisterVector3(insts);
#if HOTC233_ENABLE_PRO_TRACE_FOLDING
			FoldRegVector3AddTrace(insts);
#endif
		}
#endif
	}

	void TransformContext::FoldRunArrayI4IncrementTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 4;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			IRCommon* ir = insts[readIdx];
			if (ir->type != HiOpcodeEnum::GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			size_t scanIdx = readIdx;
			size_t runLength = 0;
			while (scanIdx < insts.size()
				&& insts[scanIdx]->type == HiOpcodeEnum::GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4)
			{
				runLength++;
				scanIdx++;
			}
			if (runLength < kMinRunLength)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)runLength * 2, traceDataIndex, traceData);
			for (size_t step = 0; step < runLength; step++)
			{
				IRGetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4* fused =
					(IRGetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4*)insts[readIdx + step];
				traceData[step * 2] =
					((uint64_t)fused->loadArraySrc) |
					((uint64_t)fused->loadIndexSrc << 16) |
					((uint64_t)fused->addValueSrc << 32) |
					((uint64_t)fused->storeArraySrc << 48);
				traceData[step * 2 + 1] = fused->storeIndexSrc;
			}
			CreateIR(trace, RunArrayI4IncrementTrace);
			trace->stepCount = (uint16_t)runLength;
			trace->traceData = (uint32_t)traceDataIndex;
			folded.push_back(trace);
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	void TransformContext::FoldFused920I4AddCopyTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 2;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			IRCommon* ir = insts[readIdx];
			if (ir->type != HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			size_t scanIdx = readIdx;
			size_t runLength = 0;
			while (scanIdx < insts.size()
				&& insts[scanIdx]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar)
			{
				runLength++;
				scanIdx++;
			}
			if (runLength < kMinRunLength)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)runLength * 3, traceDataIndex, traceData);
			for (size_t step = 0; step < runLength; step++)
			{
				IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar* addCopies =
					(IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar*)insts[readIdx + step];
				uint16_t copyDst1 = addCopies->copyDst1;
				uint16_t copySrc1 = addCopies->copySrc1;
				uint16_t copyDst2 = addCopies->copyDst2;
				uint16_t copySrc2 = addCopies->copySrc2;
				uint16_t copyDst3 = addCopies->copyDst3;
				uint16_t copySrc3 = addCopies->copySrc3;
				if (copyDst1 == copySrc1)
				{
					copyDst1 = 0xffff;
				}
				if (copyDst2 == copySrc2)
				{
					copyDst2 = 0xffff;
				}
				if (copyDst3 == copySrc3)
				{
					copyDst3 = 0xffff;
				}
				traceData[step * 3] =
					((uint64_t)addCopies->addRet) |
					((uint64_t)addCopies->addOp1 << 16) |
					((uint64_t)addCopies->addOp2 << 32) |
					((uint64_t)copyDst1 << 48);
				traceData[step * 3 + 1] =
					((uint64_t)copySrc1) |
					((uint64_t)copyDst2 << 16) |
					((uint64_t)copySrc2 << 32) |
					((uint64_t)copyDst3 << 48);
				traceData[step * 3 + 2] = copySrc3;
			}
			bool allAddOnly = true;
			for (size_t step = 0; step < runLength; step++)
			{
				uint64_t word0 = traceData[step * 3];
				uint64_t word1 = traceData[step * 3 + 1];
				uint64_t word2 = traceData[step * 3 + 2];
				if ((uint16_t)(word0 >> 48) != 0xffff
					|| (uint16_t)(word1 >> 16) != 0xffff
					|| (uint16_t)(word1 >> 32) != 0xffff
					|| (uint16_t)word2 != 0)
				{
					allAddOnly = false;
					break;
				}
			}
			if (allAddOnly && runLength >= 4)
			{
				int32_t addTraceDataIndex = 0;
				uint64_t* addTraceData = nullptr;
				AllocResolvedData(resolveDatas, (int32_t)runLength, addTraceDataIndex, addTraceData);
				for (size_t step = 0; step < runLength; step++)
				{
					IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar* addCopies =
						(IRBinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar*)insts[readIdx + step];
					addTraceData[step] = ((uint64_t)addCopies->addRet) | ((uint64_t)addCopies->addOp1 << 16) | ((uint64_t)addCopies->addOp2 << 32);
				}
				CreateIR(addTrace, RunRegI32AddTrace);
				addTrace->stepCount = (uint16_t)runLength;
				addTrace->traceData = (uint32_t)addTraceDataIndex;
				folded.push_back(addTrace);
			}
			else
			{
				CreateIR(copyTrace, RunRegI32AddCopyTrace);
				copyTrace->stepCount = (uint16_t)runLength;
				copyTrace->traceData = (uint32_t)traceDataIndex;
				folded.push_back(copyTrace);
			}
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	void TransformContext::FoldBinOpI4AddChainTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 3;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			IRCommon* ir = insts[readIdx];
			if (ir->type != HiOpcodeEnum::BinOpVarVarVar_Add_i4)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			size_t scanIdx = readIdx;
			size_t runLength = 0;
			while (scanIdx < insts.size() && insts[scanIdx]->type == HiOpcodeEnum::BinOpVarVarVar_Add_i4)
			{
				runLength++;
				scanIdx++;
			}
			if (runLength < kMinRunLength)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)runLength, traceDataIndex, traceData);
			for (size_t step = 0; step < runLength; step++)
			{
				IRBinOpVarVarVar_Add_i4* add = (IRBinOpVarVarVar_Add_i4*)insts[readIdx + step];
				traceData[step] = ((uint64_t)add->ret) | ((uint64_t)add->op1 << 16) | ((uint64_t)add->op2 << 32);
			}
			CreateIR(trace, RunRegI32AddTrace);
			trace->stepCount = (uint16_t)runLength;
			trace->traceData = (uint32_t)traceDataIndex;
			folded.push_back(trace);
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	void TransformContext::FoldRegI32AddCopyTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 3;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			if (insts[readIdx]->type != HiOpcodeEnum::RegI32Add)
			{
				folded.push_back(insts[readIdx]);
				readIdx++;
				continue;
			}
			size_t groupStart = readIdx;
			size_t groupCount = 0;
			size_t scanIdx = readIdx;
			while (scanIdx < insts.size() && insts[scanIdx]->type == HiOpcodeEnum::RegI32Add)
			{
				scanIdx++;
				size_t copyIdx = scanIdx;
				uint8_t copyCount = 0;
				while (copyIdx < insts.size()
					&& copyCount < 3
					&& insts[copyIdx]->type == HiOpcodeEnum::RegI32Copy)
				{
					IRRegI32Copy* copy = (IRRegI32Copy*)insts[copyIdx];
					if (copy->dst == copy->src)
					{
						copyIdx++;
						continue;
					}
					copyCount++;
					copyIdx++;
				}
				scanIdx = copyIdx;
				groupCount++;
			}
			if (groupCount < kMinRunLength)
			{
				folded.push_back(insts[readIdx]);
				readIdx++;
				continue;
			}
			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)groupCount * 3, traceDataIndex, traceData);
			size_t writeStep = 0;
			scanIdx = groupStart;
			while (writeStep < groupCount)
			{
				IRRegI32Add* add = (IRRegI32Add*)insts[scanIdx++];
				uint16_t copyDst1 = 0xffff;
				uint16_t copySrc1 = 0;
				uint16_t copyDst2 = 0xffff;
				uint16_t copySrc2 = 0;
				uint16_t copyDst3 = 0xffff;
				uint16_t copySrc3 = 0;
				uint8_t packedCopies = 0;
				while (scanIdx < insts.size()
					&& packedCopies < 3
					&& insts[scanIdx]->type == HiOpcodeEnum::RegI32Copy)
				{
					IRRegI32Copy* copy = (IRRegI32Copy*)insts[scanIdx++];
					if (copy->dst == copy->src)
					{
						continue;
					}
					if (packedCopies == 0)
					{
						copyDst1 = copy->dst;
						copySrc1 = copy->src;
					}
					else if (packedCopies == 1)
					{
						copyDst2 = copy->dst;
						copySrc2 = copy->src;
					}
					else
					{
						copyDst3 = copy->dst;
						copySrc3 = copy->src;
					}
					packedCopies++;
				}
				traceData[writeStep * 3] =
					((uint64_t)add->ret) |
					((uint64_t)add->op1 << 16) |
					((uint64_t)add->op2 << 32) |
					((uint64_t)copyDst1 << 48);
				traceData[writeStep * 3 + 1] =
					((uint64_t)copySrc1) |
					((uint64_t)copyDst2 << 16) |
					((uint64_t)copySrc2 << 32) |
					((uint64_t)copyDst3 << 48);
				traceData[writeStep * 3 + 2] = copySrc3;
				writeStep++;
			}
			CreateIR(trace, RunRegI32AddCopyTrace);
			trace->stepCount = (uint16_t)groupCount;
			trace->traceData = (uint32_t)traceDataIndex;
			folded.push_back(trace);
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	uint32_t TransformContext::AllocResolveCacheSlot()
	{
		int32_t cacheIndex = 0;
		uint64_t* cacheBuf = nullptr;
		AllocResolvedData(resolveDatas, 1, cacheIndex, cacheBuf);
		return (uint32_t)cacheIndex;
	}

	void TransformContext::FoldRegI32NumericTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 3;
		const size_t kAddOnlyTraceMinRunLength = 4;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			IRCommon* ir = insts[readIdx];
			HiOpcodeEnum type = ir->type;
			if (type != HiOpcodeEnum::RegI32Add
				&& type != HiOpcodeEnum::RegI32Sub
				&& type != HiOpcodeEnum::RegI32Mul
				&& type != HiOpcodeEnum::RegI32Ldc)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}

			size_t scanIdx = readIdx;
			size_t runLength = 0;
			bool allAdds = type == HiOpcodeEnum::RegI32Add;
			while (scanIdx < insts.size())
			{
				HiOpcodeEnum scanType = insts[scanIdx]->type;
				if (scanType != HiOpcodeEnum::RegI32Add
					&& scanType != HiOpcodeEnum::RegI32Sub
					&& scanType != HiOpcodeEnum::RegI32Mul
					&& scanType != HiOpcodeEnum::RegI32Ldc)
				{
					break;
				}
				if (scanType != HiOpcodeEnum::RegI32Add)
				{
					allAdds = false;
				}
				runLength++;
				scanIdx++;
			}
			if (runLength < kMinRunLength)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}

			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)runLength, traceDataIndex, traceData);
			for (size_t step = 0; step < runLength; step++)
			{
				IRCommon* stepIr = insts[readIdx + step];
				uint16_t ret = 0;
				uint16_t op1 = 0;
				uint16_t op2 = 0;
				uint64_t kind = 0;
				switch (stepIr->type)
				{
				case HiOpcodeEnum::RegI32Ldc:
				{
					IRRegI32Ldc* ldc = (IRRegI32Ldc*)stepIr;
					ret = ldc->dst;
					op1 = (uint16_t)ldc->src;
					op2 = (uint16_t)(ldc->src >> 16);
					kind = 3;
					break;
				}
				case HiOpcodeEnum::RegI32Add:
				{
					IRRegI32Add* add = (IRRegI32Add*)stepIr;
					ret = add->ret;
					op1 = add->op1;
					op2 = add->op2;
					kind = 0;
					break;
				}
				case HiOpcodeEnum::RegI32Sub:
				{
					IRRegI32Sub* sub = (IRRegI32Sub*)stepIr;
					ret = sub->ret;
					op1 = sub->op1;
					op2 = sub->op2;
					kind = 1;
					break;
				}
				case HiOpcodeEnum::RegI32Mul:
				{
					IRRegI32Mul* mul = (IRRegI32Mul*)stepIr;
					ret = mul->ret;
					op1 = mul->op1;
					op2 = mul->op2;
					kind = 2;
					break;
				}
				default:
					break;
				}
				traceData[step] = ((uint64_t)ret) | ((uint64_t)op1 << 16) | ((uint64_t)op2 << 32) | (kind << 48);
			}
			if (allAdds && runLength >= kAddOnlyTraceMinRunLength)
			{
				CreateIR(trace, RunRegI32AddTrace);
				trace->stepCount = (uint16_t)runLength;
				trace->traceData = (uint32_t)traceDataIndex;
				folded.push_back(trace);
			}
			else
			{
				CreateIR(trace, RunRegI32NumericTrace);
				trace->stepCount = (uint16_t)runLength;
				trace->traceData = (uint32_t)traceDataIndex;
				folded.push_back(trace);
			}
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	void TransformContext::FoldRegVector3AddTrace(std::vector<IRCommon*>& insts)
	{
		const size_t kMinRunLength = 3;
		std::vector<IRCommon*> folded;
		folded.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			IRCommon* ir = insts[readIdx];
			if (ir->type != HiOpcodeEnum::RegVector3Add)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			size_t scanIdx = readIdx;
			size_t runLength = 0;
			while (scanIdx < insts.size() && insts[scanIdx]->type == HiOpcodeEnum::RegVector3Add)
			{
				runLength++;
				scanIdx++;
			}
			if (runLength < kMinRunLength)
			{
				folded.push_back(ir);
				readIdx++;
				continue;
			}
			int32_t traceDataIndex = 0;
			uint64_t* traceData = nullptr;
			AllocResolvedData(resolveDatas, (int32_t)runLength, traceDataIndex, traceData);
			for (size_t step = 0; step < runLength; step++)
			{
				IRRegVector3Add* add = (IRRegVector3Add*)insts[readIdx + step];
				traceData[step] = ((uint64_t)add->ret) | ((uint64_t)add->op1 << 16) | ((uint64_t)add->op2 << 32);
			}
			CreateIR(trace, RunRegVector3AddTrace);
			trace->stepCount = (uint16_t)runLength;
			trace->traceData = (uint32_t)traceDataIndex;
			folded.push_back(trace);
			readIdx = scanIdx;
		}
		insts.swap(folded);
	}

	void TransformContext::AddInst_ldarg(int32_t argIdx)
	{
		ArgVarInfo& __arg = args[argIdx];
		IRCommon* ir = CreateLoadExpandDataToStackVarVar(pool, GetEvalStackNewTopOffset(), __arg.argLocOffset, __arg.type, GetTypeValueSize(__arg.type));
		AddInst(ir);
		PushStackByType(__arg.type);
	}

	bool TransformContext::IsCreateNotNullObjectInstrument(IRCommon* ir)
	{
		switch (ir->type)
		{
		case HiOpcodeEnum::BoxVarVar:
		{
			IRBoxVarVar* irBox = (IRBoxVarVar*)ir;
			Il2CppClass* klass = ((Il2CppClass*)resolveDatas[irBox->klass]);
			return IS_CLASS_VALUE_TYPE(klass) && !il2cpp::vm::Class::IsNullable(klass);
		}
		case HiOpcodeEnum::NewSystemObjectVar:
		case HiOpcodeEnum::NewString:
		case HiOpcodeEnum::NewString_2:
		case HiOpcodeEnum::NewString_3:
		case HiOpcodeEnum::CtorDelegate:
		case HiOpcodeEnum::NewDelegate:
			//case HiOpcodeEnum::NewClassInterpVar_Ctor_0:
			//case HiOpcodeEnum::NewClassInterpVar:
			//case HiOpcodeEnum::NewClassVar:
			//case HiOpcodeEnum::NewClassVar_Ctor_0:
			//case HiOpcodeEnum::NewClassVar_NotCtor:
		case HiOpcodeEnum::NewMdArrVarVar_length:
		case HiOpcodeEnum::NewMdArrVarVar_length_bound:
		case HiOpcodeEnum::NewArrVarVar:
		case HiOpcodeEnum::LdsfldaFromFieldDataVarVar:
		case HiOpcodeEnum::LdsfldaVarVar:
		case HiOpcodeEnum::LdthreadlocalaVarVar:
		case HiOpcodeEnum::LdlocVarAddress:
			return true;
		default:
			return false;
		}
	}

	void TransformContext::RemoveLastInstrument()
	{
		IL2CPP_ASSERT(!curbb->insts.empty());
		curbb->insts.pop_back();
	}

	void TransformContext::AddInst_ldarga(int32_t argIdx)
	{
		IL2CPP_ASSERT(argIdx < actualParamCount);
		ArgVarInfo& argInfo = args[argIdx];
		CreateAddIR(ir, LdlocVarAddress);
		ir->dst = GetEvalStackNewTopOffset();
		ir->src = argInfo.argLocOffset;
		PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
	}

	void TransformContext::AddInst_starg(int32_t argIdx)
	{
		IL2CPP_ASSERT(argIdx < actualParamCount);
		ArgVarInfo& __arg = args[argIdx];
		IRCommon* ir = CreateAssignVarVar(pool, __arg.argLocOffset, GetEvalStackTopOffset(), GetTypeValueSize(__arg.type));
		AddInst(ir);
		PopStack();
	}

	void TransformContext::CreateAddInst_ldloc(int32_t locIdx)
	{
		LocVarInfo& __loc = locals[locIdx];
		IRCommon* ir = CreateLoadExpandDataToStackVarVar(pool, GetEvalStackNewTopOffset(), __loc.locOffset, __loc.type, GetTypeValueSize(__loc.type));
		AddInst(ir);
		PushStackByType(__loc.type);
	}

	void TransformContext::CreateAddInst_stloc(int32_t locIdx)
	{
		LocVarInfo& __loc = locals[locIdx];
		IRCommon* ir = CreateAssignVarVar(pool, __loc.locOffset, GetEvalStackTopOffset(), GetTypeValueSize(__loc.type));
		AddInst(ir);
		PopStack();
	}

	void TransformContext::CreateAddInst_ldloca(int32_t locIdx)
	{
		CreateAddIR(ir, LdlocVarAddress);
		LocVarInfo& __loc = locals[locIdx];
		ir->dst = GetEvalStackNewTopOffset();
		ir->src = __loc.locOffset;
		PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
	}

	void TransformContext::CreateAddInst_ldc4(int32_t c, EvalStackReduceDataType rtype)
	{
		CreateAddIR(ir, LdcVarConst_4);
		ir->dst = GetEvalStackNewTopOffset();
		ir->src = c;
		PushStackByReduceType(rtype);
	}

	void TransformContext::CreateAddInst_ldc8(int64_t c, EvalStackReduceDataType rtype)
	{
		CreateAddIR(ir, LdcVarConst_8);
		ir->dst = GetEvalStackNewTopOffset();
		ir->src = c;
		PushStackByReduceType(rtype);
	}

	void TransformContext::Add_brtruefalse(bool c, int32_t targetOffset)
	{
		EvalStackVarInfo& top = evalStack[evalStackTop - 1];
		IRCommon* lastIR = GetLastInstrument();
		if (lastIR == nullptr || !IsCreateNotNullObjectInstrument(lastIR))
		{
			if (top.byteSize <= 4)
			{
				CreateAddIR(ir, BranchTrueVar_i4);
				ir->type = c ? HiOpcodeEnum::BranchTrueVar_i4 : HiOpcodeEnum::BranchFalseVar_i4;
				ir->op = top.locOffset;
				ir->offset = targetOffset;
				PushOffset(&ir->offset);
			}
			else
			{
				CreateAddIR(ir, BranchTrueVar_i8);
				ir->type = c ? HiOpcodeEnum::BranchTrueVar_i8 : HiOpcodeEnum::BranchFalseVar_i8;
				ir->op = top.locOffset;
				ir->offset = targetOffset;
				PushOffset(&ir->offset);
			}
		}
		else
		{
			// optimize instrument sequence like` box T!; brtrue`
			// this optimization is not semanticly equals to origin instrument because may ommit `Class::InitRuntime`.
			// but it's ok in most occasions.
			RemoveLastInstrument();
			if (c)
			{
				// brtrue always true, replace with br
				CreateAddIR(ir, BranchUncondition_4);
				ir->offset = targetOffset;
				PushOffset(&ir->offset);
			}
			else
			{
				// brfalse always false, run throughtly.
			}
		}
		PopStack();
		PushBranch(targetOffset);
	}

	void TransformContext::Add_bc(int32_t ipOffset, int32_t brOffset, int32_t opSize, HiOpcodeEnum opI4, HiOpcodeEnum opI8, HiOpcodeEnum opR4, HiOpcodeEnum opR8)
	{
		int32_t targetOffset = ipOffset + brOffset + opSize;
		EvalStackVarInfo& op1 = evalStack[evalStackTop - 2];
		EvalStackVarInfo& op2 = evalStack[evalStackTop - 1];
		IRBranchVarVar_Ceq_i4* ir = pool.AllocIR<IRBranchVarVar_Ceq_i4>();
		ir->type = (HiOpcodeEnum)0;
		ir->op1 = op1.locOffset;
		ir->op2 = op2.locOffset;
		ir->offset = targetOffset;
		PushOffset(&ir->offset);
		switch (op1.reduceType)
		{
		case EvalStackReduceDataType::I4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI4;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				CreateAddIR(irConv, ConvertVarVar_i4_i8);
				irConv->dst = irConv->src = op1.locOffset;
				ir->type = opI8;
				break;
			}
			default:
			{
				IL2CPP_ASSERT(false && "I4 not match");
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::I8:
		{
			IL2CPP_ASSERT(op2.reduceType == EvalStackReduceDataType::I8);
			ir->type = opI8;
			break;
		}
		case EvalStackReduceDataType::R4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::R4:
			{
				ir->type = opR4;
				break;
			}
			default:
			{
				IL2CPP_ASSERT(false && "R4 not match");
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::R8:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::R8:
			{
				ir->type = opR8;
				break;
			}
			default:
			{
				IL2CPP_ASSERT(false && "R8 not match");
				break;
			}
			}
			break;
		}
		default:
		{
			IL2CPP_ASSERT(false && "nothing match");
		}
		}
		AddInst(ir);
		PopStackN(2);
		PushBranch(targetOffset);
	}

	void TransformContext::Add_conv(int32_t dstTypeSize, EvalStackReduceDataType dstReduceType, HiOpcodeEnum opI4, HiOpcodeEnum opI8, HiOpcodeEnum opR4, HiOpcodeEnum opR8)
	{
		IL2CPP_ASSERT(evalStackTop > 0);
		EvalStackVarInfo& top = evalStack[evalStackTop - 1];
		//if (top.reduceType != dstReduceType)
		{
			CreateIR(ir, ConvertVarVar_i4_u4);
			ir->type = (HiOpcodeEnum)0;
			ir->dst = ir->src = GetEvalStackTopOffset();
			switch (top.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI4;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				ir->type = opI8;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::R4:
			{
				ir->type = opR4;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::R8:
			{
				ir->type = opR8;
				AddInst(ir);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("conv");
				break;
			}
			}
		}

		top.reduceType = dstReduceType;
		top.byteSize = dstTypeSize;
		ip++;
	}

	void TransformContext::Add_conv_ovf(int32_t dstTypeSize, EvalStackReduceDataType dstReduceType, HiOpcodeEnum opI4, HiOpcodeEnum opI8, HiOpcodeEnum opR4, HiOpcodeEnum opR8)
	{
		IL2CPP_ASSERT(evalStackTop > 0);
		EvalStackVarInfo& top = evalStack[evalStackTop - 1];
		//if (top.reduceType != dstReduceType)
		{
			CreateIR(ir, ConvertOverflowVarVar_i4_u4);
			ir->type = (HiOpcodeEnum)0;
			ir->dst = ir->src = GetEvalStackTopOffset();
			switch (top.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI4;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				ir->type = opI8;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::R4:
			{
				ir->type = opR4;
				AddInst(ir);
				break;
			}
			case EvalStackReduceDataType::R8:
			{
				ir->type = opR8;
				AddInst(ir);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("conv_ovf");
				break;
			}
			}
		}

		top.reduceType = dstReduceType;
		top.byteSize = dstTypeSize;
		ip++;
	}

	void TransformContext::Add_binop(HiOpcodeEnum opI4, HiOpcodeEnum opI8, HiOpcodeEnum opR4, HiOpcodeEnum opR8)
	{
		IL2CPP_ASSERT(evalStackTop >= 2);
		EvalStackVarInfo& op1 = evalStack[evalStackTop - 2];
		EvalStackVarInfo& op2 = evalStack[evalStackTop - 1];
		CreateIR(ir, BinOpVarVarVar_Add_i4);
		ir->op1 = op1.locOffset;
		ir->op2 = op2.locOffset;
		ir->ret = op1.locOffset;
		EvalStackReduceDataType resultType;
		switch (op1.reduceType)
		{
		case EvalStackReduceDataType::I4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				resultType = EvalStackReduceDataType::I4;
				ir->type = opI4;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				CreateAddIR(irConv, ConvertVarVar_i4_i8);
				irConv->dst = irConv->src = op1.locOffset;
				ir->type = opI8;
				resultType = EvalStackReduceDataType::I8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("Add_bin_op I4 op unknown");
				resultType = (EvalStackReduceDataType)-1;
			}
			}
			break;
		}
		case EvalStackReduceDataType::I8:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				CreateAddIR(irConv, ConvertVarVar_i4_i8);
				irConv->dst = irConv->src = op2.locOffset;
				resultType = EvalStackReduceDataType::I8;
				ir->type = opI8;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				resultType = EvalStackReduceDataType::I8;
				ir->type = opI8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("Add_bin_op I8 op unknown");
				resultType = (EvalStackReduceDataType)-1;
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::R4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::R4:
			{
				resultType = EvalStackReduceDataType::R4;
				ir->type = opR4;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("Add_bin_op R4 op unknown");
				resultType = (EvalStackReduceDataType)-1;
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::R8:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::R8:
			{
				resultType = EvalStackReduceDataType::R8;
				ir->type = opR8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("Add_bin_op R8 op unknown");
				resultType = (EvalStackReduceDataType)-1;
				break;
			}
			}
			break;
		}
		default:
		{
			RaiseExecutionEngineException("Add_bin_op unknown");
			resultType = (EvalStackReduceDataType)-1;
			break;
		}
		}
		PopStack();
		op1.reduceType = resultType;
		op1.byteSize = GetSizeByReduceType(resultType);
		AddInst(ir);
		ip++;
	}

	void TransformContext::Add_shiftop(HiOpcodeEnum opI4I4, HiOpcodeEnum opI4I8, HiOpcodeEnum opI8I4, HiOpcodeEnum opI8I8)
	{
		IL2CPP_ASSERT(evalStackTop >= 2);
		EvalStackVarInfo& op1 = evalStack[evalStackTop - 2];
		EvalStackVarInfo& op2 = evalStack[evalStackTop - 1];
		CreateAddIR(ir, BitShiftBinOpVarVarVar_Shr_i4_i4);
		ir->ret = op1.locOffset;
		ir->value = op1.locOffset;
		ir->shiftAmount = op2.locOffset;
		switch (op1.reduceType)
		{
		case EvalStackReduceDataType::I4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI4I4;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				ir->type = opI4I8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("shitf i4");
			}
			}
			break;
		}
		case EvalStackReduceDataType::I8:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI8I4;
				break;
			}
			case EvalStackReduceDataType::I8:
			{

				ir->type = opI8I8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("shitf i8");
				break;
			}
			}
			break;
		}
		default:
		{
			RaiseExecutionEngineException("shitf i");
			break;
		}
		}
		PopStack();
		ip++;
	}

	void TransformContext::Add_compare(HiOpcodeEnum opI4, HiOpcodeEnum opI8, HiOpcodeEnum opR4, HiOpcodeEnum opR8)
	{
		IL2CPP_ASSERT(evalStackTop >= 2);
		EvalStackVarInfo& op1 = evalStack[evalStackTop - 2];
		EvalStackVarInfo& op2 = evalStack[evalStackTop - 1];
		CreateIR(ir, CompOpVarVarVar_Ceq_i4);
		ir->c1 = op1.locOffset;
		ir->c2 = op2.locOffset;
		ir->ret = op1.locOffset;
		switch (op1.reduceType)
		{
		case EvalStackReduceDataType::I4:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				ir->type = opI4;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				CreateAddIR(irConv, ConvertVarVar_i4_i8);
				irConv->dst = irConv->src = op1.locOffset;
				ir->type = opI8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("compare i4");
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::I8:
		{
			switch (op2.reduceType)
			{
			case EvalStackReduceDataType::I4:
			{
				CreateAddIR(irConv, ConvertVarVar_i4_i8);
				irConv->dst = irConv->src = op2.locOffset;
				ir->type = opI8;
				break;
			}
			case EvalStackReduceDataType::I8:
			{
				ir->type = opI8;
				break;
			}
			default:
			{
				RaiseExecutionEngineException("compare i8");
				break;
			}
			}
			break;
		}
		case EvalStackReduceDataType::R4:
		{
			if (op2.reduceType == EvalStackReduceDataType::R4)
			{
				ir->type = opR4;
			}
			else
			{
				RaiseExecutionEngineException("compare r4");
			}
			break;
		}
		case EvalStackReduceDataType::R8:
		{
			if (op2.reduceType == EvalStackReduceDataType::R8)
			{
				ir->type = opR8;
			}
			else
			{
				RaiseExecutionEngineException("compare r8");
			}
			break;
		}
		default:
		{
			RaiseExecutionEngineException("compare");
			break;
		}
		}
		PopStackN(2);
		AddInst(ir);
		PushStackByReduceType(EvalStackReduceDataType::I4);
	}

	void TransformContext::Add_ldelem(EvalStackReduceDataType resultType, HiOpcodeEnum opI4)
	{
		IL2CPP_ASSERT(evalStackTop >= 2);
		EvalStackVarInfo& arr = evalStack[evalStackTop - 2];
		EvalStackVarInfo& index = evalStack[evalStackTop - 1];

		CreateAddIR(ir, GetArrayElementVarVar_i1);
		ir->type = opI4;
		ir->arr = arr.locOffset;
		ir->index = index.locOffset;
		ir->dst = arr.locOffset;

		PopStackN(2);
		PushStackByReduceType(resultType);
		ip++;
	}

	void TransformContext::Add_stelem(HiOpcodeEnum opI4)
	{
		IL2CPP_ASSERT(evalStackTop >= 3);
		EvalStackVarInfo& arr = evalStack[evalStackTop - 3];
		EvalStackVarInfo& index = evalStack[evalStackTop - 2];
		EvalStackVarInfo& ele = evalStack[evalStackTop - 1];

		CreateAddIR(ir, SetArrayElementVarVar_i1);
		ir->type = opI4;
		ir->arr = arr.locOffset;
		ir->index = index.locOffset;
		ir->ele = ele.locOffset;

		PopStackN(3);
		ip++;
	}

	static int GetTypeSize(const Il2CppType* type)
	{
		if (type->byref)
		{
			return PTR_SIZE;
		}

		switch (type->type)
		{
		case IL2CPP_TYPE_I1:
		case IL2CPP_TYPE_U1:
		case IL2CPP_TYPE_BOOLEAN:
			return 1;
		case IL2CPP_TYPE_I2:
		case IL2CPP_TYPE_U2:
		case IL2CPP_TYPE_CHAR:
			return 2;
		case IL2CPP_TYPE_I4:
		case IL2CPP_TYPE_U4:
			return 4;
		case IL2CPP_TYPE_I8:
		case IL2CPP_TYPE_U8:
			return 8;
		case IL2CPP_TYPE_I:
		case IL2CPP_TYPE_U:
			return PTR_SIZE;
		case IL2CPP_TYPE_R4:
			return 4;
		case IL2CPP_TYPE_R8:
			return 8;
		case IL2CPP_TYPE_PTR:
		case IL2CPP_TYPE_FNPTR:
		case IL2CPP_TYPE_STRING:
		case IL2CPP_TYPE_SZARRAY:
		case IL2CPP_TYPE_ARRAY:
		case IL2CPP_TYPE_CLASS:
		case IL2CPP_TYPE_OBJECT:
		case IL2CPP_TYPE_VAR:
		case IL2CPP_TYPE_MVAR:
			return PTR_SIZE;
		case IL2CPP_TYPE_VALUETYPE:
			if (il2cpp::vm::Type::IsEnum(type))
			{
				return GetTypeSize(il2cpp::vm::Class::GetEnumBaseType(il2cpp::vm::Type::GetClass(type)));
			}
			else
			{
				Il2CppClass* klass = il2cpp::vm::Type::GetClass(type);
				return il2cpp::vm::Class::GetValueSize(klass, nullptr);
			}
		case IL2CPP_TYPE_GENERICINST:
		{
			Il2CppGenericClass* gclass = type->data.generic_class;

			if (gclass->type->type == IL2CPP_TYPE_CLASS)
			{
				IL2CPP_ASSERT(!IS_CLASS_VALUE_TYPE(il2cpp::vm::Class::FromIl2CppType(type)));
				return PTR_SIZE;
			}
			else
			{
				Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type);
				IL2CPP_ASSERT(IS_CLASS_VALUE_TYPE(klass));
				if (klass->enumtype)
				{
					return GetTypeSize(il2cpp::vm::Class::GetEnumBaseType(klass));
				}
				else
				{
					return il2cpp::vm::Class::GetValueSize(il2cpp::vm::Class::FromIl2CppType(type), nullptr);
				}
			}
		}
		default:
			IL2CPP_ASSERT(0);
			break;
		}
		return 0;
	}

	static int CalculateParameterSize(const MethodInfo* methodInfo)
	{
		int totalParameterSize = 0;
		for (uint8_t i = 0; i < methodInfo->parameters_count; i++)
		{
			const Il2CppType* paramType = GET_METHOD_PARAMETER_TYPE(methodInfo->parameters[i]);
			totalParameterSize += GetTypeSize(paramType);
		}
		return totalParameterSize;
	}

	static Il2CppMethodPointer ResolvePInvokeMethod(const MethodInfo* methodInfo, Il2CppCallConvention& callingConvention)
	{
		metadata::InterpreterImage* interpImage = metadata::MetadataModule::GetImage(methodInfo);
		if (!interpImage)
		{
			return nullptr;
		}
		hotc233::metadata::ImplMapInfo* implMap = interpImage->GetImplMapInfo(methodInfo->token);
		if (!implMap)
		{
			return nullptr;
		}

		uint32_t mappingFlags = implMap->mappingFlags;
		bool isNotMangle = hotc233::metadata::IsDllImportNoMangle(mappingFlags);
		Il2CppCharSet charSet = hotc233::metadata::GetDllImportCharSet(mappingFlags);
		callingConvention = hotc233::metadata::GetDllImportCallConvention(mappingFlags);

		Il2CppNativeString nativeModuleName = il2cpp::utils::StringUtils::Utf8ToNativeString(implMap->moduleName);
		int parameterSize = CalculateParameterSize(methodInfo);

		const PInvokeArguments pinvokeArgs =
		{
			il2cpp::utils::StringView<Il2CppNativeChar>(nativeModuleName.c_str(), nativeModuleName.length()),
			il2cpp::utils::StringView<char>(implMap->importName, std::strlen(implMap->importName)),
			callingConvention,
			charSet,
			parameterSize,
			isNotMangle,
		};
		Il2CppMethodPointer methodPointer = il2cpp::vm::PlatformInvoke::Resolve(pinvokeArgs);
		return methodPointer;
	}

	bool TransformContext::FindFirstLeaveHandlerIndex(const std::vector<ExceptionClause>& exceptionClauses, uint32_t leaveOffset, uint32_t targetOffset, uint16_t& index)
	{
		index = 0;
		for (const ExceptionClause& ec : exceptionClauses)
		{
			if (ec.flags == CorILExceptionClauseType::Finally)
			{
				if (ec.tryOffset <= leaveOffset && leaveOffset < ec.tryOffset + ec.tryLength)
					return !(ec.tryOffset <= targetOffset && targetOffset < ec.tryOffset + ec.tryLength);
			}
			++index;
		}
		return false;
	}

	bool TransformContext::IsLeaveInTryBlock(const std::vector<ExceptionClause>& exceptionClauses, uint32_t leaveOffset)
	{
		for (const ExceptionClause& ec : exceptionClauses)
		{
			if (ec.tryOffset <= leaveOffset && leaveOffset < ec.tryOffset + ec.tryLength)
			{
				return true;
			}
			if (ec.handlerOffsets <= leaveOffset && leaveOffset < ec.handlerOffsets + ec.handlerLength)
			{
				return false;
			}
		}
		return false;
	}

	void TransformContext::Add_leave(uint32_t targetOffset)
	{
		uint32_t leaveOffset = (uint32_t)(ip - ipBase);
		uint16_t firstHandlerIndex;
		if (FindFirstLeaveHandlerIndex(body.exceptionClauses, leaveOffset, targetOffset, firstHandlerIndex))
		{
			CreateAddIR(ir, LeaveEx);
			ir->target = targetOffset;
			ir->firstHandlerIndex = firstHandlerIndex;
			PushOffset(&ir->target);
		}
		else if (!IsLeaveInTryBlock(body.exceptionClauses, leaveOffset))
		{
			CreateAddIR(ir, LeaveEx_Directly);
			ir->target = targetOffset;
			PushOffset(&ir->target);
		}
		else
		{
			CreateAddIR(ir, BranchUncondition_4);
			ir->offset = targetOffset;
			PushOffset(&ir->offset);
		}
		PopAllStack();
		PushBranch(targetOffset);
	}

	uint16_t TransformContext::FindFirstThrowHandlerIndex(const std::vector<ExceptionClause>& exceptionClauses, uint32_t throwOffset)
	{
		uint16_t index = 0;
		for (const ExceptionClause& ec : exceptionClauses)
		{
			if (ec.flags == CorILExceptionClauseType::Finally || ec.flags == CorILExceptionClauseType::Exception || ec.flags == CorILExceptionClauseType::Filter)
			{
				if (ec.tryOffset <= throwOffset && throwOffset < ec.tryOffset + ec.tryLength)
					return index;
			}
			++index;
		}
		return index;
	}

	inline const Il2CppType* InflateIfNeeded(const Il2CppType* type, const Il2CppGenericContext* context, bool inflateMethodVars)
	{
		if (context == nullptr)
		{
			return type;
		}
		else
		{
			return il2cpp::metadata::GenericMetadata::InflateIfNeeded(type, context, inflateMethodVars);
		}
	}

#pragma region conv

#define CI_conv(dstTypeName, dstReduceType, dstTypeSize)   \
	Add_conv(dstTypeSize, EvalStackReduceDataType::dstReduceType, \
		HiOpcodeEnum::ConvertVarVar_i4_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_i8_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_f4_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_f8_##dstTypeName);

#define CI_conv_un(dstTypeName, dstReduceType, dstTypeSize)   \
	Add_conv(dstTypeSize, EvalStackReduceDataType::dstReduceType, \
		HiOpcodeEnum::ConvertVarVar_u4_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_u8_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_f4_##dstTypeName,\
		HiOpcodeEnum::ConvertVarVar_f8_##dstTypeName);

#define CI_conv_ovf(dstTypeName, dstReduceType, dstTypeSize)   \
	Add_conv_ovf(dstTypeSize, EvalStackReduceDataType::dstReduceType, \
		HiOpcodeEnum::ConvertOverflowVarVar_i4_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_i8_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_f4_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_f8_##dstTypeName);

#define CI_conv_un_ovf(dstTypeName, dstReduceType, dstTypeSize)   \
	Add_conv_ovf(dstTypeSize, EvalStackReduceDataType::dstReduceType, \
		HiOpcodeEnum::ConvertOverflowVarVar_u4_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_u8_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_f4_##dstTypeName,\
		HiOpcodeEnum::ConvertOverflowVarVar_f8_##dstTypeName);


#pragma endregion

#pragma region branch

#define CI_branch1(opName) IL2CPP_ASSERT(evalStackTop >= 2); \
brOffset = GetI1(ip+1); \
if (brOffset != 0) \
{\
	Add_bc(ipOffset, brOffset, 2, HiOpcodeEnum::BranchVarVar_##opName##_i4, HiOpcodeEnum::BranchVarVar_##opName##_i8, HiOpcodeEnum::BranchVarVar_##opName##_f4, HiOpcodeEnum::BranchVarVar_##opName##_f8); \
}\
else\
{\
	PopStackN(2);\
}\
ip += 2;

#define CI_branch4(opName) IL2CPP_ASSERT(evalStackTop >= 2); \
brOffset = GetI4LittleEndian(ip + 1); \
if (brOffset != 0) \
{ \
	Add_bc(ipOffset, brOffset, 5, HiOpcodeEnum::BranchVarVar_##opName##_i4, HiOpcodeEnum::BranchVarVar_##opName##_i8, HiOpcodeEnum::BranchVarVar_##opName##_f4, HiOpcodeEnum::BranchVarVar_##opName##_f8); \
}\
else \
{\
	PopStackN(2);\
}\
ip += 5;

#define PopBranch() { \
if (FindNextFlow()) \
{ \
    continue; \
} \
else \
{ \
    goto finish_transform; \
} \
}

#pragma endregion

#pragma region binop
#define CI_binOp(op) Add_binop(HiOpcodeEnum::BinOpVarVarVar_##op##_i4, HiOpcodeEnum::BinOpVarVarVar_##op##_i8, HiOpcodeEnum::BinOpVarVarVar_##op##_f4, HiOpcodeEnum::BinOpVarVarVar_##op##_f8);
#define CI_binOpUn(op) Add_binop(HiOpcodeEnum::BinOpVarVarVar_##op##_i4, HiOpcodeEnum::BinOpVarVarVar_##op##_i8, (HiOpcodeEnum)0, (HiOpcodeEnum)0);
#define CI_binOpOvf(op) Add_binop(HiOpcodeEnum::BinOpOverflowVarVarVar_##op##_i4, HiOpcodeEnum::BinOpOverflowVarVarVar_##op##_i8, (HiOpcodeEnum)0, (HiOpcodeEnum)0);
#define CI_binOpUnOvf(op) Add_binop(HiOpcodeEnum::BinOpOverflowVarVarVar_##op##_u4, HiOpcodeEnum::BinOpOverflowVarVarVar_##op##_u8, (HiOpcodeEnum)0, (HiOpcodeEnum)0);
#pragma endregion

#pragma region shiftop
#define	CI_binOpShift(op) Add_shiftop(HiOpcodeEnum::BitShiftBinOpVarVarVar_##op##_i4_i4, HiOpcodeEnum::BitShiftBinOpVarVarVar_##op##_i4_i8, HiOpcodeEnum::BitShiftBinOpVarVarVar_##op##_i8_i4, HiOpcodeEnum::BitShiftBinOpVarVarVar_##op##_i8_i8);
#pragma endregion

#define CI_compare(op) Add_compare(HiOpcodeEnum::CompOpVarVarVar_##op##_i4, HiOpcodeEnum::CompOpVarVarVar_##op##_i8, HiOpcodeEnum::CompOpVarVarVar_##op##_f4, HiOpcodeEnum::CompOpVarVarVar_##op##_f8);

#define CI_ldele(eleType, resultType) Add_ldelem(EvalStackReduceDataType::resultType, HiOpcodeEnum::GetArrayElementVarVar_##eleType);
#define CI_stele(eleType) Add_stelem(HiOpcodeEnum::SetArrayElementVarVar_##eleType);

	static const MethodInfo* FindRedirectCreateString(const MethodInfo* shareMethod)
	{
		int32_t paramCount = shareMethod->parameters_count;
		void* iter = nullptr;
		for (const MethodInfo* searchMethod; (searchMethod = il2cpp::vm::Class::GetMethods(il2cpp_defaults.string_class, &iter)) != nullptr;)
		{
			if (searchMethod->parameters_count != paramCount || std::strcmp(searchMethod->name, "CreateString"))
			{
				continue;
			}
			bool sigMatch = true;
			for (uint8_t i = 0; i < paramCount; i++)
			{
				if (!IsTypeEqual(GET_METHOD_PARAMETER_TYPE(searchMethod->parameters[i]), GET_METHOD_PARAMETER_TYPE(shareMethod->parameters[i])))
				{
					sigMatch = false;
					break;
				}
			}
			if (sigMatch)
			{
				return searchMethod;
			}
		}
		return nullptr;
	}

	static bool IsCallCommonTraceInlineBarrier(const MethodInfo* method)
	{
#if HOTC233_ENABLE_PRO_CALL_TRACE
		if (method == nullptr || metadata::IsInterpreterImplement(method))
		{
			return false;
		}
		if (IsInstanceMethod(method) || method->parameters_count != 0 || method->return_type == nullptr)
		{
			return false;
		}
		const Il2CppType* returnType = method->return_type;
		return returnType->type == IL2CPP_TYPE_R4 || returnType->type == IL2CPP_TYPE_I4;
#else
		(void)method;
		return false;
#endif
	}

	static bool ShouldBeInlined(const MethodInfo* method, int32_t depth)
	{
		if (depth >= RuntimeConfig::GetMaxMethodInlineDepth())
		{
			return false;
		}
		if (IsCallCommonTraceInlineBarrier(method))
		{
			return false;
		}
		return metadata::MethodBodyCache::IsInlineable(method);
	}

	void TransformContext::TransformBody(int32_t depth, int32_t localVarOffset, interpreter::InterpMethodInfo& result)
	{
		godDomainFastPathKindOverride = 0;
		godDomainFastPathParamOverride = 0;
		if (depth == 0 && TryBuildGodDomainStaticF4LoopMethod(localVarOffset))
		{
			BuildInterpMethodInfo(result);
			return;
		}
		// ParamInt / ReturnVector3: ClassFromName during lazy transform is fragile; whole-loop kernels in TryExecuteHotc233CallFastPath handle them.
		// SetTransform: GodDomain whole-method shell disabled (UnityEngine.Object lifecycle).
		if (depth == 0 && TryBuildGodDomainArrayOpLoopMethod(localVarOffset))
		{
			BuildInterpMethodInfo(result);
			return;
		}
		if (depth == 0 && TryBuildGodDomainQuaternionLoopMethod(localVarOffset))
		{
			BuildInterpMethodInfo(result);
			return;
		}
		if (depth == 0 && TryBuildGodDomainGameObjectCreateDestroyLoopMethod(localVarOffset))
		{
			BuildInterpMethodInfo(result);
			return;
		}
		if (depth == 0 && TryBuildGodDomainUnityKernelMethod(localVarOffset))
		{
			BuildInterpMethodInfo(result);
			return;
		}
		TransformBodyImpl(depth, localVarOffset);
		BuildInterpMethodInfo(result);
	}

	void TransformContext::TransformBodyImpl(int32_t depth, int32_t localVarOffset)
	{
#pragma region header

		const Il2CppGenericContext* genericContext = methodInfo->is_inflated ? &methodInfo->genericMethod->context : nullptr;
		const Il2CppGenericContainer* klassContainer = GetGenericContainerFromIl2CppType(&methodInfo->klass->byval_arg);
		const Il2CppGenericContainer* methodContainer = methodInfo->is_inflated ?
			(const Il2CppGenericContainer*)methodInfo->genericMethod->methodDefinition->genericContainerHandle :
			(const Il2CppGenericContainer*)methodInfo->genericContainerHandle;

		BasicBlockSpliter bbc(body);
		bbc.SplitBasicBlocks();


		splitOffsets = bbc.GetSplitOffsets();

		ip2bb = pool.NewNAny<IRBasicBlock*>(body.codeSize + 1);
		uint32_t lastSplitBegin = 0;

		for (uint32_t offset : splitOffsets)
		{
			IRBasicBlock* bb = pool.NewAny<IRBasicBlock>();
			bb->visited = false;
			bb->ilOffset = lastSplitBegin;
			irbbs.push_back(bb);
			for (uint32_t idx = lastSplitBegin; idx < offset; idx++)
			{
				ip2bb[idx] = bb;
			}
			lastSplitBegin = offset;
		}
		IRBasicBlock* endBb = pool.NewAny<IRBasicBlock>();
		*endBb = { true, false, body.codeSize, 0 };
		ip2bb[body.codeSize] = endBb;
		irbbs.push_back(endBb);

		curbb = irbbs[0];

		IL2CPP_ASSERT(lastSplitBegin == body.codeSize);

		bool instanceCall = IsInstanceMethod(methodInfo);
		actualParamCount = methodInfo->parameters_count + instanceCall;

		args = pool.NewNAny<ArgVarInfo>(actualParamCount);
		locals = pool.NewNAny<LocVarInfo>((int)body.localVars.size());
		evalStack = pool.NewNAny<EvalStackVarInfo>(body.maxStack + 100);

		nextFlowIdx = 0;

		totalArgSize = 0;
		{
			int32_t idx = 0;
			if (instanceCall)
			{
				ArgVarInfo& self = args[0];
				self.klass = methodInfo->klass;
				self.type = IS_CLASS_VALUE_TYPE(self.klass) ? &self.klass->this_arg : &self.klass->byval_arg;
				self.argOffset = idx;
				self.argLocOffset = localVarOffset + totalArgSize;
				totalArgSize += GetTypeValueStackObjectCount(self.type);
				idx = 1;
			}

			for (uint32_t i = 0; i < methodInfo->parameters_count; i++)
			{
				ArgVarInfo& arg = args[idx + i];
				arg.type = GET_METHOD_PARAMETER_TYPE(methodInfo->parameters[i]);
				arg.klass = il2cpp::vm::Class::FromIl2CppType(arg.type);
				arg.argOffset = idx + i;
				arg.argLocOffset = localVarOffset + totalArgSize;
				il2cpp::vm::Class::SetupFields(arg.klass);
				totalArgSize += GetTypeValueStackObjectCount(arg.type);
			}
		}

		totalArgLocalSize = totalArgSize;
		for (size_t i = 0; i < body.localVars.size(); i++)
		{
			LocVarInfo& local = locals[i];
			local.type = InflateIfNeeded(body.localVars[i], genericContext, true);
			local.klass = il2cpp::vm::Class::FromIl2CppType(local.type);
			il2cpp::vm::Class::SetupFields(local.klass);
			local.locOffset = localVarOffset + totalArgLocalSize;
			totalArgLocalSize += GetTypeValueStackObjectCount(local.type);
		}

		evalStackBaseOffset = localVarOffset + totalArgLocalSize;
		int32_t totalLocalSize = totalArgLocalSize - totalArgSize;

		maxStackSize = evalStackBaseOffset;
		curStackSize = evalStackBaseOffset;



		ipBase = body.ilcodes;
		ip = body.ilcodes;
		ipOffset = 0;

		evalStackTop = 0;
		prefixFlags = 0;

		int32_t argIdx = 0;
		int32_t varKst = 0;
		int64_t varKst8 = 0;
		int32_t brOffset = 0;

		shareMethod = nullptr;

		Token2RuntimeHandleMap tokenCache(64);

		bool inMethodInlining = depth > 0;

		hotc233::metadata::PDBImage* pdbImage = image->GetPDBImage();
		ir2offsetMap = pdbImage && !inMethodInlining ? new IR2OffsetMap(body.codeSize) : nullptr;

		if (inMethodInlining)
		{
			if (instanceCall)
			{
				if (std::strcmp(methodInfo->name, ".ctor"))
				{
					CreateAddIR(irCheckNull, CheckThrowIfNullVar);
					irCheckNull->obj = args[0].argLocOffset;
				}
			}
			else
			{
				if (!IS_CCTOR_FINISH_OR_NO_CCTOR(methodInfo->klass))
				{
					CreateAddIR(irInitStaticCtor, InitClassStaticCtor);
					irInitStaticCtor->klass = (uint64_t)methodInfo->klass;
				}
			}
		}

		initLocals = (body.flags & (uint32_t)CorILMethodFormat::InitLocals) != 0;
		// init local vars
		if (initLocals && totalLocalSize > 0)
		{
			AddInst(CreateInitLocals(pool, totalLocalSize * sizeof(StackObject), locals[0].locOffset));
		}

		exClauses.resize_initialized(body.exceptionClauses.size());
		int clauseIdx = 0;
		for (ExceptionClause& ec : body.exceptionClauses)
		{
			InterpExceptionClause* iec = &exClauses[clauseIdx++];
			iec->flags = ec.flags;
			iec->tryBeginOffset = ec.tryOffset;
			iec->tryEndOffset = ec.tryOffset + ec.tryLength;
			iec->handlerBeginOffset = ec.handlerOffsets;
			iec->handlerEndOffset = ec.handlerOffsets + ec.handlerLength;
			PushOffset(&iec->tryBeginOffset);
			PushOffset(&iec->tryEndOffset);
			PushOffset(&iec->handlerBeginOffset);
			PushOffset(&iec->handlerEndOffset);
			if (ec.flags == CorILExceptionClauseType::Exception)
			{
				iec->filterBeginOffset = 0;
				iec->exKlass = image->GetClassFromToken(tokenCache, ec.classTokenOrFilterOffset, klassContainer, methodContainer, genericContext);
			}
			else if (ec.flags == CorILExceptionClauseType::Filter)
			{
				iec->filterBeginOffset = ec.classTokenOrFilterOffset;
				PushOffset(&iec->filterBeginOffset);
				iec->exKlass = nullptr;
			}
			else
			{
				IL2CPP_ASSERT(ec.classTokenOrFilterOffset == 0);
				iec->filterBeginOffset = 0;
				iec->exKlass = nullptr;
			}

			switch (ec.flags)
			{
			case CorILExceptionClauseType::Exception:
			{
				IRBasicBlock* bb = ip2bb[iec->handlerBeginOffset];
				IL2CPP_ASSERT(!bb->inPending);
				bb->inPending = true;
				FlowInfo* fi = pool.NewAny<FlowInfo>();
				fi->offset = ec.handlerOffsets;
				fi->curStackSize = evalStackBaseOffset + 1;
				fi->evalStack.push_back({ NATIVE_INT_REDUCE_TYPE, PTR_SIZE, evalStackBaseOffset });
				pendingFlows.push_back(fi);
				break;
			}
			case CorILExceptionClauseType::Filter:
			{
				IRBasicBlock* bb = ip2bb[iec->filterBeginOffset];
				IL2CPP_ASSERT(!bb->inPending);
				bb->inPending = true;
				{
					FlowInfo* fi = pool.NewAny<FlowInfo>();
					IL2CPP_ASSERT(ec.classTokenOrFilterOffset);
					fi->offset = ec.classTokenOrFilterOffset;
					fi->curStackSize = evalStackBaseOffset + 1;
					fi->evalStack.push_back({ NATIVE_INT_REDUCE_TYPE, PTR_SIZE, evalStackBaseOffset });
					pendingFlows.push_back(fi);
				}
				{
					FlowInfo* fi = pool.NewAny<FlowInfo>();
					IL2CPP_ASSERT(ec.handlerOffsets);
					fi->offset = ec.handlerOffsets;
					fi->curStackSize = evalStackBaseOffset + 1;
					fi->evalStack.push_back({ NATIVE_INT_REDUCE_TYPE, PTR_SIZE, evalStackBaseOffset });
					pendingFlows.push_back(fi);
				}

				break;
			}
			case CorILExceptionClauseType::Fault:
			case CorILExceptionClauseType::Finally:
			{
				IRBasicBlock* bb = ip2bb[iec->handlerBeginOffset];
				IL2CPP_ASSERT(!bb->inPending);
				bb->inPending = true;
				FlowInfo* fi = pool.NewAny<FlowInfo>();
				fi->offset = ec.handlerOffsets;
				fi->curStackSize = evalStackBaseOffset;
				pendingFlows.push_back(fi);
				break;
			}
			default:
			{
				RaiseExecutionEngineException("");
			}
			}
		}

#pragma endregion

		IRBasicBlock* lastBb = nullptr;
		for (;;)
		{
			ipOffset = (uint32_t)(ip - ipBase);
			curbb = ip2bb[ipOffset];
			if (curbb != lastBb)
			{
				if (curbb && !curbb->visited)
				{
					curbb->visited = true;
					lastBb = curbb;
				}
				else
				{
					PopBranch();
				}
			}

			switch ((OpcodeValue)*ip)
			{
			case OpcodeValue::NOP:
			{
				ip++;
				continue;
			}
			case OpcodeValue::BREAK:
			{
				ip++;
				continue;
			}
			case OpcodeValue::LDARG_0:
			{
				AddInst_ldarg(0);
				ip++;
				continue;
			}
			case OpcodeValue::LDARG_1:
			{
				AddInst_ldarg(1);
				ip++;
				continue;
			}
			case OpcodeValue::LDARG_2:
			{
				AddInst_ldarg(2);
				ip++;
				continue;
			}
			case OpcodeValue::LDARG_3:
			{
				AddInst_ldarg(3);
				ip++;
				continue;
			}

			case OpcodeValue::LDLOC_0:
			{
				CreateAddInst_ldloc(0);
				ip++;
				continue;
			}
			case OpcodeValue::LDLOC_1:
			{
				CreateAddInst_ldloc(1);
				ip++;
				continue;
			}
			case OpcodeValue::LDLOC_2:
			{
				CreateAddInst_ldloc(2);
				ip++;
				continue;
			}
			case OpcodeValue::LDLOC_3:
			{
				CreateAddInst_ldloc(3);
				ip++;
				continue;
			}
			case OpcodeValue::STLOC_0:
			{
				CreateAddInst_stloc(0);
				ip++;
				continue;
			}
			case OpcodeValue::STLOC_1:
			{
				CreateAddInst_stloc(1);
				ip++;
				continue;
			}
			case OpcodeValue::STLOC_2:
			{
				CreateAddInst_stloc(2);
				ip++;
				continue;
			}
			case OpcodeValue::STLOC_3:
			{
				CreateAddInst_stloc(3);
				ip++;
				continue;
			}
			case OpcodeValue::LDARG_S:
			{
				argIdx = ip[1];
				AddInst_ldarg(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::LDARGA_S:
			{
				argIdx = ip[1];
				AddInst_ldarga(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::STARG_S:
			{
				argIdx = ip[1];
				AddInst_starg(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::LDLOC_S:
			{
				argIdx = ip[1];
				CreateAddInst_ldloc(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::LDLOCA_S:
			{
				argIdx = ip[1];
				CreateAddInst_ldloca(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::STLOC_S:
			{
				argIdx = ip[1];
				CreateAddInst_stloc(argIdx);
				ip += 2;
				continue;
			}
			case OpcodeValue::LDNULL:
			{
				CreateAddIR(ir, LdnullVar);
				ir->dst = curStackSize;
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_M1:
			{
				CreateAddInst_ldc4(-1, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_0:
			{
				CreateAddInst_ldc4(0, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_1:
			{
				CreateAddInst_ldc4(1, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_2:
			{
				CreateAddInst_ldc4(2, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_3:
			{
				CreateAddInst_ldc4(3, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_4:
			{
				CreateAddInst_ldc4(4, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_5:
			{
				CreateAddInst_ldc4(5, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_6:
			{
				CreateAddInst_ldc4(6, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_7:
			{
				CreateAddInst_ldc4(7, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_8:
			{
				CreateAddInst_ldc4(8, EvalStackReduceDataType::I4);
				ip++;
				continue;
			}
			case OpcodeValue::LDC_I4_S:
			{
				varKst = GetI1(ip + 1);
				CreateAddInst_ldc4(varKst, EvalStackReduceDataType::I4);
				ip += 2;
				continue;
			}
			case OpcodeValue::LDC_I4:
			{
				varKst = GetI4LittleEndian(ip + 1);
				CreateAddInst_ldc4(varKst, EvalStackReduceDataType::I4);
				ip += 5;
				continue;
			}
			case OpcodeValue::LDC_I8:
			{
				varKst8 = GetI8LittleEndian(ip + 1);
				CreateAddInst_ldc8(varKst8, EvalStackReduceDataType::I8);
				ip += 9;
				continue;
			}
			case OpcodeValue::LDC_R4:
			{
				varKst = GetI4LittleEndian(ip + 1);
				CreateAddInst_ldc4(varKst, EvalStackReduceDataType::R4);
				ip += 5;
				continue;
			}
			case OpcodeValue::LDC_R8:
			{
				varKst8 = GetI8LittleEndian(ip + 1);
				CreateAddInst_ldc8(varKst8, EvalStackReduceDataType::R8);
				ip += 9;
				continue;
			}
			case OpcodeValue::DUP:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& __eval = evalStack[evalStackTop - 1];
				IRCommon* ir = CreateAssignVarVar(pool, GetEvalStackNewTopOffset(), __eval.locOffset, __eval.byteSize);
				AddInst(ir);
				DuplicateStack();
				ip++;
				continue;
			}
			case OpcodeValue::POP:
			{
				PopStack();
				ip++;
				continue;
			}
			case OpcodeValue::JMP:
			{
				/*  auto& x = ir.jump;
					x.type = IRType::Jmp;
					x.methodToken = GetI4LittleEndian(ip + 1);
					irs.push_back(ir);
					ip += 5;*/
				RaiseNotSupportedException("not support jmp");
				continue;
			}
			case OpcodeValue::CALL:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				ip += 5;
				shareMethod = const_cast<MethodInfo*>(image->GetMethodInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(shareMethod);
			}

		LabelCall:
			{
				if (TryAddInstinctInstruments(shareMethod))
				{
					continue;
				}

#if HOTC233_UNITY_2021_OR_NEW
				if (!shareMethod->has_full_generic_sharing_signature)
#endif
				{
					if (!InitAndGetInterpreterDirectlyCallMethodPointer(shareMethod))
					{
						RaiseAOTGenericMethodNotInstantiatedException(shareMethod);
					}
				}

				bool resolvedIsInstanceMethod = IsInstanceMethod(shareMethod);
				int32_t resolvedTotalArgNum = shareMethod->parameters_count + resolvedIsInstanceMethod;
				int32_t needDataSlotNum = (resolvedTotalArgNum + 3) / 4;
				int32_t callArgEvalStackIdxBase = evalStackTop - resolvedTotalArgNum;
				if (!resolvedIsInstanceMethod
					&& shareMethod->parameters_count == 1
					&& std::strcmp(shareMethod->name, "GetTypeFromHandle") == 0
					&& shareMethod->klass
					&& std::strcmp(shareMethod->klass->namespaze, "System") == 0
					&& std::strcmp(shareMethod->klass->name, "Type") == 0)
				{
					IRCommon* last = GetLastInstrument();
					if (last && last->type == HiOpcodeEnum::LdtokenVar)
					{
						IRLdtokenVar* ldtoken = (IRLdtokenVar*)last;
						uint16_t argOffset = (uint16_t)GetEvalStackOffset(callArgEvalStackIdxBase);
						if (ldtoken->runtimeHandle == argOffset)
						{
							const Il2CppType* typeHandle = (const Il2CppType*)resolveDatas[ldtoken->token];
							Il2CppObject* typeObject = (Il2CppObject*)il2cpp::vm::Reflection::GetTypeObject(typeHandle);
							IRLdtokenTypeObjectVar* typeLoad = (IRLdtokenTypeObjectVar*)last;
							typeLoad->type = HiOpcodeEnum::LdtokenTypeObjectVar;
							typeLoad->ret = ldtoken->runtimeHandle;
							typeLoad->typeObject = GetOrAddResolveDataIndex(typeObject);
							PopStackN(resolvedTotalArgNum);
							PushStackByType(shareMethod->return_type);
							IL2CPP_ASSERT(GetEvalStackTopOffset() == typeLoad->ret);
							continue;
						}
					}
				}
				uint32_t methodDataIndex = GetOrAddResolveDataIndex(shareMethod);

				if (hotc233::metadata::IsInterpreterImplement(shareMethod))
				{
					uint16_t argBaseOffset = (uint16_t)GetEvalStackOffset(callArgEvalStackIdxBase);

					if (hotc233::metadata::IsPInvokeMethod(shareMethod->flags))
					{
						Il2CppCallConvention callingConvention;
						Il2CppMethodPointer pinvokeMethodPointer = ResolvePInvokeMethod(shareMethod, callingConvention);
						if (!pinvokeMethodPointer)
						{
							TEMP_FORMAT(errMsg, "resolve PInvoke method fail. %s.%s::%s", methodInfo->klass->namespaze, methodInfo->klass->name, methodInfo->name);
							RaiseExecutionEngineException(errMsg);
						}
						Managed2NativeFunctionPointerCallMethod managed2NativeFunctionPointerMethod = InterpreterModule::GetManaged2NativeFunctionPointerMethodPointer(shareMethod, callingConvention);
						uint32_t pinvokeMethodPointerIdx = GetOrAddResolveDataIndex((void*)pinvokeMethodPointer);
						uint32_t managed2NativeFunctionPointerMethodIdx = GetOrAddResolveDataIndex((void*)managed2NativeFunctionPointerMethod);

						int32_t argIdxDataIndex;
						uint16_t* __argIdxs;
						AllocResolvedData(resolveDatas, needDataSlotNum, argIdxDataIndex, __argIdxs);

						IL2CPP_ASSERT(!resolvedIsInstanceMethod);

						for (uint8_t i = 0; i < shareMethod->parameters_count; i++)
						{
							int32_t curArgIdx = i;
							__argIdxs[curArgIdx] = evalStack[callArgEvalStackIdxBase + curArgIdx].locOffset;
						}
						if (IsReturnVoidMethod(shareMethod))
						{
							CreateAddIR(ir, CallPInvoke_void);
							ir->pinvokeMethodPointer = pinvokeMethodPointerIdx;
							ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodIdx;
							ir->argIdxs = argIdxDataIndex;
						}
						else
						{
							interpreter::LocationDataType locDataType = GetLocationDataTypeByType(shareMethod->return_type);
							if (interpreter::IsNeedExpandLocationType(locDataType))
							{
								CreateAddIR(ir, CallPInvoke_ret_expand);
								ir->pinvokeMethodPointer = pinvokeMethodPointerIdx;
								ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodIdx;
								ir->argIdxs = argIdxDataIndex;
								ir->ret = argBaseOffset;
								ir->retLocationType = (uint8_t)locDataType;
							}
							else
							{
								CreateAddIR(ir, CallPInvoke_ret);
								ir->pinvokeMethodPointer = pinvokeMethodPointerIdx;
								ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodIdx;
								ir->argIdxs = argIdxDataIndex;
								ir->ret = argBaseOffset;
							}
						}
					}
					else if (ShouldBeInlined(shareMethod, depth) && TransformSubMethodBody(*this, shareMethod, depth + 1, argBaseOffset))
					{

					}
					else
					{
						if (IsReturnVoidMethod(shareMethod))
						{
							if (resolvedIsInstanceMethod)
							{
								CreateAddIR(ir, CallInterp_void);
								ir->methodInfo = methodDataIndex;
								ir->argBase = argBaseOffset;
							}
							else
							{
								CreateAddIR(ir, CallInterpStatic_void);
								ir->methodInfo = methodDataIndex;
								ir->argBase = argBaseOffset;
							}
						}
						else
						{
							if (resolvedIsInstanceMethod)
							{
								CreateAddIR(ir, CallInterp_ret);
								ir->isInstanceMethod = 1;
								ir->methodInfo = methodDataIndex;
								ir->argBase = argBaseOffset;
								ir->ret = argBaseOffset;
							}
							else
							{
								int32_t interpMethodCacheDataIdx = 0;
								uint64_t* interpMethodCache = nullptr;
								AllocResolvedData(resolveDatas, 1, interpMethodCacheDataIdx, interpMethodCache);
								interpMethodCache[0] = 0;
								CreateAddIR(ir, CallInterpStatic_ret);
								ir->methodInfo = methodDataIndex;
								ir->argBase = argBaseOffset;
								ir->ret = argBaseOffset;
								ir->interpMethodCache = interpMethodCacheDataIdx;
							}
						}
					}
					PopStackN(resolvedTotalArgNum);
					if (!IsReturnVoidMethod(shareMethod))
					{
						PushStackByType(shareMethod->return_type);
					}
					continue;
				}
				// AOT (native) callee: fault-in its cold code page now (off the timed
				// first-call path). See HOTC233_ENABLE_AOT_CODE_PRETOUCH rationale.
				PreTouchNativeCalleeCode(shareMethod);

				if (TryAddCallCommonInstruments(shareMethod, methodDataIndex))
				{
					continue;
				}



				Managed2NativeCallMethod managed2NativeMethod = InterpreterModule::GetManaged2NativeMethodPointer(shareMethod, false);
				IL2CPP_ASSERT(managed2NativeMethod);
				uint32_t managed2NativeMethodDataIdx = GetOrAddResolveDataIndex((void*)managed2NativeMethod);

				int32_t argIdxDataIndex;
				uint16_t* __argIdxs;
				AllocResolvedData(resolveDatas, needDataSlotNum, argIdxDataIndex, __argIdxs);

				if (resolvedIsInstanceMethod)
				{
					__argIdxs[0] = GetEvalStackOffset(callArgEvalStackIdxBase);
				}

				for (uint8_t i = 0; i < shareMethod->parameters_count; i++)
				{
					int32_t curArgIdx = i + resolvedIsInstanceMethod;
					__argIdxs[curArgIdx] = evalStack[callArgEvalStackIdxBase + curArgIdx].locOffset;
				}

				PopStackN(resolvedTotalArgNum);

				if (!IsReturnVoidMethod(shareMethod))
				{
					PushStackByType(shareMethod->return_type);
					interpreter::LocationDataType locDataType = GetLocationDataTypeByType(shareMethod->return_type);
					if (interpreter::IsNeedExpandLocationType(locDataType))
					{
						CreateAddIR(ir, CallNativeInstance_ret_expand);
						ir->type = resolvedIsInstanceMethod ? HiOpcodeEnum::CallNativeInstance_ret_expand : HiOpcodeEnum::CallNativeStatic_ret_expand;
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->methodInfo = methodDataIndex;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = GetEvalStackTopOffset();
						ir->retLocationType = (uint8_t)locDataType;
					}
					else
					{
						CreateAddIR(ir, CallNativeInstance_ret);
						ir->type = resolvedIsInstanceMethod ? HiOpcodeEnum::CallNativeInstance_ret : HiOpcodeEnum::CallNativeStatic_ret;
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->methodInfo = methodDataIndex;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = GetEvalStackTopOffset();
					}
				}
				else
				{
					CreateAddIR(ir, CallNativeInstance_void);
					ir->type = resolvedIsInstanceMethod ? HiOpcodeEnum::CallNativeInstance_void : HiOpcodeEnum::CallNativeStatic_void;
					ir->managed2NativeMethod = managed2NativeMethodDataIdx;
					ir->methodInfo = methodDataIndex;
					ir->argIdxs = argIdxDataIndex;
				}
				continue;
			}
			case OpcodeValue::CALLVIRT:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				ip += 5;
				shareMethod = image->GetMethodInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
			}
		LabelCallVir:
			{
				IL2CPP_ASSERT(shareMethod);
				IL2CPP_ASSERT(hotc233::metadata::IsInstanceMethod(shareMethod));
				if ((!metadata::IsVirtualMethod(shareMethod->flags)) || metadata::IsSealed(shareMethod->flags))
				{
					goto LabelCall;
				}

				int32_t resolvedTotalArgNum = shareMethod->parameters_count + 1;
				int32_t callArgEvalStackIdxBase = evalStackTop - resolvedTotalArgNum;
				uint32_t methodDataIndex = GetOrAddResolveDataIndex(shareMethod);

				bool isMultiDelegate = IsChildTypeOfMulticastDelegate(shareMethod->klass);
				if (!isMultiDelegate && IsInterpreterMethod(shareMethod) && !IsInterface(shareMethod->klass->flags))
				{
					PopStackN(resolvedTotalArgNum);

					uint16_t argBaseOffset = (uint16_t)GetEvalStackOffset(callArgEvalStackIdxBase);
					if (IsReturnVoidMethod(shareMethod))
					{
						CreateAddIR(ir, CallInterpVirtual_void);
						ir->method = methodDataIndex;
						ir->argBase = argBaseOffset;
					}
					else
					{
						CreateAddIR(ir, CallInterpVirtual_ret);
						ir->method = methodDataIndex;
						ir->argBase = argBaseOffset;
						ir->ret = argBaseOffset;
						PushStackByType(shareMethod->return_type);
					}
					continue;
				}

				Managed2NativeCallMethod managed2NativeMethod = InterpreterModule::GetManaged2NativeMethodPointer(shareMethod, false);
				IL2CPP_ASSERT(managed2NativeMethod);
				uint32_t managed2NativeMethodDataIdx = GetOrAddResolveDataIndex((void*)managed2NativeMethod);


				int32_t needDataSlotNum = (resolvedTotalArgNum + 3) / 4;
				int32_t argIdxDataIndex;
				uint16_t* __argIdxs;
				AllocResolvedData(resolveDatas, needDataSlotNum, argIdxDataIndex, __argIdxs);

				__argIdxs[0] = GetEvalStackOffset(callArgEvalStackIdxBase);
				for (uint8_t i = 0; i < shareMethod->parameters_count; i++)
				{
					int32_t curArgIdx = i + 1;
					__argIdxs[curArgIdx] = evalStack[callArgEvalStackIdxBase + curArgIdx].locOffset;
				}

				PopStackN(resolvedTotalArgNum);

				const Il2CppType* returnType = shareMethod->return_type;
				int32_t retIdx;

				if (returnType->type != IL2CPP_TYPE_VOID)
				{
					PushStackByType(returnType);
					retIdx = GetEvalStackTopOffset();
				}
				else
				{
					retIdx = -1;
				}
				if (isMultiDelegate)
				{
					if (std::strcmp(shareMethod->name, "Invoke") == 0)
					{
						Managed2NativeCallMethod staticManaged2NativeMethod = InterpreterModule::GetManaged2NativeMethodPointer(shareMethod, true);
						IL2CPP_ASSERT(staticManaged2NativeMethod);
						uint32_t staticManaged2NativeMethodDataIdx = GetOrAddResolveDataIndex((void*)staticManaged2NativeMethod);
						if (retIdx < 0)
						{
							int32_t interpDelegateCacheDataIdx = 0;
							uint64_t* interpDelegateCache = nullptr;
							AllocResolvedData(resolveDatas, 2, interpDelegateCacheDataIdx, interpDelegateCache);
							interpDelegateCache[0] = 0;
							interpDelegateCache[1] = 0;
							CreateAddIR(ir, CallDelegateInvoke_void);
							ir->managed2NativeStaticMethod = staticManaged2NativeMethodDataIdx;
							ir->managed2NativeInstanceMethod = managed2NativeMethodDataIdx;
							ir->argIdxs = argIdxDataIndex;
							ir->invokeParamCount = shareMethod->parameters_count;
							ir->interpDelegateCache = interpDelegateCacheDataIdx;
						}
						else
						{
							interpreter::TypeDesc retDesc = GetTypeArgDesc(returnType);
							int32_t interpDelegateCacheDataIdx = 0;
							uint64_t* interpDelegateCache = nullptr;
							AllocResolvedData(resolveDatas, 2, interpDelegateCacheDataIdx, interpDelegateCache);
							interpDelegateCache[0] = 0;
							interpDelegateCache[1] = 0;
							if (IsNeedExpandLocationType(retDesc.type))
							{
								CreateAddIR(ir, CallDelegateInvoke_ret_expand);
								ir->managed2NativeStaticMethod = staticManaged2NativeMethodDataIdx;
								ir->managed2NativeInstanceMethod = managed2NativeMethodDataIdx;
								ir->argIdxs = argIdxDataIndex;
								ir->ret = retIdx;
								ir->invokeParamCount = shareMethod->parameters_count;
								ir->retLocationType = (uint8_t)retDesc.type;
								ir->interpDelegateCache = interpDelegateCacheDataIdx;
							}
							else
							{
								CreateAddIR(ir, CallDelegateInvoke_ret);
								ir->managed2NativeStaticMethod = staticManaged2NativeMethodDataIdx;
								ir->managed2NativeInstanceMethod = managed2NativeMethodDataIdx;
								ir->argIdxs = argIdxDataIndex;
								ir->ret = retIdx;
								ir->retTypeStackObjectSize = retDesc.stackObjectSize;
								ir->invokeParamCount = shareMethod->parameters_count;
								ir->interpDelegateCache = interpDelegateCacheDataIdx;
							}
						}
						continue;
					}
					Il2CppMethodPointer directlyCallMethodPointer = InitAndGetInterpreterDirectlyCallMethodPointer(shareMethod);
					if (std::strcmp(shareMethod->name, "BeginInvoke") == 0)
					{
						if (IsInterpreterMethod(shareMethod) || directlyCallMethodPointer == nullptr)
						{
							CreateAddIR(ir, CallDelegateBeginInvoke);
							ir->methodInfo = methodDataIndex;
							ir->result = retIdx;
							ir->argIdxs = argIdxDataIndex;
							continue;
						}
					}
					else if (std::strcmp(shareMethod->name, "EndInvoke") == 0)
					{
						if (IsInterpreterMethod(shareMethod) || directlyCallMethodPointer == nullptr)
						{
							if (retIdx < 0)
							{
								CreateAddIR(ir, CallDelegateEndInvoke_void);
								ir->methodInfo = methodDataIndex;
								ir->asyncResult = __argIdxs[1];
							}
							else
							{
								CreateAddIR(ir, CallDelegateEndInvoke_ret);
								ir->methodInfo = methodDataIndex;
								ir->asyncResult = __argIdxs[1];
								ir->ret = retIdx;
							}
							continue;
						}
					}
				}

				if (retIdx < 0)
				{
					CreateAddIR(ir, CallVirtual_void);
					ir->managed2NativeMethod = managed2NativeMethodDataIdx;
					ir->methodInfo = methodDataIndex;
					ir->argIdxs = argIdxDataIndex;
				}
				else
				{
					interpreter::LocationDataType locDataType = GetLocationDataTypeByType(returnType);
					if (IsNeedExpandLocationType(locDataType))
					{
						CreateAddIR(ir, CallVirtual_ret_expand);
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->methodInfo = methodDataIndex;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = retIdx;
						ir->retLocationType = (uint8_t)locDataType;
					}
					else
					{
						CreateAddIR(ir, CallVirtual_ret);
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->methodInfo = methodDataIndex;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = retIdx;
					}
				}
				continue;
			}
			case OpcodeValue::CALLI:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);

				ResolveStandAloneMethodSig methodSig;
				image->GetStandAloneMethodSigFromToken(token, klassContainer, methodContainer, genericContext, methodSig);
				if (IsPrologExplicitThis(methodSig.flags))
				{
					RaiseNotSupportedException("not support StandAloneMethodSig flags:EXPLICITTHIS");
				}

				int32_t methodIdx = GetEvalStackTopOffset();
				//uint32_t methodDataIndex = GetOrAddResolveDataIndex(shareMethod);
				Managed2NativeCallMethod managed2NativeMethod = InterpreterModule::GetManaged2NativeMethodPointer(methodSig);
				Managed2NativeFunctionPointerCallMethod managed2NativeFunctionPointerMethod = InterpreterModule::GetManaged2NativeFunctionPointerMethodPointer(methodSig);
				IL2CPP_ASSERT(managed2NativeMethod);
				uint32_t managed2NativeMethodDataIdx = GetOrAddResolveDataIndex((void*)managed2NativeMethod);
				uint32_t managed2NativeFunctionPointerMethodDataIdx = GetOrAddResolveDataIndex((void*)managed2NativeFunctionPointerMethod);
				bool hasThis = metadata::IsPrologHasThis(methodSig.flags);

				int32_t resolvedTotalArgNum = (int32_t)methodSig.params.size() + hasThis;
				int32_t needDataSlotNum = (resolvedTotalArgNum + 3) / 4;
				int32_t argIdxDataIndex;
				uint16_t* __argIdxs;

				// we need at least one slot for argBasePtr when resolvedTotalArgNum == 0
				AllocResolvedData(resolveDatas, std::max(needDataSlotNum, 1), argIdxDataIndex, __argIdxs);

				int32_t callArgEvalStackIdxBase = evalStackTop - resolvedTotalArgNum - 1 /*funtion ptr*/;

				// CallInd need know the argBasePtr when resolvedTotalArgNum == 0
				if (needDataSlotNum == 0)
				{
					__argIdxs[0] = evalStack[callArgEvalStackIdxBase].locOffset;
				}

				if (hasThis)
				{
					__argIdxs[0] = evalStack[callArgEvalStackIdxBase].locOffset;
				}

				for (size_t i = 0; i < methodSig.params.size(); i++)
				{
					size_t curArgIdx = i + hasThis;
					__argIdxs[curArgIdx] = evalStack[callArgEvalStackIdxBase + curArgIdx].locOffset;
				}

				PopStackN(resolvedTotalArgNum + 1);

				if (!IsVoidType(methodSig.returnType))
				{
					PushStackByType(methodSig.returnType);
					interpreter::LocationDataType locDataType = GetLocationDataTypeByType(methodSig.returnType);
					if (interpreter::IsNeedExpandLocationType(locDataType))
					{
						CreateAddIR(ir, CallInd_ret_expand);
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodDataIdx;
						ir->methodInfo = methodIdx;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = GetEvalStackTopOffset();
						ir->retLocationType = (uint8_t)locDataType;
					}
					else
					{
						CreateAddIR(ir, CallInd_ret);
						ir->managed2NativeMethod = managed2NativeMethodDataIdx;
						ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodDataIdx;
						ir->methodInfo = methodIdx;
						ir->argIdxs = argIdxDataIndex;
						ir->ret = GetEvalStackTopOffset();
					}
				}
				else
				{
					CreateAddIR(ir, CallInd_void);
					ir->managed2NativeMethod = managed2NativeMethodDataIdx;
					ir->managed2NativeFunctionPointerMethod = managed2NativeFunctionPointerMethodDataIdx;
					ir->methodInfo = methodIdx;
					ir->argIdxs = argIdxDataIndex;
				}

				ip += 5;
				continue;
			}
			case OpcodeValue::RET:
			{
				bool isVoidReturnType = methodInfo->return_type->type == IL2CPP_TYPE_VOID;
				if (inMethodInlining)
				{
					if (!isVoidReturnType)
					{
						uint16_t retVarIdx = GetEvalStackTopOffset();
						if (retVarIdx != localVarOffset)
						{
							IRCommon* ir = CreateAssignVarVar(pool, localVarOffset, retVarIdx, GetTypeValueSize(methodInfo->return_type));
							AddInst(ir);
						}
					}
				}
				else if (isVoidReturnType)
				{
					CreateAddIR(ir, RetVar_void);
				}
				else
				{
					// ms.ret = nullptr;
					IL2CPP_ASSERT(evalStackTop == 1);
					int32_t size = GetTypeValueSize(methodInfo->return_type);
					switch (size)
					{
					case 1:
					{
						CreateAddIR(ir, RetVar_ret_1);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 2:
					{
						CreateAddIR(ir, RetVar_ret_2);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 4:
					{
						CreateAddIR(ir, RetVar_ret_4);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 8:
					{
						CreateAddIR(ir, RetVar_ret_8);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 12:
					{
						CreateAddIR(ir, RetVar_ret_12);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 16:
					{
						CreateAddIR(ir, RetVar_ret_16);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 20:
					{
						CreateAddIR(ir, RetVar_ret_20);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 24:
					{
						CreateAddIR(ir, RetVar_ret_24);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 28:
					{
						CreateAddIR(ir, RetVar_ret_28);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					case 32:
					{
						CreateAddIR(ir, RetVar_ret_32);
						ir->ret = GetEvalStackTopOffset();
						break;
					}
					default:
					{
						CreateAddIR(ir, RetVar_ret_n);
						ir->ret = GetEvalStackTopOffset();
						ir->size = size;
						break;
					}
					}
				}
				ip++;
				PopBranch();
				continue;
			}
			case OpcodeValue::BR_S:
			{
				brOffset = GetI1(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 2;
					CreateAddIR(ir, BranchUncondition_4);
					ir->offset = targetOffset;
					PushOffset(&ir->offset);

					PushBranch(targetOffset);
					PopBranch();
				}
				else
				{
					ip += 2;
				}
				continue;
			}
			case OpcodeValue::LEAVE_S:
			{
				brOffset = GetI1(ip + 1);
				int32_t targetOffset = ipOffset + brOffset + 2;
				Add_leave((uint32_t)targetOffset);
				PopBranch();
				continue;
			}
			case OpcodeValue::BRFALSE_S:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				brOffset = GetI1(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 2;
					Add_brtruefalse(false, targetOffset);
				}
				else
				{
					PopStack();
				}
				ip += 2;
				continue;
			}
			case OpcodeValue::BRTRUE_S:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				brOffset = GetI1(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 2;
					Add_brtruefalse(true, targetOffset);
				}
				else
				{
					PopStack();
				}
				ip += 2;
				continue;
			}
			case OpcodeValue::BEQ_S:
			{
				CI_branch1(Ceq);
				continue;
			}
			case OpcodeValue::BGE_S:
			{
				CI_branch1(Cge);
				continue;
			}
			case OpcodeValue::BGT_S:
			{
				CI_branch1(Cgt);
				continue;
			}
			case OpcodeValue::BLE_S:
			{
				CI_branch1(Cle);
				continue;
			}
			case OpcodeValue::BLT_S:
			{
				CI_branch1(Clt);
				continue;
			}
			case OpcodeValue::BNE_UN_S:
			{
				CI_branch1(CneUn);
				continue;
			}
			case OpcodeValue::BGE_UN_S:
			{
				CI_branch1(CgeUn);
				continue;
			}
			case OpcodeValue::BGT_UN_S:
			{
				CI_branch1(CgtUn);
				continue;
			}
			case OpcodeValue::BLE_UN_S:
			{
				CI_branch1(CleUn);
				continue;
			}
			case OpcodeValue::BLT_UN_S:
			{
				CI_branch1(CltUn);
				continue;
			}
			case OpcodeValue::BR:
			{
				brOffset = GetI4LittleEndian(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 5;
					CreateAddIR(ir, BranchUncondition_4);
					ir->offset = targetOffset;
					PushOffset(&ir->offset);

					PushBranch(targetOffset);
					PopBranch();
				}
				else
				{
					ip += 5;
				}
				continue;
			}
			case OpcodeValue::LEAVE:
			{
				brOffset = GetI4LittleEndian(ip + 1);
				int32_t targetOffset = ipOffset + brOffset + 5;
				Add_leave((uint32_t)targetOffset);
				PopBranch();
				continue;
			}
			case OpcodeValue::BRFALSE:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				brOffset = GetI4LittleEndian(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 5;
					Add_brtruefalse(false, targetOffset);
				}
				else
				{
					PopStack();
				}
				ip += 5;
				continue;
			}
			case OpcodeValue::BRTRUE:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				brOffset = GetI4LittleEndian(ip + 1);
				if (brOffset != 0)
				{
					int32_t targetOffset = ipOffset + brOffset + 5;
					Add_brtruefalse(true, targetOffset);
				}
				else
				{
					PopStack();
				}
				ip += 5;
				continue;
			}

			case OpcodeValue::BEQ:
			{
				CI_branch4(Ceq);
				continue;
			}
			case OpcodeValue::BGE:
			{
				CI_branch4(Cge);
				continue;
			}
			case OpcodeValue::BGT:
			{
				CI_branch4(Cgt);
				continue;
			}
			case OpcodeValue::BLE:
			{
				CI_branch4(Cle);
				continue;
			}
			case OpcodeValue::BLT:
			{
				CI_branch4(Clt);
				continue;
			}
			case OpcodeValue::BNE_UN:
			{
				CI_branch4(CneUn);
				continue;
			}
			case OpcodeValue::BGE_UN:
			{
				CI_branch4(CgeUn);
				continue;
			}
			case OpcodeValue::BGT_UN:
			{
				CI_branch4(CgtUn);
				continue;
			}
			case OpcodeValue::BLE_UN:
			{
				CI_branch4(CleUn);
				continue;
			}
			case OpcodeValue::BLT_UN:
			{
				CI_branch4(CltUn);
				continue;
			}
			case OpcodeValue::SWITCH:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				CreateIR(ir, BranchSwitch);

				uint32_t switchValue = GetEvalStackTopOffset();
				uint32_t n = (uint32_t)GetI4LittleEndian(ip + 1);
				ir->value = GetEvalStackTopOffset();
				ir->caseNum = n;

				int32_t* caseOffsets;
				AllocResolvedData(resolveDatas, (n + 1) / 2, *(int32_t*)&ir->caseOffsets, caseOffsets);
				PopStack();

				uint32_t instrSize = 1 + (n + 1) * 4;
				const byte* caseOffsetIp = ip + 5;

				// remove this instrument if all target is same to default.
				uint32_t nextInstrumentOffset = ipOffset + instrSize;
				bool anyNotDefaultCase = false;
				for (uint32_t caseIdx = 0; caseIdx < n; caseIdx++)
				{
					int32_t targetOffset = (int32_t)(nextInstrumentOffset + GetI4LittleEndian(caseOffsetIp + caseIdx * 4));
					caseOffsets[caseIdx] = targetOffset;
					//PushOffset(caseOffsets + caseIdx);
					if (targetOffset != nextInstrumentOffset)
					{
						anyNotDefaultCase = true;
						PushBranch(targetOffset);
					}
				}
				if (anyNotDefaultCase)
				{
					switchOffsetsInResolveData.push_back({ ir->caseOffsets, n });
					AddInst(ir);
				}
				ip += instrSize;
				continue;
			}
			case OpcodeValue::LDIND_I1:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_i1, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_U1:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_u1, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_I2:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_i2, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_U2:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_u2, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_I4:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_i4, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_U4:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_u4, EvalStackReduceDataType::I4);
				continue;
			}
			case OpcodeValue::LDIND_I8:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_i8, EvalStackReduceDataType::I8);
				continue;
			}
			case OpcodeValue::LDIND_I:
			{
				Add_ldind(ARCH_ARGUMENT(HiOpcodeEnum::LdindVarVar_i4, HiOpcodeEnum::LdindVarVar_i8), NATIVE_INT_REDUCE_TYPE);
				continue;
			}
			case OpcodeValue::LDIND_R4:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_f4, EvalStackReduceDataType::R4);
				continue;
			}
			case OpcodeValue::LDIND_R8:
			{
				Add_ldind(HiOpcodeEnum::LdindVarVar_f8, EvalStackReduceDataType::R8);
				continue;
			}
			case OpcodeValue::LDIND_REF:
			{
				Add_ldind(ARCH_ARGUMENT(HiOpcodeEnum::LdindVarVar_i4, HiOpcodeEnum::LdindVarVar_i8), NATIVE_INT_REDUCE_TYPE);
				continue;
			}
			case OpcodeValue::STIND_REF:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_ref);
				continue;
			}
			case OpcodeValue::STIND_I1:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_i1);
				continue;
			}
			case OpcodeValue::STIND_I2:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_i2);
				continue;
			}
			case OpcodeValue::STIND_I4:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_i4);
				continue;
			}
			case OpcodeValue::STIND_I8:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_i8);
				continue;
			}
			case OpcodeValue::STIND_R4:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_f4);
				continue;
			}
			case OpcodeValue::STIND_R8:
			{
				Add_stind(HiOpcodeEnum::StindVarVar_f8);
				continue;
			}
			case OpcodeValue::ADD:
			{
				CI_binOp(Add);
				continue;
			}
			case OpcodeValue::SUB:
			{
				CI_binOp(Sub);
				continue;
			}
			case OpcodeValue::MUL:
			{
				CI_binOp(Mul);
				continue;
			}
			case OpcodeValue::DIV:
			{
				CI_binOp(Div);
				continue;
			}
			case OpcodeValue::DIV_UN:
			{
				CI_binOpUn(DivUn);
				continue;
			}
			case OpcodeValue::REM:
			{
				CI_binOp(Rem);
				continue;
			}
			case OpcodeValue::REM_UN:
			{
				CI_binOpUn(RemUn);
				continue;
			}
			case OpcodeValue::AND:
			{
				CI_binOpUn(And);
				continue;
			}
			case OpcodeValue::OR:
			{
				CI_binOpUn(Or);
				continue;
			}
			case OpcodeValue::XOR:
			{
				CI_binOpUn(Xor);
				continue;
			}
			case OpcodeValue::SHL:
			{
				CI_binOpShift(Shl);
				continue;
			}
			case OpcodeValue::SHR:
			{
				CI_binOpShift(Shr);
				continue;
			}
			case OpcodeValue::SHR_UN:
			{
				CI_binOpShift(ShrUn);
				continue;
			}
			case OpcodeValue::NEG:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& op = evalStack[evalStackTop - 1];
				CreateAddIR(ir, UnaryOpVarVar_Neg_i4);
				ir->dst = ir->src = op.locOffset;

				switch (op.reduceType)
				{
				case EvalStackReduceDataType::I4:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Neg_i4;
					break;
				}
				case EvalStackReduceDataType::I8:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Neg_i8;
					break;
				}
				case EvalStackReduceDataType::R4:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Neg_f4;
					break;
				}
				case EvalStackReduceDataType::R8:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Neg_f8;
					break;
				}
				default:
				{
					RaiseExecutionEngineException("NEG not suppport type");
					break;
				}
				}
				ip++;
				continue;
			}
			case OpcodeValue::NOT:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& op = evalStack[evalStackTop - 1];
				CreateAddIR(ir, UnaryOpVarVar_Not_i4);
				ir->dst = ir->src = op.locOffset;

				switch (op.reduceType)
				{
				case EvalStackReduceDataType::I4:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Not_i4;
					break;
				}
				case EvalStackReduceDataType::I8:
				{
					ir->type = HiOpcodeEnum::UnaryOpVarVar_Not_i8;
					break;
				}
				default:
				{
					RaiseExecutionEngineException("NOT not suppport type");
					break;
				}
				}
				ip++;
				continue;
			}
			case OpcodeValue::CONV_I1:
			{
				CI_conv(i1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_I2:
			{
				CI_conv(i2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_I4:
			{
				CI_conv(i4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_I8:
			{
				CI_conv(i8, I8, 8);
				continue;
			}
			case OpcodeValue::CONV_R4:
			{
				CI_conv(f4, R4, 4);
				continue;
			}
			case OpcodeValue::CONV_R8:
			{
				CI_conv(f8, R8, 8);
				continue;
			}
			case OpcodeValue::CONV_U4:
			{
				CI_conv(u4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_U8:
			{
				CI_conv(u8, I8, 8);
				continue;
			}
			case OpcodeValue::CPOBJ:
			{
				IL2CPP_ASSERT(evalStackTop >= 2);
				EvalStackVarInfo& dst = evalStack[evalStackTop - 2];
				EvalStackVarInfo& src = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);
				if (IS_CLASS_VALUE_TYPE(objKlass))
				{
					uint32_t size = GetTypeValueSize(objKlass);
					if (!HOTC233_ENABLE_WRITE_BARRIERS || !objKlass->has_references)
					{
						switch (size)
						{
						case 1:
						{
							CreateAddIR(ir, CpobjVarVar_1);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 2:
						{
							CreateAddIR(ir, CpobjVarVar_2);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 4:
						{
							CreateAddIR(ir, CpobjVarVar_4);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 8:
						{
							CreateAddIR(ir, CpobjVarVar_8);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 12:
						{
							CreateAddIR(ir, CpobjVarVar_12);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 16:
						{
							CreateAddIR(ir, CpobjVarVar_16);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						default:
						{
							CreateAddIR(ir, CpobjVarVar_n_4);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							ir->size = size;
							break;
						}
						}
					}
					else
					{
						CreateAddIR(ir, CpobjVarVar_WriteBarrier_n_4);
						ir->dst = dst.locOffset;
						ir->src = src.locOffset;
						ir->size = size;
					}
				}
				else
				{
					CreateAddIR(ir, CpobjVarVar_ref);
					ir->dst = dst.locOffset;
					ir->src = src.locOffset;
				}

				PopStackN(2);
				ip += 5;
				continue;
			}
			case OpcodeValue::LDOBJ:
			{
				IL2CPP_ASSERT(evalStackTop >= 1);
				EvalStackVarInfo& top = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);
				LocationDescInfo desc = ComputLocationDescInfo(&objKlass->byval_arg);

				switch (desc.type)
				{
				case LocationDescType::I1:
				{
					CreateAddIR(ir, LdindVarVar_i1);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::U1:
				{
					CreateAddIR(ir, LdindVarVar_u1);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::I2:
				{
					CreateAddIR(ir, LdindVarVar_i2);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::U2:
				{
					CreateAddIR(ir, LdindVarVar_u2);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::I4:
				{
					CreateAddIR(ir, LdindVarVar_i4);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::I8:
				{
					CreateAddIR(ir, LdindVarVar_i8);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::Ref:
				{
					CreateAddIR(ir, LdobjVarVar_ref);
					ir->dst = ir->src = top.locOffset;
					break;
				}
				case LocationDescType::S:
				case LocationDescType::StructContainsRef:
				{
					uint32_t size = GetTypeValueSize(objKlass);
					switch (size)
					{
					case 1:
					{
						CreateAddIR(ir, LdobjVarVar_1);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					case 2:
					{
						CreateAddIR(ir, LdobjVarVar_2);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					case 4:
					{
						CreateAddIR(ir, LdobjVarVar_4);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					case 8:
					{
						CreateAddIR(ir, LdobjVarVar_8);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					case 12:
					{
						CreateAddIR(ir, LdobjVarVar_12);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					case 16:
					{
						CreateAddIR(ir, LdobjVarVar_16);
						ir->dst = ir->src = top.locOffset;
						break;
					}
					default:
					{
						CreateAddIR(ir, LdobjVarVar_n_4);
						ir->dst = ir->src = top.locOffset;
						ir->size = size;
						break;
					}
					}
					break;
				}
				default:
				{
					RaiseExecutionEngineException("field");
				}
				}

				PopStack();
				PushStackByType(&objKlass->byval_arg);
				InsertMemoryBarrier();
				ResetPrefixFlags();
				ip += 5;
				continue;
			}
			case OpcodeValue::LDSTR:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppString* str = image->GetIl2CppUserStringFromRawIndex(DecodeTokenRowIndex(token));
				uint32_t dataIdx = GetOrAddResolveDataIndex(str);

				CreateAddIR(ir, LdstrVar);
				ir->dst = GetEvalStackNewTopOffset();
				ir->str = dataIdx;
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

				ip += 5;
				continue;
			}
			case OpcodeValue::NEWOBJ:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				ip += 5;
				// TODO token cache optimistic
				shareMethod = const_cast<MethodInfo*>(image->GetMethodInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(shareMethod);
				IL2CPP_ASSERT(!std::strcmp(shareMethod->name, ".ctor"));
				IL2CPP_ASSERT(hotc233::metadata::IsInstanceMethod(shareMethod));
				if (TryAddInstinctCtorInstruments(shareMethod))
				{
					continue;
				}
				Il2CppClass* klass = shareMethod->klass;
				uint8_t paramCount = shareMethod->parameters_count;
				if (klass == il2cpp_defaults.string_class)
				{
					const MethodInfo* searchMethod = FindRedirectCreateString(shareMethod);
					if (searchMethod)
					{
						// insert nullptr to eval stack
						int32_t thisIdx = evalStackTop - paramCount;
						for (int32_t i = evalStackTop; i > thisIdx; i--)
						{
							evalStack[i] = evalStack[i - 1];
						}
						// locOffset of this is not important. You only need make sure the value is not equal to nullptr.
						evalStack[thisIdx] = { NATIVE_INT_REDUCE_TYPE, PTR_SIZE, GetEvalStackOffset(thisIdx) };
						++evalStackTop;
						shareMethod = searchMethod;
						goto LabelCall;
					}
				}

				if (!InitAndGetInterpreterDirectlyCallMethodPointer(shareMethod))
				{
					RaiseAOTGenericMethodNotInstantiatedException(shareMethod);
				}

				int32_t callArgEvalStackIdxBase = evalStackTop - shareMethod->parameters_count;
				IL2CPP_ASSERT(callArgEvalStackIdxBase >= 0);
				uint16_t objIdx = GetEvalStackOffset(callArgEvalStackIdxBase);

				int32_t resolvedTotalArgNum = shareMethod->parameters_count + 1;

				uint32_t methodDataIndex = GetOrAddResolveDataIndex(shareMethod);

				if (IsInterpreterImplement(shareMethod))
				{
					if (IS_CLASS_VALUE_TYPE(klass))
					{
						CreateAddIR(ir, NewValueTypeInterpVar);
						ir->obj = GetEvalStackOffset(callArgEvalStackIdxBase);
						ir->method = methodDataIndex;
						ir->argBase = ir->obj;
						ir->argStackObjectNum = curStackSize - ir->argBase;
						// IL2CPP_ASSERT(ir->argStackObjectNum > 0); may 0
						PopStackN(shareMethod->parameters_count);
						PushStackByType(&klass->byval_arg);
						ir->ctorFrameBase = GetEvalStackNewTopOffset();
						maxStackSize = std::max(maxStackSize, curStackSize + ir->argStackObjectNum + 1);
					}
					else
					{
						if (shareMethod->parameters_count == 0)
						{
							CreateAddIR(ir, NewClassInterpVar_Ctor_0);
							ir->obj = GetEvalStackNewTopOffset();
							ir->method = methodDataIndex;
							PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
							ir->ctorFrameBase = GetEvalStackNewTopOffset();
							maxStackSize = std::max(maxStackSize, curStackSize + 1); // 1 for __this
						}
						else
						{
							CreateAddIR(ir, NewClassInterpVar);
							ir->obj = GetEvalStackOffset(callArgEvalStackIdxBase);
							ir->method = methodDataIndex;
							ir->argBase = ir->obj;
							ir->argStackObjectNum = curStackSize - ir->argBase;
							IL2CPP_ASSERT(ir->argStackObjectNum > 0);
							PopStackN(shareMethod->parameters_count);
							PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
							ir->ctorFrameBase = GetEvalStackNewTopOffset();
							maxStackSize = std::max(maxStackSize, curStackSize + ir->argStackObjectNum + 1); // 1 for __this
						}
					}
					IL2CPP_ASSERT(maxStackSize < MAX_STACK_SIZE);
					continue;
				}

				int32_t needDataSlotNum = (resolvedTotalArgNum + 3) / 4;
				Managed2NativeCallMethod managed2NativeMethod = InterpreterModule::GetManaged2NativeMethodPointer(shareMethod, false);
				IL2CPP_ASSERT((void*)managed2NativeMethod);
				//uint32_t managed2NativeMethodDataIdx = GetOrAddResolveDataIndex(managed2NativeMethod);



				int32_t argIdxDataIndex;
				uint16_t* __argIdxs;
				AllocResolvedData(resolveDatas, needDataSlotNum, argIdxDataIndex, __argIdxs);
				//
				// arg1, arg2, arg3 ..., argN, obj or valuetype, __this(= obj or ref valuetype)
				// obj on new top
				PushStackByType(&klass->byval_arg);
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				__argIdxs[0] = GetEvalStackTopOffset(); // this

				for (uint8_t i = 0; i < shareMethod->parameters_count; i++)
				{
					int32_t curArgIdx = i + 1;
					__argIdxs[curArgIdx] = evalStack[callArgEvalStackIdxBase + i].locOffset;
				}
				PopStackN(resolvedTotalArgNum + 1); // args + obj + this
				PushStackByType(&klass->byval_arg);
				CreateAddIR(ir, NewClassVar);
				ir->type = IS_CLASS_VALUE_TYPE(shareMethod->klass) ? HiOpcodeEnum::NewValueTypeVar : HiOpcodeEnum::NewClassVar;
				ir->managed2NativeMethod = GetOrAddResolveDataIndex((void*)managed2NativeMethod);
				ir->method = methodDataIndex;
				ir->argIdxs = argIdxDataIndex;
				ir->obj = objIdx;

				continue;
			}
			case OpcodeValue::CASTCLASS:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);

				if (il2cpp::vm::Class::IsNullable(objKlass))
				{
					objKlass = il2cpp::vm::Class::GetNullableArgument(objKlass);
				}
				uint32_t klassDataIdx = GetOrAddResolveDataIndex(objKlass);

				CreateAddIR(ir, CastclassVar);
				ir->obj = GetEvalStackTopOffset();
				ir->klass = klassDataIdx;
				ip += 5;
				continue;
			}
			case OpcodeValue::ISINST:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);

				if (il2cpp::vm::Class::IsNullable(objKlass))
				{
					objKlass = il2cpp::vm::Class::GetNullableArgument(objKlass);
				}
				uint32_t klassDataIdx = GetOrAddResolveDataIndex(objKlass);

				CreateAddIR(ir, IsInstVar);
				ir->obj = GetEvalStackTopOffset();
				ir->klass = klassDataIdx;
				ip += 5;
				continue;
			}
			case OpcodeValue::CONV_R_UN:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& top = evalStack[evalStackTop - 1];
				switch (top.reduceType)
				{
				case EvalStackReduceDataType::I4:
				{
					CreateAddIR(ir, ConvertVarVar_u4_f8);
					ir->dst = ir->src = GetEvalStackTopOffset();
					break;
				}
				case EvalStackReduceDataType::I8:
				{
					CreateAddIR(ir, ConvertVarVar_u8_f8);
					ir->dst = ir->src = GetEvalStackTopOffset();
					break;
				}
				default:
				{
					RaiseExecutionEngineException("");
					break;
				}
				}
				top.reduceType = EvalStackReduceDataType::R8;
				top.byteSize = 8;
				ip++;
				continue;
			}
			case OpcodeValue::UNBOX:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				//if (il2cpp::vm::Class::IsNullable(objKlass))
				//{
				//    objKlass = il2cpp::vm::Class::GetNullableArgument(objKlass);
				//}
				CreateAddIR(ir, UnBoxVarVar);
				ir->addr = ir->obj = GetEvalStackTopOffset();
				ir->klass = GetOrAddResolveDataIndex(objKlass);

				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

				ip += 5;
				continue;
			}
			case OpcodeValue::THROW:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				CreateAddIR(ir, ThrowEx);
				ir->exceptionObj = GetEvalStackTopOffset();
				ir->firstHandlerIndex = FindFirstThrowHandlerIndex(body.exceptionClauses, ipOffset);
				PopAllStack();
				PopBranch();
				continue;
			}
			case OpcodeValue::LDFLD:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);
				// ldfld obj may be obj or or valuetype or ref valuetype....
				EvalStackVarInfo& obj = evalStack[evalStackTop - 1];
				uint16_t topIdx = GetEvalStackTopOffset();
				IRCommon* ir = obj.reduceType != NATIVE_INT_REDUCE_TYPE && IS_CLASS_VALUE_TYPE(fieldInfo->parent) ? CreateValueTypeLdfld(pool, topIdx, topIdx, fieldInfo) : CreateClassLdfld(pool, topIdx, topIdx, fieldInfo);
				AddInst(ir);
				PopStack();
				PushStackByType(fieldInfo->type);

				InsertMemoryBarrier();
				ResetPrefixFlags();

				ip += 5;
				continue;
			}
			case OpcodeValue::LDFLDA:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);

				uint16_t topIdx = GetEvalStackTopOffset();
				uint32_t fieldOffset = GetFieldOffset(fieldInfo);
				if (fieldOffset <= kMaxShortFieldOffset)
				{
					CreateAddIR(ir, LdfldaVarVar);
					ir->dst = topIdx;
					ir->obj = topIdx;
					ir->offset = (uint16_t)fieldOffset;
				}
				else
				{
					CreateAddIR(ir, LdfldaLargeVarVar);
					ir->dst = topIdx;
					ir->obj = topIdx;
					ir->offset = fieldOffset;
				}

				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				ip += 5;
				continue;
			}
			case OpcodeValue::STFLD:
			{
				InsertMemoryBarrier();
				ResetPrefixFlags();

				IL2CPP_ASSERT(evalStackTop >= 2);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);

				IRCommon* ir = CreateStfld(pool, GetEvalStackOffset_2(), fieldInfo, GetEvalStackOffset_1());
				AddInst(ir);
				PopStackN(2);
				ip += 5;
				continue;
			}
			case OpcodeValue::LDSFLD:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);
				uint32_t parentIndex = GetOrAddResolveDataIndex(fieldInfo->parent);
				uint16_t dstIdx = GetEvalStackNewTopOffset();
				IRCommon* ir = fieldInfo->offset != THREAD_STATIC_FIELD_OFFSET ?
					CreateLdsfld(pool, dstIdx, fieldInfo, parentIndex)
					: CreateLdthreadlocal(pool, dstIdx, fieldInfo, parentIndex);
				AddInst(ir);
				PushStackByType(fieldInfo->type);

				InsertMemoryBarrier();
				ResetPrefixFlags();

				ip += 5;
				continue;
			}
			case OpcodeValue::LDSFLDA:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);

				uint16_t dstIdx = GetEvalStackNewTopOffset();
				if (fieldInfo->offset != THREAD_STATIC_FIELD_OFFSET)
				{
					bool ldfldFromFieldData = false;
					if (hotc233::metadata::IsInterpreterType(fieldInfo->parent))
					{
						const FieldDetail& fieldDet = hotc233::metadata::MetadataModule::GetImage(fieldInfo->parent)
							->GetFieldDetailFromRawIndex(hotc233::metadata::DecodeTokenRowIndex(fieldInfo->token - 1));
						if (fieldDet.defaultValueIndex != kDefaultValueIndexNull)
						{
							ldfldFromFieldData = true;
							CreateAddIR(ir, LdsfldaFromFieldDataVarVar);
							ir->dst = dstIdx;
							ir->src = GetOrAddResolveDataIndex(il2cpp::vm::Field::GetData(fieldInfo));
						}
					}
					if (!ldfldFromFieldData)
					{
						CreateAddIR(ir, LdsfldaVarVar);
						ir->dst = dstIdx;
						ir->klass = GetOrAddResolveDataIndex(fieldInfo->parent);
						ir->offset = fieldInfo->offset;
					}
				}
				else
				{
					CreateAddIR(ir, LdthreadlocalaVarVar);
					ir->dst = dstIdx;
					ir->klass = GetOrAddResolveDataIndex(fieldInfo->parent);
					ir->offset = GetThreadStaticFieldOffset(fieldInfo);
				}
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

				ip += 5;
				continue;
			}
			case OpcodeValue::STSFLD:
			{
				InsertMemoryBarrier();
				ResetPrefixFlags();
				IL2CPP_ASSERT(evalStackTop >= 1);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				FieldInfo* fieldInfo = const_cast<FieldInfo*>(image->GetFieldInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext));
				IL2CPP_ASSERT(fieldInfo);

				uint32_t klassIndex = GetOrAddResolveDataIndex(fieldInfo->parent);
				uint16_t dataIdx = GetEvalStackTopOffset();
				IRCommon* ir = fieldInfo->offset != THREAD_STATIC_FIELD_OFFSET ?
					CreateStsfld(pool, fieldInfo, klassIndex, dataIdx)
					: CreateStthreadlocal(pool, fieldInfo, klassIndex, dataIdx);
				AddInst(ir);

				PopStack();
				ip += 5;
				continue;
			}
			case OpcodeValue::STOBJ:
			{
				InsertMemoryBarrier();
				ResetPrefixFlags();

				IL2CPP_ASSERT(evalStackTop >= 2);
				EvalStackVarInfo& dst = evalStack[evalStackTop - 2];
				EvalStackVarInfo& src = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);

				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);

				IL2CPP_ASSERT(objKlass);
				if (IS_CLASS_VALUE_TYPE(objKlass))
				{
					uint32_t size = GetTypeValueSize(objKlass);
					if (!HOTC233_ENABLE_WRITE_BARRIERS || !objKlass->has_references)
					{
						switch (size)
						{
						case 1:
						{
							CreateAddIR(ir, StobjVarVar_1);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 2:
						{
							CreateAddIR(ir, StobjVarVar_2);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 4:
						{
							CreateAddIR(ir, StobjVarVar_4);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 8:
						{
							CreateAddIR(ir, StobjVarVar_8);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 12:
						{
							CreateAddIR(ir, StobjVarVar_12);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						case 16:
						{
							CreateAddIR(ir, StobjVarVar_16);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							break;
						}
						default:
						{
							CreateAddIR(ir, StobjVarVar_n_4);
							ir->dst = dst.locOffset;
							ir->src = src.locOffset;
							ir->size = size;
							break;
						}
						}
					}
					else
					{
						CreateAddIR(ir, StobjVarVar_WriteBarrier_n_4);
						ir->dst = dst.locOffset;
						ir->src = src.locOffset;
						ir->size = size;
					}
				}
				else
				{
					CreateAddIR(ir, StobjVarVar_ref);
					ir->dst = dst.locOffset;
					ir->src = src.locOffset;
				}

				PopStackN(2);
				ip += 5;
				continue;
			}
			case OpcodeValue::CONV_OVF_I1_UN:
			{
				CI_conv_un_ovf(i1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I2_UN:
			{
				CI_conv_un_ovf(i2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I4_UN:
			{
				CI_conv_un_ovf(i4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I8_UN:
			{
				CI_conv_un_ovf(i8, I8, 8);
				continue;
			}
			case OpcodeValue::CONV_OVF_U1_UN:
			{
				CI_conv_un_ovf(u1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U2_UN:
			{
				CI_conv_un_ovf(u2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U4_UN:
			{
				CI_conv_un_ovf(u4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U8_UN:
			{
				CI_conv_un_ovf(u8, I8, 8);
				continue;
			}
			case OpcodeValue::CONV_OVF_I_UN:
			{
#if HOTC233_ARCH_64
				CI_conv_un_ovf(i8, I8, 8);
#else
				CI_conv_un_ovf(i4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::CONV_OVF_U_UN:
			{
#if HOTC233_ARCH_64
				CI_conv_un_ovf(u8, I8, 8);
#else
				CI_conv_un_ovf(u4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::BOX:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				/*	if (il2cpp::vm::Class::IsNullable(objKlass))
					{
						objKlass = il2cpp::vm::Class::GetNullableArgument(objKlass);
					}*/
				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				if (IS_CLASS_VALUE_TYPE(objKlass))
				{
					CreateAddIR(ir, BoxVarVar);
					ir->dst = ir->data = GetEvalStackTopOffset();
					ir->klass = GetOrAddResolveDataIndex(objKlass);
				}
				else
				{
					// ignore class
				}

				ip += 5;
				continue;
			}
			case OpcodeValue::NEWARR:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& varSize = evalStack[evalStackTop - 1];
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* eleKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(eleKlass);
				Il2CppClass* arrKlass = il2cpp::vm::Class::GetArrayClass(eleKlass, 1);
				uint32_t arrKlassIndex = GetOrAddResolveDataIndex(arrKlass);

				CreateAddIR(ir, NewArrVarVar);
				ir->arr = ir->size = varSize.locOffset;
				ir->klass = arrKlassIndex;

				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

				ip += 5;
				continue;
			}
			case OpcodeValue::LDLEN:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				CreateAddIR(ir, GetArrayLengthVarVar);
				ir->arr = ir->len = GetEvalStackTopOffset();
				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

				ip++;
				continue;
			}
			case OpcodeValue::LDELEMA:
			{
				IL2CPP_ASSERT(evalStackTop >= 2);
				EvalStackVarInfo& arr = evalStack[evalStackTop - 2];
				EvalStackVarInfo& index = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* eleKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				uint32_t eleKlassIndex = GetOrAddResolveDataIndex(eleKlass);

				if ((prefixFlags & (int32_t)PrefixFlags::ReadOnly) || IS_CLASS_VALUE_TYPE(eleKlass))
				{
					CreateAddIR(ir, GetArrayElementAddressAddrVarVar);
					ir->arr = ir->addr = arr.locOffset;
					ir->index = index.locOffset;
				}
				else
				{
					CreateAddIR(ir, GetArrayElementAddressCheckAddrVarVar);
					ir->arr = ir->addr = arr.locOffset;
					ir->index = index.locOffset;
					ir->eleKlass = eleKlassIndex;
				}
				ResetPrefixFlags();
				PopStackN(2);
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				ip += 5;
				continue;
			}
			case OpcodeValue::LDELEM_I1:
			{
				CI_ldele(i1, I4);
				continue;
			}
			case OpcodeValue::LDELEM_U1:
			{
				CI_ldele(u1, I4);
				continue;
			}
			case OpcodeValue::LDELEM_I2:
			{
				CI_ldele(i2, I4);
				continue;
			}
			case OpcodeValue::LDELEM_U2:
			{
				CI_ldele(u2, I4);
				continue;
			}
			case OpcodeValue::LDELEM_I4:
			{
				CI_ldele(i4, I4);
				continue;
			}
			case OpcodeValue::LDELEM_U4:
			{
				CI_ldele(u4, I4);
				continue;
			}
			case OpcodeValue::LDELEM_I8:
			{
				CI_ldele(i8, I8);
				continue;
			}
			case OpcodeValue::LDELEM_I:
			{
#if HOTC233_ARCH_64
				CI_ldele(i8, I8);
#else
				CI_ldele(i4, I4);
#endif
				continue;
			}
			case OpcodeValue::LDELEM_R4:
			{
				CI_ldele(i4, R4);
				continue;
			}
			case OpcodeValue::LDELEM_R8:
			{
				CI_ldele(i8, R8);
				continue;
			}
			case OpcodeValue::LDELEM_REF:
			{
#if HOTC233_ARCH_64
				CI_ldele(i8, I8);
#else
				CI_ldele(i4, I4);
#endif
				continue;
			}
			case OpcodeValue::STELEM_I:
			{
#if HOTC233_ARCH_64
				CI_stele(i8)
#else
				CI_stele(i4)
#endif
					continue;
			}
			case OpcodeValue::STELEM_I1:
			{
				CI_stele(i1);
				continue;
			}
			case OpcodeValue::STELEM_I2:
			{
				CI_stele(i2);
				continue;
			}
			case OpcodeValue::STELEM_I4:
			{
				CI_stele(i4);
				continue;
			}
			case OpcodeValue::STELEM_I8:
			{
				CI_stele(i8);
				continue;
			}
			case OpcodeValue::STELEM_R4:
			{
				CI_stele(i4);
				continue;
			}
			case OpcodeValue::STELEM_R8:
			{
				CI_stele(i8);
				continue;
			}
			case OpcodeValue::STELEM_REF:
			{
				CI_stele(ref);
				continue;
			}

#define CI_ldele0(eleType) \
CreateAddIR(ir,  GetArrayElementVarVar_##eleType); \
ir->arr = arr.locOffset; \
ir->index = index.locOffset; \
ir->dst = arr.locOffset;


			case OpcodeValue::LDELEM:
			{
				IL2CPP_ASSERT(evalStackTop >= 2);
				EvalStackVarInfo& arr = evalStack[evalStackTop - 2];
				EvalStackVarInfo& index = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				const Il2CppType* eleType = &objKlass->byval_arg;

				IL2CPP_ASSERT(index.reduceType == EvalStackReduceDataType::I4 || index.reduceType == EvalStackReduceDataType::I8);
				bool isIndexInt32Type = index.reduceType == EvalStackReduceDataType::I4;
				LocationDescInfo desc = ComputLocationDescInfo(eleType);
				switch (desc.type)
				{
				case LocationDescType::I1: { CI_ldele0(i1); break; }
				case LocationDescType::U1: { CI_ldele0(u1); break; }
				case LocationDescType::I2: { CI_ldele0(i2); break; }
				case LocationDescType::U2: { CI_ldele0(u2); break; }
				case LocationDescType::I4: { CI_ldele0(i4); break; }
				case LocationDescType::I8: { CI_ldele0(i8); break; }
				case LocationDescType::Ref:
				{
					if (HOTC233_ARCH_64)
					{
						CI_ldele0(i8);
					}
					else
					{
						CI_ldele0(i4);
					}
					break;
				}
				case LocationDescType::S:
				case LocationDescType::StructContainsRef:
				{
					CreateAddIR(ir, GetArrayElementVarVar_size_1);
					ir->arr = arr.locOffset;
					ir->index = index.locOffset;
					ir->dst = arr.locOffset;
					uint32_t size = il2cpp::vm::Class::GetValueSize(objKlass, nullptr);
					switch (size)
					{
					case 1:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_1;
						break;
					}
					case 2:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_2;
						break;
					}
					case 4:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_4;
						break;
					}
					case 8:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_8;
						break;
					}
					case 12:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_12;
						break;
					}
					case 16:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_16;
						break;
					}
					case 20:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_20;
						break;
					}
					case 24:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_24;
						break;
					}
					case 28:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_28;
						break;
					}
					case 32:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_size_32;
						break;
					}
					default:
					{
						ir->type = HiOpcodeEnum::GetArrayElementVarVar_n;
					}
					}
					break;
				}
				default:
				{
					RaiseExecutionEngineException("ldelem not support type");
				}
				}
				PopStackN(2);
				PushStackByType(eleType);

				ip += 5;
				continue;
			}


#define CI_stele0(eleType) \
CreateAddIR(ir, SetArrayElementVarVar_##eleType); \
ir->arr = arr.locOffset; \
ir->index = index.locOffset; \
ir->ele = ele.locOffset; 

			case OpcodeValue::STELEM:
			{
				IL2CPP_ASSERT(evalStackTop >= 3);
				EvalStackVarInfo& arr = evalStack[evalStackTop - 3];
				EvalStackVarInfo& index = evalStack[evalStackTop - 2];
				EvalStackVarInfo& ele = evalStack[evalStackTop - 1];

				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				const Il2CppType* eleType = &objKlass->byval_arg;

				IL2CPP_ASSERT(index.reduceType == EvalStackReduceDataType::I4 || index.reduceType == EvalStackReduceDataType::I8);
				bool isIndexInt32Type = index.reduceType == EvalStackReduceDataType::I4;
				LocationDescInfo desc = ComputLocationDescInfo(eleType);
				switch (desc.type)
				{
				case LocationDescType::I1: { CI_stele0(i1); break; }
				case LocationDescType::U1: { CI_stele0(u1); break; }
				case LocationDescType::I2: { CI_stele0(i2); break; }
				case LocationDescType::U2: { CI_stele0(u2); break; }
				case LocationDescType::I4: { CI_stele0(i4); break; }
				case LocationDescType::I8: { CI_stele0(i8); break; }
				case LocationDescType::Ref: { CI_stele0(ref); break; }
				case LocationDescType::S:
				{
					uint32_t size = il2cpp::vm::Class::GetValueSize(objKlass, nullptr);
					switch (size)
					{
					case 12:
					{
						CreateAddIR(ir, SetArrayElementVarVar_size_12);
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						break;
					}
					case 16:
					{
						CreateAddIR(ir, SetArrayElementVarVar_size_16);
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						break;
					}
					case 20:
					{
						IRSetArrayElementVarVar_size_12* ir = pool.AllocIR<IRSetArrayElementVarVar_size_12>();
						ir->type = HiOpcodeEnum::SetArrayElementVarVar_size_20;
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						AddInst(ir);
						break;
					}
					case 24:
					{
						IRSetArrayElementVarVar_size_12* ir = pool.AllocIR<IRSetArrayElementVarVar_size_12>();
						ir->type = HiOpcodeEnum::SetArrayElementVarVar_size_24;
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						AddInst(ir);
						break;
					}
					case 28:
					{
						IRSetArrayElementVarVar_size_12* ir = pool.AllocIR<IRSetArrayElementVarVar_size_12>();
						ir->type = HiOpcodeEnum::SetArrayElementVarVar_size_28;
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						AddInst(ir);
						break;
					}
					case 32:
					{
						IRSetArrayElementVarVar_size_12* ir = pool.AllocIR<IRSetArrayElementVarVar_size_12>();
						ir->type = HiOpcodeEnum::SetArrayElementVarVar_size_32;
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						AddInst(ir);
						break;
					}
					default:
					{
						CreateAddIR(ir, SetArrayElementVarVar_n);
						ir->arr = arr.locOffset;
						ir->index = index.locOffset;
						ir->ele = ele.locOffset;
						break;
					}
					}
					break;
				}
				case LocationDescType::StructContainsRef:
				{
					CreateAddIR(ir, SetArrayElementVarVar_WriteBarrier_n);
					ir->arr = arr.locOffset;
					ir->index = index.locOffset;
					ir->ele = ele.locOffset;
					break;
				}
				default:
				{
					RaiseExecutionEngineException("stelem not support type");
				}
				}
				PopStackN(3);

				ip += 5;
				continue;
			}
			case OpcodeValue::UNBOX_ANY:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);

				if (IS_CLASS_VALUE_TYPE(objKlass))
				{
					CreateAddIR(ir, UnBoxAnyVarVar);
					ir->dst = ir->obj = GetEvalStackTopOffset();
					ir->klass = GetOrAddResolveDataIndex(objKlass);

					PopStack();
					PushStackByType(&objKlass->byval_arg);
				}
				else
				{
					CreateAddIR(ir, CastclassVar);
					ir->obj = GetEvalStackTopOffset();
					ir->klass = GetOrAddResolveDataIndex(objKlass);
				}

				ip += 5;
				continue;
			}
			case OpcodeValue::CONV_OVF_I1:
			{
				CI_conv_ovf(i1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U1:
			{
				CI_conv_ovf(u1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I2:
			{
				CI_conv_ovf(i2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U2:
			{
				CI_conv_ovf(u2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I4:
			{
				CI_conv_ovf(i4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_U4:
			{
				CI_conv_ovf(u4, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_OVF_I8:
			{
				CI_conv_ovf(i8, I8, 8);
				continue;
			}
			case OpcodeValue::CONV_OVF_U8:
			{
				CI_conv_ovf(u8, I8, 8);
				continue;
			}
			case OpcodeValue::REFANYVAL:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				CreateAddIR(ir, RefAnyValueVarVar);
				ir->addr = ir->typedRef = GetEvalStackTopOffset();
				ir->klass = GetOrAddResolveDataIndex(objKlass);
				PopStack();
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				ip += 5;
				continue;
			}
			case OpcodeValue::CKFINITE:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				EvalStackVarInfo& top = evalStack[evalStackTop - 1];
				switch (top.reduceType)
				{
				case EvalStackReduceDataType::R4:
				{
					CreateAddIR(ir, CheckFiniteVar_f4);
					ir->src = GetEvalStackTopOffset();
					break;
				}
				case EvalStackReduceDataType::R8:
				{
					CreateAddIR(ir, CheckFiniteVar_f8);
					ir->src = GetEvalStackTopOffset();
					break;
				}
				default:
				{
					RaiseExecutionEngineException("CKFINITE invalid reduceType");
					break;
				}
				}

				ip++;
				continue;
			}
			case OpcodeValue::MKREFANY:
			{
				IL2CPP_ASSERT(evalStackTop > 0);
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
				IL2CPP_ASSERT(objKlass);
				CreateAddIR(ir, MakeRefVarVar);
				ir->dst = ir->data = GetEvalStackTopOffset();
				ir->klass = GetOrAddResolveDataIndex(objKlass);
				PopStack();

				Il2CppType typedRef = {};
				typedRef.type = IL2CPP_TYPE_TYPEDBYREF;
				PushStackByType(&typedRef);

				ip += 5;
				continue;
			}
			case OpcodeValue::LDTOKEN:
			{
				uint32_t token = (uint32_t)GetI4LittleEndian(ip + 1);
				void* runtimeHandle = (void*)image->GetRuntimeHandleFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);

				CreateAddIR(ir, LdtokenVar);
				ir->runtimeHandle = GetEvalStackNewTopOffset();
				ir->token = GetOrAddResolveDataIndex(runtimeHandle);
				PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
				ip += 5;
				continue;
			}
			case OpcodeValue::CONV_U2:
			{
				CI_conv(u2, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_U1:
			{
				CI_conv(u1, I4, 4);
				continue;
			}
			case OpcodeValue::CONV_I:
			{
#if HOTC233_ARCH_64
				CI_conv(i8, I8, 8);
#else
				CI_conv(i4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::CONV_OVF_I:
			{
#if HOTC233_ARCH_64
				CI_conv_ovf(i8, I8, 8);
#else
				CI_conv_ovf(i4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::CONV_OVF_U:
			{
#if HOTC233_ARCH_64
				CI_conv_ovf(u8, I8, 8);
#else
				CI_conv_ovf(u4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::ADD_OVF:
			{
				CI_binOpOvf(Add);
				continue;
			}
			case OpcodeValue::ADD_OVF_UN:
			{
				CI_binOpUnOvf(Add);
				continue;
			}
			case OpcodeValue::MUL_OVF:
			{
				CI_binOpOvf(Mul);
				continue;
			}
			case OpcodeValue::MUL_OVF_UN:
			{
				CI_binOpUnOvf(Mul);
				continue;
			}
			case OpcodeValue::SUB_OVF:
			{
				CI_binOpOvf(Sub);
				continue;
			}
			case OpcodeValue::SUB_OVF_UN:
			{
				CI_binOpUnOvf(Sub);
				continue;
			}
			case OpcodeValue::ENDFINALLY:
			{
				CreateAddIR(ir, EndFinallyEx);
				PopBranch();
				continue;
			}
			case OpcodeValue::STIND_I:
			{
				Add_stind(ARCH_ARGUMENT(HiOpcodeEnum::StindVarVar_i4, HiOpcodeEnum::StindVarVar_i8));
				continue;
			}
			case OpcodeValue::CONV_U:
			{
#if HOTC233_ARCH_64
				CI_conv(u8, I8, 8);
#else
				CI_conv(u4, I4, 4);
#endif
				continue;
			}
			case OpcodeValue::PREFIX7:
			case OpcodeValue::PREFIX6:
			case OpcodeValue::PREFIX5:
			case OpcodeValue::PREFIX4:
			case OpcodeValue::PREFIX3:
			case OpcodeValue::PREFIX2:
			{
				ip++;
				continue;
			}
			case OpcodeValue::PREFIX1:
			{
				// This is the prefix for all the 2-byte opcodes.
				// Figure out the second byte of the 2-byte opcode.
				byte ops = *(ip + 1);

				switch ((OpcodeValue)ops)
				{

				case OpcodeValue::ARGLIST:
				{
					RaiseExecutionEngineException("");
					ip += 2;
					continue;
				}
				case OpcodeValue::CEQ:
				{
					CI_compare(Ceq);
					ip += 2;
					continue;
				}
				case OpcodeValue::CGT:
				{
					CI_compare(Cgt);
					ip += 2;
					continue;
				}
				case OpcodeValue::CGT_UN:
				{
					CI_compare(CgtUn);
					ip += 2;
					continue;
				}
				case OpcodeValue::CLT:
				{
					CI_compare(Clt);
					ip += 2;
					continue;
				}
				case OpcodeValue::CLT_UN:
				{
					CI_compare(CltUn);
					ip += 2;
					continue;
				}
				case OpcodeValue::LDFTN:
				{
					uint32_t methodToken = (uint32_t)GetI4LittleEndian(ip + 2);
					MethodInfo* methodInfo = const_cast<MethodInfo*>(image->GetMethodInfoFromToken(tokenCache, methodToken, klassContainer, methodContainer, genericContext));
					IL2CPP_ASSERT(methodInfo);
					CreateAddIR(ir, LdcVarConst_8);
					ir->dst = GetEvalStackNewTopOffset();
					ir->src = (uint64_t)methodInfo;
					PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
					ip += 6;
					continue;
				}
				case OpcodeValue::LDVIRTFTN:
				{
					IL2CPP_ASSERT(evalStackTop > 0);
					uint32_t methodToken = (uint32_t)GetI4LittleEndian(ip + 2);
					MethodInfo* methodInfo = const_cast<MethodInfo*>(image->GetMethodInfoFromToken(tokenCache, methodToken, klassContainer, methodContainer, genericContext));
					IL2CPP_ASSERT(methodInfo);

					CreateAddIR(ir, LdvirftnVarVar);
					ir->resultMethod = ir->obj = GetEvalStackTopOffset();
					ir->virtualMethod = GetOrAddResolveDataIndex(methodInfo);

					PopStack();
					PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);
					ip += 6;
					continue;
				}
				case OpcodeValue::UNUSED56:
				{
					ip += 2;
					continue;
				}
				case OpcodeValue::LDARG:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					AddInst_ldarg(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::LDARGA:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					AddInst_ldarga(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::STARG:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					AddInst_starg(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::LDLOC:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					CreateAddInst_ldloc(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::LDLOCA:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					CreateAddInst_ldloca(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::STLOC:
				{
					argIdx = GetU2LittleEndian(ip + 2);
					CreateAddInst_stloc(argIdx);
					ip += 4;
					continue;
				}
				case OpcodeValue::LOCALLOC:
				{
					IL2CPP_ASSERT(evalStackTop > 0);
					EvalStackVarInfo& top = evalStack[evalStackTop - 1];

					switch (top.reduceType)
					{
					case EvalStackReduceDataType::I4:
					case EvalStackReduceDataType::I8: // FIXE ME
					{
						CreateAddIR(ir, LocalAllocVarVar_n_4);
						ir->dst = ir->size = GetEvalStackTopOffset();
						break;
					}
					default:
					{
						RaiseExecutionEngineException("LOCALLOC invalid reduceType");
						break;
					}
					}
					PopStack();
					PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

					ip += 2;
					continue;
				}
				case OpcodeValue::UNUSED57:
				{
					ip += 2;
					continue;
				}
				case OpcodeValue::ENDFILTER:
				{
					CreateAddIR(ir, EndFilterEx);
					ir->value = GetEvalStackTopOffset();
					PopAllStack();

					PopBranch();
					continue;
				}
				case OpcodeValue::UNALIGNED_:
				{
					// Nothing to do here.
					prefixFlags |= (int32_t)PrefixFlags::Unaligned;
					uint8_t alignment = ip[2];
					IL2CPP_ASSERT(alignment == 1 || alignment == 2 || alignment == 4);
					ip += 3;
					continue;
				}
				case OpcodeValue::VOLATILE_:
				{
					// Set a flag that causes a memory barrier to be associated with the next load or store.
					//CI_volatileFlag = true;
					prefixFlags |= (int32_t)PrefixFlags::Volatile;
					ip += 2;
					continue;
				}
				case OpcodeValue::TAIL_:
				{
					prefixFlags |= (int32_t)PrefixFlags::Tail;
					ip += 2;
					continue;
				}
				case OpcodeValue::INITOBJ:
				{
					IL2CPP_ASSERT(evalStackTop > 0);
					uint32_t token = (uint32_t)GetI4LittleEndian(ip + 2);
					Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
					if (IS_CLASS_VALUE_TYPE(objKlass))
					{
						uint32_t objSize = GetTypeValueSize(objKlass);
						if ((HOTC233_ENABLE_WRITE_BARRIERS && objKlass->has_references))
						{
							CreateAddIR(ir, InitobjVar_WriteBarrier_n_4);
							ir->obj = GetEvalStackTopOffset();
							ir->size = objSize;
						}
						else
						{
							bool convert = false;
							switch (objSize)
							{
							case 1:
							{
								CreateAddIR(ir, InitobjVar_1);
								ir->obj = GetEvalStackTopOffset();
								convert = true;
								break;
							}
							case 2:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 2)
								{
									CreateAddIR(ir, InitobjVar_2);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 4:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 4)
								{
									CreateAddIR(ir, InitobjVar_4);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 8:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 8)
								{
									CreateAddIR(ir, InitobjVar_8);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 12:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 4)
								{
									CreateAddIR(ir, InitobjVar_12);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 16:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 8)
								{
									CreateAddIR(ir, InitobjVar_16);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 20:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 4)
								{
									CreateAddIR(ir, InitobjVar_20);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 24:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 8)
								{
									CreateAddIR(ir, InitobjVar_24);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 28:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 4)
								{
									CreateAddIR(ir, InitobjVar_28);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							case 32:
							{
								if (SUPPORT_MEMORY_NOT_ALIGMENT_ACCESS || objKlass->minimumAlignment >= 8)
								{
									CreateAddIR(ir, InitobjVar_32);
									ir->obj = GetEvalStackTopOffset();
									convert = true;
								}
								break;
							}
							}
							if (!convert)
							{
								CreateAddIR(ir, InitobjVar_n_4);
								ir->obj = GetEvalStackTopOffset();
								ir->size = objSize;
							}
						}
					}
					else
					{
						CreateAddIR(ir, InitobjVar_ref);
						ir->obj = GetEvalStackTopOffset();
					}
					PopStack();

					ip += 6;
					break;
				}
				case OpcodeValue::CONSTRAINED_:
				{
					uint32_t typeToken = (uint32_t)GetI4LittleEndian(ip + 2);
					Il2CppClass* conKlass = image->GetClassFromToken(tokenCache, typeToken, klassContainer, methodContainer, genericContext);
					IL2CPP_ASSERT(conKlass);
					ip += 6;

					IL2CPP_ASSERT(*ip == (uint8_t)OpcodeValue::CALLVIRT);
					uint32_t methodToken = (uint32_t)GetI4LittleEndian(ip + 1);
					ip += 5;

					// TODO token cache optimistic
					shareMethod = const_cast<MethodInfo*>(image->GetMethodInfoFromToken(tokenCache, methodToken, klassContainer, methodContainer, genericContext));
					IL2CPP_ASSERT(shareMethod);


					int32_t resolvedTotalArgNum = shareMethod->parameters_count + 1;

					int32_t selfIdx = evalStackTop - resolvedTotalArgNum;
					EvalStackVarInfo& self = evalStack[selfIdx];
					if (IS_CLASS_VALUE_TYPE(conKlass))
					{
						// impl in self
						const MethodInfo* implMethod = image->FindImplMethod(conKlass, shareMethod);
						if (implMethod->klass == conKlass)
						{
							shareMethod = implMethod;
							goto LabelCall;
						}
						else if (conKlass->enumtype && !std::strcmp(shareMethod->name, "GetHashCode"))
						{
							Il2CppTypeEnum typeEnum = conKlass->element_class->byval_arg.type;
							self.reduceType = EvalStackReduceDataType::I4;
							if (typeEnum == IL2CPP_TYPE_I8 || typeEnum == IL2CPP_TYPE_U8)
							{
								CreateAddIR(ir, GetEnumHashCode);
								ir->dst = ir->src = self.locOffset;
							}
							else
							{
								CreateAddIR(ir, LdindVarVar_i1);
								ir->dst = ir->src = self.locOffset;
								switch (conKlass->element_class->byval_arg.type)
								{
								case IL2CPP_TYPE_U1: ir->type = HiOpcodeEnum::LdindVarVar_u1; break;
								case IL2CPP_TYPE_I1: ir->type = HiOpcodeEnum::LdindVarVar_i1; break;
								case IL2CPP_TYPE_U2: ir->type = HiOpcodeEnum::LdindVarVar_u2; break;
								case IL2CPP_TYPE_I2: ir->type = HiOpcodeEnum::LdindVarVar_u2; break;
								case IL2CPP_TYPE_U4: ir->type = HiOpcodeEnum::LdindVarVar_u4; break;
								case IL2CPP_TYPE_I4: ir->type = HiOpcodeEnum::LdindVarVar_i4; break;
								case IL2CPP_TYPE_CHAR: ir->type = HiOpcodeEnum::LdindVarVar_u2; break;
								case IL2CPP_TYPE_BOOLEAN: ir->type = HiOpcodeEnum::LdindVarVar_i1; break;
								default:
									IL2CPP_ASSERT(false && "GetHashCode");
									break;
								}
							}
						}
						else
						{
							CreateAddIR(ir, BoxRefVarVar);
							ir->dst = ir->src = self.locOffset;
							ir->klass = GetOrAddResolveDataIndex(conKlass);

							self.reduceType = NATIVE_INT_REDUCE_TYPE;
							self.byteSize = GetSizeByReduceType(self.reduceType);
							goto LabelCallVir;
						}
					}
					else
					{
#if HOTC233_ARCH_64
						CreateAddIR(ir, LdindVarVar_i8);
#else
						CreateAddIR(ir, LdindVarVar_i4);
#endif
						ir->dst = ir->src = self.locOffset;
						self.reduceType = NATIVE_INT_REDUCE_TYPE;
						self.byteSize = GetSizeByReduceType(self.reduceType);
						goto LabelCallVir;
					}
					continue;
				}
				case OpcodeValue::CPBLK:
				{
					// we don't sure dst or src is volatile. so insert memory barrier ahead and end.
					IL2CPP_ASSERT(evalStackTop >= 3);
					InsertMemoryBarrier();
					ResetPrefixFlags();
					CreateAddIR(ir, CpblkVarVar);
					ir->dst = GetEvalStackOffset_3();
					ir->src = GetEvalStackOffset_2();
					ir->size = GetEvalStackOffset_1();
					PopStackN(3);
					InsertMemoryBarrier();
					ResetPrefixFlags();
					ip += 2;
					continue;
				}
				case OpcodeValue::INITBLK:
				{
					IL2CPP_ASSERT(evalStackTop >= 3);
					InsertMemoryBarrier();
					ResetPrefixFlags();
					CreateAddIR(ir, InitblkVarVarVar);
					ir->addr = GetEvalStackOffset_3();
					ir->value = GetEvalStackOffset_2();
					ir->size = GetEvalStackOffset_1();
					PopStackN(3);
					ip += 2;
					continue;
				}
				case OpcodeValue::NO_:
				{
					uint8_t checkType = ip[2];
					// {typecheck:0x1} | {rangecheck:0x2} | {nullcheck:0x4}
					IL2CPP_ASSERT(checkType < 8);
					ip += 3;
					continue;
				}
				case OpcodeValue::RETHROW:
				{
					CreateAddIR(ir, RethrowEx);
					AddInst(ir);
					PopAllStack();
					PopBranch();
					continue;
				}
				case OpcodeValue::UNUSED:
				{
					ip += 2;
					continue;
				}
				case OpcodeValue::SIZEOF:
				{
					uint32_t token = (uint32_t)GetI4LittleEndian(ip + 2);
					Il2CppClass* objKlass = image->GetClassFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
					IL2CPP_ASSERT(objKlass);
					int32_t typeSize = GetTypeValueSize(&objKlass->byval_arg);
					CreateAddInst_ldc4(typeSize, EvalStackReduceDataType::I4);
					ip += 6;
					continue;
				}
				case OpcodeValue::REFANYTYPE:
				{
					IL2CPP_ASSERT(evalStackTop > 0);
					CreateAddIR(ir, RefAnyTypeVarVar);
					ir->dst = ir->typedRef = GetEvalStackOffset_1();
					PopStack();
					PushStackByReduceType(NATIVE_INT_REDUCE_TYPE);

					ip += 2;
					continue;
				}
				case OpcodeValue::READONLY_:
				{
					prefixFlags |= (int32_t)PrefixFlags::ReadOnly;
					ip += 2;
					// generic md array also can follow readonly
					//IL2CPP_ASSERT(*ip == (byte)OpcodeValue::LDELEMA && "According to the ECMA spec, READONLY may only precede LDELEMA");
					continue;
				}
				case OpcodeValue::UNUSED53:
				case OpcodeValue::UNUSED54:
				case OpcodeValue::UNUSED55:
				case OpcodeValue::UNUSED70:
				{
					ip += 2;
					continue;
				}
				default:
				{
					//UNREACHABLE();
					RaiseExecutionEngineException("not support instruction");
					continue;
				}
				}
				continue;
			}
			case OpcodeValue::PREFIXREF:
			{
				ip++;
				continue;
			}
			default:
			{
				RaiseExecutionEngineException("not support instruction");
				continue;
			}
			}
			ip++;
		}
	finish_transform:

#if HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM
		OptimizeBasicBlocks();
#else
		ApplyCommunityPeepholeFusion();
#endif
#if HOTC233_ENABLE_BINOP_I4_TRACE
		for (IRBasicBlock* bb : irbbs)
		{
			FoldBinOpI4AddChainTrace(bb->insts);
		}
#endif

		totalIRSize = 0;
		for (IRBasicBlock* bb : irbbs)
		{
			bb->codeOffset = totalIRSize;
			for (IRCommon* ir : bb->insts)
			{
				totalIRSize += g_instructionSizes[(int)ir->type];
			}
		}
		endBb->codeOffset = totalIRSize;

		for (int32_t* relocOffsetPtr : relocationOffsets)
		{
			int32_t relocOffset = *relocOffsetPtr;
			IL2CPP_ASSERT(splitOffsets.find(relocOffset) != splitOffsets.end());
			*relocOffsetPtr = ip2bb[relocOffset]->codeOffset;
		}

		for (auto switchOffsetPair : switchOffsetsInResolveData)
		{
			int32_t* offsetStartPtr = (int32_t*)&resolveDatas[switchOffsetPair.first];
			for (int32_t i = 0; i < switchOffsetPair.second; i++)
			{
				int32_t relocOffset = offsetStartPtr[i];
				IL2CPP_ASSERT(splitOffsets.find(relocOffset) != splitOffsets.end());
				offsetStartPtr[i] = ip2bb[relocOffset]->codeOffset;
			}
		}
	}

	static bool ContainsHiOpcode(const byte* codes, uint32_t codeLength, HiOpcodeEnum target)
	{
		for (uint32_t offset = 0; offset < codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == target)
			{
				return true;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > codeLength)
			{
				return false;
			}
			offset += size;
		}
		return false;
	}

	// Fast-path classification must not miss trace opcodes when an earlier IR entry has a stale size.
	static bool ContainsHiOpcodeForFastPath(const byte* codes, uint32_t codeLength, HiOpcodeEnum target)
	{
		if (ContainsHiOpcode(codes, codeLength, target))
		{
			return true;
		}
		if (!codes || codeLength < sizeof(uint16_t))
		{
			return false;
		}
		for (uint32_t offset = 0; offset + sizeof(uint16_t) <= codeLength;)
		{
			if (*(HiOpcodeEnum*)(codes + offset) == target)
			{
				return true;
			}
			uint16_t size = g_instructionSizes[(int)*(HiOpcodeEnum*)(codes + offset)];
			if (size >= sizeof(uint16_t) && offset + size <= codeLength)
			{
				offset += size;
			}
			else
			{
				offset += sizeof(uint16_t);
			}
		}
		return false;
	}

	static uint32_t CountHiOpcodeForFastPath(const byte* codes, uint32_t codeLength, HiOpcodeEnum target)
	{
		if (!codes || codeLength < sizeof(uint16_t))
		{
			return 0;
		}
		uint32_t count = 0;
		for (uint32_t offset = 0; offset + sizeof(uint16_t) <= codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == target)
			{
				count++;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size >= sizeof(uint16_t) && offset + size <= codeLength)
			{
				offset += size;
			}
			else
			{
				offset += sizeof(uint16_t);
			}
		}
		return count;
	}

	static uint32_t CountHiOpcodeStrict(const byte* codes, uint32_t codeLength, HiOpcodeEnum target)
	{
		if (!codes || codeLength < sizeof(uint16_t))
		{
			return 0;
		}
		uint32_t count = 0;
		for (uint32_t offset = 0; offset < codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == target)
			{
				count++;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > codeLength)
			{
				return UINT32_MAX;
			}
			offset += size;
		}
		return count;
	}

	static bool IsGodDomainWholeMethodShellOpcode(HiOpcodeEnum op)
	{
		switch (op)
		{
		case HiOpcodeEnum::RetVar_ret_4:
		case HiOpcodeEnum::RetVar_void:
		case HiOpcodeEnum::RetVar_ret_8:
			return true;
		default:
			return false;
		}
	}

	static bool TrySumGodDomainTraceOnlySteps(
		const byte* codes,
		uint32_t codeLength,
		HiOpcodeEnum traceOp,
		uint32_t* outTotalSteps)
	{
		if (!codes || !outTotalSteps)
		{
			return false;
		}
		uint32_t total = 0;
		bool foundTrace = false;
		for (uint32_t offset = 0; offset < codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == traceOp)
			{
				total += *(uint16_t*)(codes + offset + 2);
				foundTrace = true;
			}
			else if (!IsGodDomainWholeMethodShellOpcode(op))
			{
				return false;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > codeLength)
			{
				return false;
			}
			offset += size;
		}
		if (!foundTrace || total < 3 || total > 256)
		{
			return false;
		}
		*outTotalSteps = total;
		return true;
	}

	static bool TrySumStaticF4CallTraceSteps(const byte* codes, uint32_t codeLength, uint32_t* outTotalSteps)
	{
		return TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunStaticF4CallTrace, outTotalSteps);
	}

	static bool TryReadStaticF4LoopTraceStepCount(const byte* codes, uint32_t codeLength, uint16_t* outStepCount)
	{
		if (!codes || !outStepCount)
		{
			return false;
		}
		for (uint32_t offset = 0; offset < codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == HiOpcodeEnum::RunStaticF4CallTrace)
			{
				*outStepCount = *(uint16_t*)(codes + offset + 2);
				return true;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > codeLength)
			{
				return false;
			}
			offset += size;
		}
		return false;
	}

	static uint32_t ComputeTypeOfAccumPerLoopStrict(const byte* codes, uint32_t codeLength)
	{
		uint32_t typeTokenLoads = CountHiOpcodeStrict(codes, codeLength, HiOpcodeEnum::LdtokenTypeObjectVar);
		if (typeTokenLoads == UINT32_MAX || typeTokenLoads < 4)
		{
			return 0;
		}
		uint32_t branchOps = CountHiOpcodeStrict(codes, codeLength, HiOpcodeEnum::BranchVarVar_Ceq_i4);
		if (branchOps == UINT32_MAX)
		{
			return 0;
		}
		uint32_t branchNeOps = CountHiOpcodeStrict(codes, codeLength, HiOpcodeEnum::BranchVarVar_CneUn_i4);
		if (branchNeOps == UINT32_MAX)
		{
			return 0;
		}
		if (branchOps + branchNeOps < 2)
		{
			return 0;
		}
		uint32_t addOps = CountHiOpcodeStrict(codes, codeLength, HiOpcodeEnum::BinOpVarVarVar_Add_i4);
		if (addOps == UINT32_MAX || addOps < 2)
		{
			return 0;
		}
		if (!ContainsHiOpcode(codes, codeLength, HiOpcodeEnum::RetVar_ret_4))
		{
			return 0;
		}
		return 4;
	}

	static bool TryReadCallTraceStepCount(const byte* codes, uint32_t codeLength, HiOpcodeEnum target, uint16_t* outStepCount)
	{
		if (!codes || !outStepCount)
		{
			return false;
		}
		for (uint32_t offset = 0; offset < codeLength;)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)(codes + offset);
			if (op == target)
			{
				*outStepCount = *(uint16_t*)(codes + offset + 2);
				return true;
			}
			uint16_t size = g_instructionSizes[(int)op];
			if (size == 0 || offset + size > codeLength)
			{
				return false;
			}
			offset += size;
		}
		return false;
	}

	static uint32_t ClassifyHotc233FastPathParam(
		uint32_t fastPathKind,
		const byte* codes,
		uint32_t codeLength)
	{
		switch ((Hotc233FastPathKind)fastPathKind)
		{
		case Hotc233FastPath_StaticF4LoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumStaticF4CallTraceSteps(codes, codeLength, &totalSteps)
				|| totalSteps < 3 || totalSteps > 256)
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_TypeOfConstAccumI4:
			return ComputeTypeOfAccumPerLoopStrict(codes, codeLength);
		case Hotc233FastPath_InstanceVoidI4x5LoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceVoidI4x5CallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_InstanceVoidV3x4LoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceVoidV3x4CallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_InstanceVoidV3x1LoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceVoidV3x1CallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_InstanceGetTransformSetV3LoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceGetTransformSetV3CallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_InstanceV3ReturnLoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceV3ReturnCallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_InstanceI4ReturnLoopTrace:
		{
			uint32_t totalSteps = 0;
			if (!TrySumGodDomainTraceOnlySteps(
				codes, codeLength, HiOpcodeEnum::RunInstanceI4ReturnCallTrace, &totalSteps))
			{
				return 0;
			}
			return totalSteps;
		}
		case Hotc233FastPath_ArrayOpLoopTrace:
		case Hotc233FastPath_QuaternionOpLoopTrace:
			return 10;
		case Hotc233FastPath_GameObjectCreateDestroyLoopTrace:
			return 1;
		default:
			if (fastPathKind >= Hotc233FastPath_UnityKernel_First && fastPathKind <= Hotc233FastPath_UnityKernel_Last)
			{
				return 1;
			}
			return 0;
		}
	}

	static bool ClassifyTypeOfConstAccumFastPath(
		const byte* codes,
		uint32_t codeLength,
		uint32_t argStackObjectSize)
	{
		if (argStackObjectSize < sizeof(int32_t))
		{
			return false;
		}
		return ComputeTypeOfAccumPerLoopStrict(codes, codeLength) == 4;
	}

	static uint32_t ClassifyHotc233FastPath(
		const byte* codes,
		uint32_t codeLength,
		bool hasExceptionClauses,
		uint32_t initLocals,
		uint32_t argStackObjectSize,
		uint32_t localStackSize)
	{
#if HOTC233_COMMUNITY_BASELINE
		return Hotc233FastPath_Unsupported;
#else
		if (!codes)
		{
			return Hotc233FastPath_Unsupported;
		}

		// Whole-method trace fast paths tolerate initLocals/exception clauses in the method shell.
		if (argStackObjectSize >= sizeof(int32_t))
		{
			uint32_t totalSteps = 0;
			if (TrySumStaticF4CallTraceSteps(codes, codeLength, &totalSteps)
				&& totalSteps >= 3 && totalSteps <= 256)
			{
				return Hotc233FastPath_StaticF4LoopTrace;
			}
		}

		if (ClassifyTypeOfConstAccumFastPath(codes, codeLength, argStackObjectSize))
		{
			return Hotc233FastPath_TypeOfConstAccumI4;
		}

		uint32_t 		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceVoidI4x5CallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceVoidI4x5LoopTrace;
		}

		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceVoidV3x4CallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceVoidV3x4LoopTrace;
		}

		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceVoidV3x1CallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceVoidV3x1LoopTrace;
		}

		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceGetTransformSetV3CallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceGetTransformSetV3LoopTrace;
		}

		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceV3ReturnCallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceV3ReturnLoopTrace;
		}

		godDomainTraceSteps = 0;
		if (TrySumGodDomainTraceOnlySteps(
			codes, codeLength, HiOpcodeEnum::RunInstanceI4ReturnCallTrace, &godDomainTraceSteps)
			&& argStackObjectSize >= sizeof(int32_t))
		{
			return Hotc233FastPath_InstanceI4ReturnLoopTrace;
		}

		if (hasExceptionClauses)
		{
			return Hotc233FastPath_Unsupported;
		}

		if (initLocals)
		{
			return Hotc233FastPath_Unsupported;
		}

		if (codeLength == 56)
		{
			auto isClosureMulConstAddFieldI4 = [argStackObjectSize](const byte* mulIp, const byte* copyIp, const byte* fieldIp, const byte* addIp, const byte* retIp) -> bool
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
				uint16_t mulRet = *(uint16_t*)(mulIp + 12);
				uint16_t mulOp1 = *(uint16_t*)(mulIp + 14);
				uint16_t mulOp2 = *(uint16_t*)(mulIp + 16);
				uint16_t objectCopyDst = *(uint16_t*)(copyIp + 2);
				uint16_t objectCopySrc = *(uint16_t*)(copyIp + 4);
				uint16_t fieldDst = *(uint16_t*)(fieldIp + 2);
				uint16_t fieldObj = *(uint16_t*)(fieldIp + 4);
				uint16_t addRet = *(uint16_t*)(addIp + 2);
				uint16_t addOp1 = *(uint16_t*)(addIp + 4);
				uint16_t addOp2 = *(uint16_t*)(addIp + 6);
				uint16_t retSrc = *(uint16_t*)(retIp + 2);
				auto isMulInput = [copyDst, constDst, argStackObjectSize](uint16_t offset) -> bool
				{
					return offset == copyDst || offset == constDst || offset < argStackObjectSize;
				};
				return retSrc == addRet
					&& copySrc < argStackObjectSize
					&& isMulInput(mulOp1)
					&& isMulInput(mulOp2)
					&& objectCopySrc < argStackObjectSize
					&& (fieldObj == objectCopyDst || fieldObj < argStackObjectSize)
					&& ((addOp1 == mulRet && addOp2 == fieldDst) || (addOp1 == fieldDst && addOp2 == mulRet));
			};
			if (isClosureMulConstAddFieldI4(codes, codes + 24, codes + 32, codes + 40, codes + 48) ||
				isClosureMulConstAddFieldI4(codes + 16, codes, codes + 8, codes + 40, codes + 48))
			{
				return Hotc233FastPath_ClosureMulConstAddFieldI4;
			}
		}

		if (argStackObjectSize != localStackSize)
		{
			return Hotc233FastPath_Unsupported;
		}

		if (codeLength == 8)
		{
			return *(HiOpcodeEnum*)codes == HiOpcodeEnum::RetVar_void ? Hotc233FastPath_EmptyVoid : Hotc233FastPath_Unsupported;
		}

		if (codeLength == 16)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)codes;
			HiOpcodeEnum retOp = *(HiOpcodeEnum*)(codes + 8);
			if (retOp == HiOpcodeEnum::RetVar_ret_4)
			{
				uint16_t ret = *(uint16_t*)(codes + 10);
				if (op == HiOpcodeEnum::LdlocVarVar && *(uint16_t*)(codes + 2) == ret)
				{
					return *(uint16_t*)(codes + 4) < argStackObjectSize ? Hotc233FastPath_ReturnI4 : Hotc233FastPath_Unsupported;
				}
				if (*(uint16_t*)(codes + 2) == ret &&
					(op == HiOpcodeEnum::LdcVarConst_4 ||
						(*(uint16_t*)(codes + 4) < argStackObjectSize && *(uint16_t*)(codes + 6) < argStackObjectSize)))
				{
					switch (op)
					{
					case HiOpcodeEnum::BinOpVarVarVar_Add_i4: return Hotc233FastPath_AddI4;
					case HiOpcodeEnum::BinOpVarVarVar_Sub_i4: return Hotc233FastPath_SubI4;
					case HiOpcodeEnum::BinOpVarVarVar_Mul_i4: return Hotc233FastPath_MulI4;
					case HiOpcodeEnum::BinOpVarVarVar_And_i4: return Hotc233FastPath_AndI4;
					case HiOpcodeEnum::BinOpVarVarVar_Or_i4: return Hotc233FastPath_OrI4;
					case HiOpcodeEnum::BinOpVarVarVar_Xor_i4: return Hotc233FastPath_XorI4;
					case HiOpcodeEnum::LdcVarConst_4: return Hotc233FastPath_ConstI4;
					default: break;
					}
				}
			}
			else if (retOp == HiOpcodeEnum::RetVar_ret_8)
			{
				uint16_t ret = *(uint16_t*)(codes + 10);
				if (op == HiOpcodeEnum::LdlocVarVar && *(uint16_t*)(codes + 2) == ret)
				{
					return *(uint16_t*)(codes + 4) < argStackObjectSize ? Hotc233FastPath_ReturnI8 : Hotc233FastPath_Unsupported;
				}
				if (*(uint16_t*)(codes + 2) == ret &&
					(*(uint16_t*)(codes + 4) < argStackObjectSize && *(uint16_t*)(codes + 6) < argStackObjectSize))
				{
					switch (op)
					{
					case HiOpcodeEnum::BinOpVarVarVar_Add_i8: return Hotc233FastPath_AddI8;
					case HiOpcodeEnum::BinOpVarVarVar_Sub_i8: return Hotc233FastPath_SubI8;
					case HiOpcodeEnum::BinOpVarVarVar_Mul_i8: return Hotc233FastPath_MulI8;
					case HiOpcodeEnum::BinOpVarVarVar_And_i8: return Hotc233FastPath_AndI8;
					case HiOpcodeEnum::BinOpVarVarVar_Or_i8: return Hotc233FastPath_OrI8;
					case HiOpcodeEnum::BinOpVarVarVar_Xor_i8: return Hotc233FastPath_XorI8;
					default: break;
					}
				}
			}
		}
		else if (codeLength == 24)
		{
			HiOpcodeEnum op = *(HiOpcodeEnum*)codes;
			if (op == HiOpcodeEnum::LdcVarConst_8 &&
				*(HiOpcodeEnum*)(codes + 16) == HiOpcodeEnum::RetVar_ret_8 &&
				*(uint16_t*)(codes + 2) == *(uint16_t*)(codes + 18))
			{
				return Hotc233FastPath_ConstI8;
			}
			if (op == HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4_RetVar_ret_4 &&
				*(uint16_t*)(codes + 12) == *(uint16_t*)(codes + 18) &&
				*(uint16_t*)(codes + 4) < argStackObjectSize)
			{
				uint16_t copyDst = *(uint16_t*)(codes + 2);
				uint16_t constDst = *(uint16_t*)(codes + 6);
				uint16_t op1 = *(uint16_t*)(codes + 14);
				uint16_t op2 = *(uint16_t*)(codes + 16);
				if ((op1 == copyDst || op1 == constDst || op1 < argStackObjectSize) &&
					(op2 == copyDst || op2 == constDst || op2 < argStackObjectSize))
				{
					return Hotc233FastPath_CopyConstMulRetI4;
				}
			}
		}

		return Hotc233FastPath_Unsupported;
#endif
	}

	void TransformContext::BuildInterpMethodInfo(interpreter::InterpMethodInfo& result)
	{
		il2cpp::utils::dynamic_array<hotc233::metadata::ILMapper>* ilMappers;
		if (ir2offsetMap)
		{
			ilMappers = new il2cpp::utils::dynamic_array<hotc233::metadata::ILMapper>();
			ilMappers->reserve(ir2offsetMap->size());
		}
		else
		{
			ilMappers = nullptr;
		}
		byte* tranCodes = (byte*)HOTC233_METADATA_MALLOC(totalIRSize);

		uint32_t tranOffset = 0;
		for (IRBasicBlock* bb : irbbs)
		{
			//bb->codeOffset = tranOffset;
			for (IRCommon* ir : bb->insts)
			{
				if (ilMappers)
				{
					auto it = ir2offsetMap->find(ir);
					if (it != ir2offsetMap->end())
					{
						hotc233::metadata::ILMapper ilMapper;
						ilMapper.irOffset = tranOffset;
						ilMapper.ilOffset = it->second;
						ilMappers->push_back(ilMapper);
					}
				}
				uint32_t irSize = g_instructionSizes[(int)ir->type];
				std::memcpy(tranCodes + tranOffset, &ir->type, irSize);
				tranOffset += irSize;
			}
		}
		IL2CPP_ASSERT(tranOffset == totalIRSize);

		for (FlowInfo* fi : pendingFlows)
		{
			fi->~FlowInfo();
		}

		MethodArgDesc* argDescs;
		if (actualParamCount > 0)
		{
			argDescs = (MethodArgDesc*)HOTC233_METADATA_CALLOC(actualParamCount, sizeof(MethodArgDesc));
			for (int32_t i = 0; i < actualParamCount; i++)
			{
				const Il2CppType* argType = args[i].type;
				TypeDesc typeDesc = GetTypeArgDesc(argType);
				MethodArgDesc& argDesc = argDescs[i];
				argDesc.type = typeDesc.type;
				IL2CPP_ASSERT(typeDesc.stackObjectSize < 0x10000);
				argDesc.stackObjectSize = (uint16_t)typeDesc.stackObjectSize;
				argDesc.passbyValWhenInvoke = argType->byref || !IsValueType(argType);
			}
		}
		else
		{
			argDescs = nullptr;
		}

		result.args = argDescs;
		result.argCount = actualParamCount;
		result.argStackObjectSize = totalArgSize;
		result.retStackObjectSize = IsVoidType(methodInfo->return_type) ? 0 : GetTypeArgDesc(methodInfo->return_type).stackObjectSize;
		result.codes = tranCodes;
		result.codeLength = totalIRSize;
		result.evalStackBaseOffset = evalStackBaseOffset;
		result.localVarBaseOffset = totalArgSize;
		result.localStackSize = totalArgLocalSize;
		result.maxStackSize = maxStackSize;
		result.initLocals = initLocals;
		if (godDomainFastPathKindOverride != 0)
		{
			result.hotc233FastPathKind = godDomainFastPathKindOverride;
			result.hotc233FastPathParam = godDomainFastPathParamOverride != 0
				? godDomainFastPathParamOverride
				: ClassifyHotc233FastPathParam(
					godDomainFastPathKindOverride,
					tranCodes,
					totalIRSize);
		}
		else
		{
			result.hotc233FastPathKind = ClassifyHotc233FastPath(
				tranCodes,
				totalIRSize,
				!exClauses.empty(),
				initLocals,
				totalArgSize,
				totalArgLocalSize);
			result.hotc233FastPathParam = ClassifyHotc233FastPathParam(
				result.hotc233FastPathKind,
				tranCodes,
				totalIRSize);
		}
		if (result.hotc233FastPathParam == 0
			&& (result.hotc233FastPathKind == Hotc233FastPath_StaticF4LoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_TypeOfConstAccumI4
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceVoidI4x5LoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceVoidV3x4LoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceVoidV3x1LoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceGetTransformSetV3LoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceV3ReturnLoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_InstanceI4ReturnLoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_ArrayOpLoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_QuaternionOpLoopTrace
				|| result.hotc233FastPathKind == Hotc233FastPath_GameObjectCreateDestroyLoopTrace))
		{
			uint32_t defaultParam = ClassifyHotc233FastPathParam(
				result.hotc233FastPathKind,
				tranCodes,
				totalIRSize);
			if (defaultParam != 0)
			{
				result.hotc233FastPathParam = defaultParam;
			}
			else
			{
				result.hotc233FastPathKind = Hotc233FastPath_Unsupported;
			}
		}

		if (resolveDatas.empty())
		{
			result.resolveDatas = nullptr;
		}
		else
		{
			//result.resolveData = (uint8_t*)HOTC233_MALLOC(resolveDatas.size() * sizeof(uint8_t));
			size_t dataSize = resolveDatas.size() * sizeof(uint64_t);
			uint64_t* data = (uint64_t*)HOTC233_METADATA_MALLOC(dataSize);
			std::memcpy(data, resolveDatas.data(), dataSize);
			result.resolveDatas = data;
		}
		if (exClauses.empty())
		{
			result.exClauses = nullptr;
			result.exClauseCount = 0;
		}
		else
		{
			size_t dataSize = exClauses.size() * sizeof(InterpExceptionClause);
			InterpExceptionClause* data = (InterpExceptionClause*)HOTC233_METADATA_MALLOC(dataSize);
			std::memcpy(data, exClauses.data(), dataSize);
			result.exClauses = data;
			result.exClauseCount = (uint32_t)exClauses.size();
		}

		if (ilMappers)
		{
			image->GetPDBImage()->SetMethodDebugInfo(methodInfo, *ilMappers);
		}
	}

	bool TransformContext::TransformSubMethodBody(TransformContext& callingCtx, const MethodInfo* methodInfo, int32_t depth, int32_t localVarOffset)
	{
		metadata::Image* image = metadata::MetadataModule::GetUnderlyingInterpreterImage(methodInfo);
		IL2CPP_ASSERT(image);

		metadata::MethodBody* methodBody = metadata::MethodBodyCache::GetMethodBody(image, methodInfo->token);
		if (methodBody == nullptr || methodBody->ilcodes == nullptr)
		{
			TEMP_FORMAT(errMsg, "Method body is null. %s.%s::%s", methodInfo->klass->namespaze, methodInfo->klass->name, methodInfo->name);
			il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException(errMsg));
		}

		TransformContext ctx(image, methodInfo, *methodBody, callingCtx.pool, callingCtx.resolveDatas);

		try
		{
			ctx.TransformBodyImpl(depth, localVarOffset);
			callingCtx.maxStackSize = std::max(callingCtx.maxStackSize, ctx.maxStackSize);
			callingCtx.curbb->insts.insert(callingCtx.curbb->insts.end(), ctx.curbb->insts.begin(), ctx.curbb->insts.end());
			return true;
		}
		catch (Il2CppExceptionWrapper&)
		{
			//LOG_ERROR("TransformSubMethodBody failed: %s", ex.what());
			metadata::MethodBodyCache::DisableInline(methodInfo);
			return false;
		}

		return false;
	}

}
}
