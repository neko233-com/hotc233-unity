#pragma once

#include "../interpreter/Instruction.h"

namespace hotc233
{
namespace transform
{
	enum class Hotc233TypedSlotKind : uint8_t
	{
		Void = 0,
		I32 = 1,
		I64 = 2,
		F32 = 3,
		F64 = 4,
		Ref = 5,
		Vector3 = 6,
		Quaternion = 7,
	};

	enum class Hotc233TypedRegOpKind : uint8_t
	{
		None = 0,
		Copy = 1,
		ConstI32 = 2,
		AddI32 = 3,
		SubI32 = 4,
		MulI32 = 5,
		XorI32 = 6,
		ShrI32 = 7,
	};

	struct Hotc233TypedRegisterCoverage
	{
		uint32_t totalInstructions;
		uint32_t eligibleI32Instructions;
		uint32_t longestI32Sequence;
		uint32_t i32SequenceCount;
	};

	bool IsTypedRegisterEligibleI32(const interpreter::IRCommon* ir);
	Hotc233TypedRegisterCoverage AnalyzeTypedRegisterCoverage(const std::vector<interpreter::IRCommon*>& insts);
	void LowerTypedRegisterI32(std::vector<interpreter::IRCommon*>& insts, TemporaryMemoryArena& pool);
	void LowerTypedRegisterVector3(std::vector<interpreter::IRCommon*>& insts, TemporaryMemoryArena& pool);
}
}
