
#include "Interpreter.h"

#include <cmath>
#include <algorithm>

#include "vm/Object.h"
#include "vm/Class.h"
#include "vm/ClassInlines.h"
#include "vm/Array.h"
#include "vm/Image.h"
#include "vm/Exception.h"
#include "vm/Thread.h"
#include "vm/Runtime.h"
#include "vm/Reflection.h"
#include "metadata/GenericMetadata.h"
#if HOTC233_UNITY_2020_OR_NEW
#include "vm-utils/icalls/mscorlib/System.Threading/Interlocked.h"
#else
#include "icalls/mscorlib/System.Threading/Interlocked.h"
#endif

#include "../metadata/MetadataModule.h"

#include "../transform/Hotc233TransformPolicy.h"
#include "Instruction.h"
#include "MethodBridge.h"
#include "InstrinctDef.h"
#include "MemoryUtil.h"
#include "InterpreterModule.h"
#include "InterpreterUtil.h"
#include "gc/WriteBarrier.h"

using namespace hotc233::metadata;

namespace hotc233
{
namespace interpreter
{


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
		default:
			return false;
		}
	}

	IL2CPP_FORCE_INLINE bool TryExecuteHotc233CallFastPath(const MethodInfo* methodInfo, StackObject* argBasePtr, void* retPtr)
	{
		InterpMethodInfo* calleeImi = methodInfo->interpData ? (InterpMethodInfo*)methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(methodInfo);
		if (calleeImi->hotc233FastPathKind <= Hotc233FastPath_Unsupported)
		{
			return false;
		}
		RuntimeInitClassCCtorWithoutInitClass(methodInfo);
		return TryExecuteHotc233FastPath(calleeImi, argBasePtr, retPtr);
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
	RuntimeInitClassCCtorWithoutInitClass(newMethodInfo); \
	frame = interpFrameGroup.EnterFrameFromNative(newMethodInfo, argBasePtr); \
	frame->ret = retPtr; \
	ip = ipBase = imi->codes; \
	frame->ip = (byte*)ip; \
	localVarBase = frame->stackBasePtr; \
}

#define PREPARE_NEW_FRAME_FROM_INTERPRETER(newMethodInfo, argBasePtr, retPtr) { \
	imi = newMethodInfo->interpData ? (InterpMethodInfo*)newMethodInfo->interpData : InterpreterModule::GetInterpMethodInfo(newMethodInfo); \
	RuntimeInitClassCCtorWithoutInitClass(newMethodInfo); \
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
	SAVE_CUR_FRAME(nextIp) \
	PREPARE_NEW_FRAME_FROM_INTERPRETER_PREPARED(methodInfo, preparedImi, argBasePtr, retPtr); \
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

