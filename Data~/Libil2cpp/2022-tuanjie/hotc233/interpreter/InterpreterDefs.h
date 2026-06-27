#pragma once
#include "../CommonDef.h"
#include "../metadata/MetadataDef.h"

namespace hotc233
{
	namespace interpreter
	{

		// from obj or arg
		enum class LocationDataType : uint8_t
		{
			I1,
			U1,
			I2,
			U2,
			U8,
			S_N,  // struct size = 3，5，6，7， > 8, size is described by stackObjectSize
		};

		union StackObject
		{
			uint64_t __u64;
			void* ptr; // can't adjust position. will raise native_invoke init args bugs.
			bool b;
			int8_t i8;
			uint8_t u8;
			int16_t i16;
			uint16_t u16;
			int32_t i32;
			uint32_t u32;
			int64_t i64;
			uint64_t u64;
			float f4;
			double f8;
			Il2CppObject* obj;
			Il2CppString* str;
			Il2CppObject** ptrObj;
		};

		static_assert(sizeof(StackObject) == 8, "require 8 bytes");


		enum class ExceptionFlowType
		{
			Exception,
			Catch,
			Leave,
		};

		struct InterpMethodInfo;

		struct ExceptionFlowInfo
		{
			ExceptionFlowType exFlowType;
			int32_t throwOffset;
			Il2CppException* ex;
			int32_t nextExClauseIndex;
			int32_t leaveTarget;
		};

		struct InterpFrame
		{
			const MethodInfo* method;
			StackObject* stackBasePtr;
			int32_t oldStackTop;
			void* ret;
			byte* ip;

			ExceptionFlowInfo* exFlowBase;
			int32_t exFlowCount;
			int32_t exFlowCapaticy;

			int32_t oldLocalPoolBottomIdx;

			ExceptionFlowInfo* GetCurExFlow() const
			{
				return exFlowCount > 0 ? exFlowBase + exFlowCount - 1 : nullptr;
			}

			ExceptionFlowInfo* GetPrevExFlow() const
			{
				return exFlowCount > 1 ? exFlowBase + exFlowCount - 2 : nullptr;
			}
		};

		struct InterpExceptionClause
		{
			metadata::CorILExceptionClauseType flags;
			int32_t tryBeginOffset;
			int32_t tryEndOffset;
			int32_t handlerBeginOffset;
			int32_t handlerEndOffset;
			int32_t filterBeginOffset;
			Il2CppClass* exKlass;
		};

		struct MethodArgDesc
		{
			bool passbyValWhenInvoke;
			LocationDataType type;
			uint16_t stackObjectSize;
		};

		enum Hotc233FastPathKind : uint32_t
		{
			Hotc233FastPath_None = 0,
			Hotc233FastPath_Unsupported = 1,
			Hotc233FastPath_ReturnI4 = 2,
			Hotc233FastPath_ReturnI8 = 3,
			Hotc233FastPath_ConstI4 = 4,
			Hotc233FastPath_ConstI8 = 5,
			Hotc233FastPath_EmptyVoid = 6,
			Hotc233FastPath_AddI4 = 10,
			Hotc233FastPath_SubI4 = 11,
			Hotc233FastPath_MulI4 = 12,
			Hotc233FastPath_AndI4 = 13,
			Hotc233FastPath_OrI4 = 14,
			Hotc233FastPath_XorI4 = 15,
			Hotc233FastPath_AddI8 = 20,
			Hotc233FastPath_SubI8 = 21,
			Hotc233FastPath_MulI8 = 22,
			Hotc233FastPath_AndI8 = 23,
			Hotc233FastPath_OrI8 = 24,
			Hotc233FastPath_XorI8 = 25,
			Hotc233FastPath_CopyConstMulRetI4 = 30,
			Hotc233FastPath_ClosureMulConstAddFieldI4 = 31,
			Hotc233FastPath_StaticF4LoopTrace = 32,
			Hotc233FastPath_InstanceVoidI4x5LoopTrace = 33,
			Hotc233FastPath_TypeOfConstAccumI4 = 34,
			Hotc233FastPath_InstanceVoidV3x1LoopTrace = 35,
			Hotc233FastPath_InstanceGetTransformSetV3LoopTrace = 36,
			Hotc233FastPath_InstanceV3ReturnLoopTrace = 37,
			Hotc233FastPath_ArrayOpLoopTrace = 38,
			Hotc233FastPath_QuaternionOpLoopTrace = 39,
		};

		struct InterpMethodInfo
		{
			byte* codes;
			MethodArgDesc* args;
			uint64_t* resolveDatas;
			const InterpExceptionClause* exClauses;
			uint32_t argStackObjectSize;
			uint32_t retStackObjectSize : 24;
			uint32_t initLocals : 8;
			uint32_t localStackSize; // args + locals StackObject size
			uint32_t maxStackSize; // args + locals + evalstack size
			uint32_t argCount : 8;
			uint32_t codeLength : 24;
			uint32_t localVarBaseOffset;
			uint32_t evalStackBaseOffset;
			uint32_t exClauseCount;
			uint32_t hotc233FastPathKind;
			uint32_t hotc233FastPathParam;
		};
	}
}