IL2CPP_FORCE_INLINE bool TryExecuteCachedSingleInterpDelegateFastPath(uint64_t* cache, Il2CppMulticastDelegate* del, uint16_t invokeParamCount, StackObject* argBasePtr, void* ret)
{
	if ((Il2CppMulticastDelegate*)cache[0] != del)
	{
		return false;
	}
	InterpMethodInfo* methodImi = (InterpMethodInfo*)cache[1];
	if (!methodImi || methodImi->hotc233FastPathKind <= Hotc233FastPath_Unsupported)
	{
		return false;
	}
	const MethodInfo* method = del->delegate.method;
	Il2CppObject* target = del->delegate.target;
	StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(invokeParamCount, method, target, argBasePtr);
	if (!fastArgBase)
	{
		return false;
	}
	if (TryExecuteHotc233FastPath(methodImi, fastArgBase, ret))
	{
		return true;
	}
	argBasePtr->obj = (Il2CppObject*)del;
	cache[0] = 0;
	cache[1] = 0;
	return false;
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
		InterpMethodInfo* directImi = methodInfo->interpData ? (InterpMethodInfo*)methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(methodInfo);
		RuntimeInitClassCCtorWithoutInitClass(methodInfo);
		if (directImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(directImi, args, ret))
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
#if HOTC233_COMMUNITY_BASELINE
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
					continue;
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
					(*(int32_t*)(localVarBase + __dst)) = (*(int8_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_u1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(uint8_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(int16_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_u2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(uint16_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i4:
				HOTC233_EXEC_LdindVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)(localVarBase + __dst)) = (*(int32_t*)*(void**)(localVarBase + __src));
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
					(*(int32_t*)(localVarBase + __dst)) = (*(uint32_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_i8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int64_t*)(localVarBase + __dst)) = (*(int64_t*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)(localVarBase + __dst)) = (*(float*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::LdindVarVar_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)(localVarBase + __dst)) = (*(double*)*(void**)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i1:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int8_t*)*(void**)(localVarBase + __dst)) = (*(int8_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i2:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int16_t*)*(void**)(localVarBase + __dst)) = (*(int16_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_i4:
				HOTC233_EXEC_StindVarVar_i4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(int32_t*)*(void**)(localVarBase + __dst)) = (*(int32_t*)(localVarBase + __src));
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
					(*(int64_t*)*(void**)(localVarBase + __dst)) = (*(int64_t*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_f4:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(float*)*(void**)(localVarBase + __dst)) = (*(float*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_f8:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(double*)*(void**)(localVarBase + __dst)) = (*(double*)(localVarBase + __src));
				    ip += 8;
				    continue;
				}
				case HiOpcodeEnum::StindVarVar_ref:
				{
					uint16_t __dst = *(uint16_t*)(ip + 2);
					uint16_t __src = *(uint16_t*)(ip + 4);
					(*(Il2CppObject**)*(void**)(localVarBase + __dst)) = (*(Il2CppObject**)(localVarBase + __src));	HOTC233_SET_WRITE_BARRIER((void**)(*(void**)(localVarBase + __dst)));
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
#if HOTC233_COMMUNITY_BASELINE
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
#if HOTC233_COMMUNITY_BASELINE
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
#if !HOTC233_COMMUNITY_BASELINE
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
#if HOTC233_COMMUNITY_BASELINE
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
				    SET_RET_AND_LEAVE_FRAME(8, 8);
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
				    frame->ip = ip + 2;
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(((MethodInfo*)imi->resolveDatas[__methodInfo]), _resolvedArgIdxs, localVarBase, nullptr);
				    ip += 16;
				    continue;
				}
				case HiOpcodeEnum::CallNativeInstance_ret:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
					uint16_t __ret = *(uint16_t*)(ip + 2);
				    frame->ip = ip + 2;
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
				    void* _ret = (void*)(localVarBase + __ret);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(((MethodInfo*)imi->resolveDatas[__methodInfo]), _resolvedArgIdxs, localVarBase, _ret);
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
				    frame->ip = ip + 2;
				    uint16_t* _resolvedArgIdxs = ((uint16_t*)&imi->resolveDatas[__argIdxs]);
				    CHECK_NOT_NULL_THROW((localVarBase + _resolvedArgIdxs[0])->obj);
				    void* _ret = (void*)(localVarBase + __ret);
				    ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(((MethodInfo*)imi->resolveDatas[__methodInfo]), _resolvedArgIdxs, localVarBase, _ret);
				    ExpandLocationData2StackDataByType(_ret, (LocationDataType)__retLocationType);
				    ip += 24;
				    continue;
				}
				case HiOpcodeEnum::CallNativeStatic_void:
				{
					uint32_t __managed2NativeMethod = *(uint32_t*)(ip + 4);
					uint32_t __methodInfo = *(uint32_t*)(ip + 8);
					uint32_t __argIdxs = *(uint32_t*)(ip + 12);
				    frame->ip = ip + 2;
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
				    frame->ip = ip + 2;
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
				    frame->ip = ip + 2;
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
					if (__isInstanceMethod)
					{
						CHECK_NOT_NULL_THROW((localVarBase + __argBase)->obj);
					}
					InterpMethodInfo* __calleeImi = __methodInfo->interpData ? (InterpMethodInfo*)__methodInfo->interpData : InterpreterModule::GetInterpMethodInfo(__methodInfo);
					RuntimeInitClassCCtorWithoutInitClass(__methodInfo);
					void* __retPtr = (void*)(localVarBase + __ret);
					StackObject* __argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
					if (__calleeImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(__calleeImi, __argBasePtr, __retPtr))
					{
						ip += 16;
						continue;
					}
					CALL_INTERP_RET_PREPARED((ip + 16), __methodInfo, __calleeImi, __argBasePtr, __retPtr);
				    continue;
				}
				case HiOpcodeEnum::CallInterpStatic_ret:
				{
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
					if (__calleeImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(__calleeImi, __argBasePtr, __retPtr))
					{
						ip += 16;
						continue;
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
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD( _objPtr->obj, ((MethodInfo*)imi->resolveDatas[__methodInfo]));
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _objPtr->obj += 1;
				    }
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod))
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
				        ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_actualMethod, _argIdxData, localVarBase, nullptr);
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
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_objPtr->obj, ((MethodInfo*)imi->resolveDatas[__methodInfo]));
				    void* _ret = (void*)(localVarBase + __ret);
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _objPtr->obj += 1;
				    }
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod))
				    {
				        InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				        RuntimeInitClassCCtorWithoutInitClass(_actualMethod);
				        if (_actualImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(_actualImi, _objPtr, _ret))
				        {
				            ip += 16;
				            continue;
				        }
				        CALL_INTERP_RET_PREPARED((ip + 16), _actualMethod, _actualImi, _objPtr, _ret);
				    }
				    else 
				    {
				        frame->ip = ip + 2;
				        if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				        {
				            RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				        }
				        ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_actualMethod, _argIdxData, localVarBase, _ret);
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
				    if (hotc233::metadata::IsInterpreterImplement(_actualMethod))
				    {
				        InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				        RuntimeInitClassCCtorWithoutInitClass(_actualMethod);
				        if (_actualImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(_actualImi, _objPtr, _ret))
				        {
				            ip += 24;
				            continue;
				        }
				        CALL_INTERP_RET_PREPARED((ip + 24), _actualMethod, _actualImi, _objPtr, _ret);
				    }
				    else 
				    {
				        frame->ip = ip + 2;
				        if (!InitAndGetInterpreterDirectlyCallMethodPointer(_actualMethod))
				        {
				            RaiseAOTGenericMethodNotInstantiatedException(_actualMethod);
				        }
				        ((Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeMethod])(_actualMethod, _argIdxData, localVarBase, _ret);
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
				    CALL_INTERP_VOID((ip + 8), _actualMethod, _argBasePtr);
				    continue;
				}
				case HiOpcodeEnum::CallInterpVirtual_ret:
				{
					MethodInfo* __method = ((MethodInfo*)imi->resolveDatas[*(uint32_t*)(ip + 8)]);
					uint16_t __argBase = *(uint16_t*)(ip + 2);
					uint16_t __ret = *(uint16_t*)(ip + 4);
				    StackObject* _argBasePtr = (StackObject*)(void*)(localVarBase + __argBase);
				    MethodInfo* _actualMethod = GET_OBJECT_VIRTUAL_METHOD(_argBasePtr->obj, __method);
				    if (IS_CLASS_VALUE_TYPE(_actualMethod->klass))
				    {
				        _argBasePtr->obj += 1;
				    }
				    void* _ret = (void*)(localVarBase + __ret);
				    InterpMethodInfo* _actualImi = _actualMethod->interpData ? (InterpMethodInfo*)_actualMethod->interpData : InterpreterModule::GetInterpMethodInfo(_actualMethod);
				    RuntimeInitClassCCtorWithoutInitClass(_actualMethod);
				    if (_actualImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(_actualImi, _argBasePtr, _ret))
				    {
				        ip += 16;
				        continue;
				    }
				    CALL_INTERP_RET_PREPARED((ip + 16), _actualMethod, _actualImi, _argBasePtr, _ret);
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
					    if (IsInterpreterImplement(_method))
					    {
				            CALL_INTERP_VOID((ip + 24), _method, _argBasePtr);
				            continue;
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
					    if (IsInterpreterImplement(_method))
					    {
				            InterpMethodInfo* _methodImi = _method->interpData ? (InterpMethodInfo*)_method->interpData : InterpreterModule::GetInterpMethodInfo(_method);
				            RuntimeInitClassCCtorWithoutInitClass(_method);
				            if (_methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(_methodImi, _argBasePtr, _ret))
				            {
				                ip += 24;
				                continue;
				            }
				            CALL_INTERP_RET_PREPARED((ip + 24), _method, _methodImi, _argBasePtr, _ret);
				            continue;
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
					    if (IsInterpreterImplement(_method))
					    {
				            InterpMethodInfo* _methodImi = _method->interpData ? (InterpMethodInfo*)_method->interpData : InterpreterModule::GetInterpMethodInfo(_method);
				            RuntimeInitClassCCtorWithoutInitClass(_method);
				            if (_methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(_methodImi, _argBasePtr, _ret))
				            {
				                ip += 24;
				                continue;
				            }
				            CALL_INTERP_RET_PREPARED((ip + 24), _method, _methodImi, _argBasePtr, _ret);
				            continue;
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
					if (_del->delegates == nullptr)
					{
						if (TryExecuteCachedSingleInterpDelegateFastPath(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, _ret))
						{
							ip += 20;
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (hotc233::metadata::IsInterpreterImplement(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								RuntimeInitClassCCtorWithoutInitClass(method);
								if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 20;
									continue;
								}
								CALL_INTERP_RET_PREPARED((ip + 20), method, methodImi, fastArgBase, _ret);
								continue;
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
							RuntimeInitClassCCtorWithoutInitClass(method);
							if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 20;
								continue;
							}
							CALL_INTERP_RET_PREPARED((ip + 20), method, methodImi, _argBasePtr, _ret);
							continue;
						}
						else
						{
							Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
							Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _ret);
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
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _ret);
						}
					}
				    ip += 16;
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
					if (_del->delegates == nullptr)
					{
						if (TryExecuteCachedSingleInterpDelegateFastPath(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, _ret))
						{
							ip += 24;
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (hotc233::metadata::IsInterpreterImplement(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								RuntimeInitClassCCtorWithoutInitClass(method);
								if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 24;
									continue;
								}
								CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, fastArgBase, _ret);
								continue;
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
							RuntimeInitClassCCtorWithoutInitClass(method);
							if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 24;
								continue;
							}
							CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, _argBasePtr, _ret);
							continue;
						}
						else
						{
							Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
							Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
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
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
						}
					}
					CopyStackObject((StackObject*)_ret, _tempRet, __retTypeStackObjectSize);
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
						if (TryExecuteCachedSingleInterpDelegateFastPath(_interpDelegateCache, _del, __invokeParamCount, _argBasePtr, _ret))
						{
							ip += 24;
							continue;
						}
						const MethodInfo* method = _del->delegate.method;
						Il2CppObject* target = _del->delegate.target;
						if (hotc233::metadata::IsInterpreterImplement(method))
						{
							if (StackObject* fastArgBase = TryPrepareClosedInstanceInterpDelegate(__invokeParamCount, method, target, _argBasePtr))
							{
								InterpMethodInfo* methodImi = method->interpData ? (InterpMethodInfo*)method->interpData : InterpreterModule::GetInterpMethodInfo(method);
								RuntimeInitClassCCtorWithoutInitClass(method);
								if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, fastArgBase, _ret))
								{
									_interpDelegateCache[0] = (uint64_t)_del;
									_interpDelegateCache[1] = (uint64_t)methodImi;
									ip += 24;
									continue;
								}
								CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, fastArgBase, _ret);
								continue;
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
							RuntimeInitClassCCtorWithoutInitClass(method);
							if (methodImi->hotc233FastPathKind > Hotc233FastPath_Unsupported && TryExecuteHotc233FastPath(methodImi, _argBasePtr, _ret))
							{
								_interpDelegateCache[0] = (uint64_t)_del;
								_interpDelegateCache[1] = (uint64_t)methodImi;
								ip += 24;
								continue;
							}
							CALL_INTERP_RET_PREPARED((ip + 24), method, methodImi, _argBasePtr, _ret);
							continue;
						}
						else
						{
							Managed2NativeCallMethod _staticM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeStaticMethod];
							Managed2NativeCallMethod _instanceM2NMethod = (Managed2NativeCallMethod)imi->resolveDatas[__managed2NativeInstanceMethod];
							InvokeSingleDelegate(__invokeParamCount, method, target, _staticM2NMethod, _instanceM2NMethod, _resolvedArgIdxs, localVarBase, _tempRet);
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
				    typedef uint8_t(*_NativeMethod_)(void*, MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, _resolvedMethod);
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
				    typedef void(*_NativeMethod_)(void*, int32_t, MethodInfo*);
				    ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_self, (*(int32_t*)(localVarBase + __param0)), _resolvedMethod);
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
#if !HOTC233_COMMUNITY_BASELINE
				case HiOpcodeEnum::CallCommonNativeStatic_i4_0Cached:
				{
					uint32_t __method = *(uint32_t*)(ip + 4);
					uint16_t __ret = *(uint16_t*)(ip + 2);
					uint32_t __thunkCache = *(uint32_t*)(ip + 8);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    uint64_t& __cachedThunk = imi->resolveDatas[__thunkCache];
				    if (__cachedThunk == 0)
				    {
				    	RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    	__cachedThunk = (uint64_t)_resolvedMethod->methodPointerCallByInterp;
				    }
				    typedef int32_t(*_NativeMethod_)(MethodInfo*);
				    *(int32_t*)(void*)(localVarBase + __ret) = ((_NativeMethod_)__cachedThunk)(_resolvedMethod);
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
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(MethodInfo*);
				    *(float*)(void*)(localVarBase + __ret) = ((_NativeMethod_)_resolvedMethod->methodPointerCallByInterp)(_resolvedMethod);
				    ip += 8;
				    continue;
				}
#if !HOTC233_COMMUNITY_BASELINE
				case HiOpcodeEnum::RunStaticF4CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					uint32_t __method = *(uint32_t*)(ip + 8);
				    frame->ip = ip + 2;
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    typedef float(*_NativeMethod_)(MethodInfo*);
					_NativeMethod_ _method = (_NativeMethod_)_resolvedMethod->methodPointerCallByInterp;
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __copyDst = (uint16_t)(__word >> 16);
						uint16_t __copySrc = (uint16_t)(__word >> 32);
						*(float*)(void*)(localVarBase + __ret) = _method(_resolvedMethod);
						if (__copyDst != 0xffff)
						{
							(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
						}
					}
				    ip += 12;
				    continue;
				}
				case HiOpcodeEnum::RunStaticI4CallTrace:
				{
					uint16_t __stepCount = *(uint16_t*)(ip + 2);
					uint64_t* __trace = &imi->resolveDatas[*(uint32_t*)(ip + 4)];
					uint32_t __method = *(uint32_t*)(ip + 8);
					uint32_t __thunkCache = *(uint32_t*)(ip + 12);
				    MethodInfo* _resolvedMethod = ((MethodInfo*)imi->resolveDatas[__method]);
				    uint64_t& __cachedThunk = imi->resolveDatas[__thunkCache];
				    if (__cachedThunk == 0)
				    {
				    	RuntimeInitClassCCtorWithoutInitClass(_resolvedMethod);
				    	__cachedThunk = (uint64_t)_resolvedMethod->methodPointerCallByInterp;
				    }
				    typedef int32_t(*_NativeMethod_)(MethodInfo*);
					_NativeMethod_ _method = (_NativeMethod_)__cachedThunk;
					for (uint16_t __step = 0; __step < __stepCount; __step++)
					{
						uint64_t __word = __trace[__step];
						uint16_t __ret = (uint16_t)__word;
						uint16_t __copyDst = (uint16_t)(__word >> 16);
						uint16_t __copySrc = (uint16_t)(__word >> 32);
						*(int32_t*)(void*)(localVarBase + __ret) = _method(_resolvedMethod);
						if (__copyDst != 0xffff)
						{
							(*(uint64_t*)(localVarBase + __copyDst)) = (*(uint64_t*)(localVarBase + __copySrc));
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
#if !HOTC233_COMMUNITY_BASELINE
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
#if !HOTC233_COMMUNITY_BASELINE
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
#if !HOTC233_COMMUNITY_BASELINE
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
#if !HOTC233_COMMUNITY_BASELINE
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
				    ip += 20;
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

