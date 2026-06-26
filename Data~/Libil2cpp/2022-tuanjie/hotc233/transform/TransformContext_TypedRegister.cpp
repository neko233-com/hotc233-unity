#include "Hotc233TypedRegisterIR.h"
#include "TransformContext.h"

namespace hotc233
{
namespace transform
{
	static void EmitRegI32Copy(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t dst, uint16_t src)
	{
		if (dst == src)
		{
			return;
		}
		CreateIR(ir, RegI32Copy);
		ir->dst = dst;
		ir->src = src;
		out.push_back(ir);
	}

	static void EmitRegI32Ldc(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t dst, uint32_t value)
	{
		CreateIR(ir, RegI32Ldc);
		ir->dst = dst;
		ir->src = value;
		out.push_back(ir);
	}

	static void EmitRegI32Add(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t ret, uint16_t op1, uint16_t op2)
	{
		CreateIR(ir, RegI32Add);
		ir->ret = ret;
		ir->op1 = op1;
		ir->op2 = op2;
		out.push_back(ir);
	}

	static void EmitRegI32Sub(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t ret, uint16_t op1, uint16_t op2)
	{
		CreateIR(ir, RegI32Sub);
		ir->ret = ret;
		ir->op1 = op1;
		ir->op2 = op2;
		out.push_back(ir);
	}

	static void EmitRegI32Mul(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t ret, uint16_t op1, uint16_t op2)
	{
		CreateIR(ir, RegI32Mul);
		ir->ret = ret;
		ir->op1 = op1;
		ir->op2 = op2;
		out.push_back(ir);
	}

	static void EmitRegI32Xor(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t ret, uint16_t op1, uint16_t op2)
	{
		CreateIR(ir, RegI32Xor);
		ir->ret = ret;
		ir->op1 = op1;
		ir->op2 = op2;
		out.push_back(ir);
	}

	static void EmitRegI32Shr(TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out, uint16_t ret, uint16_t value, uint16_t shiftAmount)
	{
		CreateIR(ir, RegI32Shr);
		ir->ret = ret;
		ir->value = value;
		ir->shiftAmount = shiftAmount;
		out.push_back(ir);
	}

	static bool TryLowerFusedCopyConstAdd(interpreter::IRCommon* ir, TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out)
	{
		if (ir->type != interpreter::HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4)
		{
			return false;
		}
		interpreter::IRLdlocVarVar_LdcVarConst_4_BinOpAdd_i4* fused = (interpreter::IRLdlocVarVar_LdcVarConst_4_BinOpAdd_i4*)ir;
		if (fused->copyDst != fused->copySrc
			&& fused->copyDst != fused->addRet
			&& fused->copyDst != fused->addOp1
			&& fused->copyDst != fused->addOp2
			&& fused->copyDst != fused->constDst)
		{
			EmitRegI32Copy(pool, out, fused->copyDst, fused->copySrc);
		}
		EmitRegI32Ldc(pool, out, fused->constDst, fused->constant);
		EmitRegI32Add(pool, out, fused->addRet, fused->addOp1, fused->addOp2);
		return true;
	}

	static bool TryLowerFusedCopyConstMul(interpreter::IRCommon* ir, TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out)
	{
		if (ir->type != interpreter::HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4)
		{
			return false;
		}
		interpreter::IRLdlocVarVar_LdcVarConst_4_BinOpMul_i4* fused = (interpreter::IRLdlocVarVar_LdcVarConst_4_BinOpMul_i4*)ir;
		if (fused->copyDst != fused->copySrc
			&& fused->copyDst != fused->mulRet
			&& fused->copyDst != fused->mulOp1
			&& fused->copyDst != fused->mulOp2
			&& fused->copyDst != fused->constDst)
		{
			EmitRegI32Copy(pool, out, fused->copyDst, fused->copySrc);
		}
		EmitRegI32Ldc(pool, out, fused->constDst, fused->constant);
		EmitRegI32Mul(pool, out, fused->mulRet, fused->mulOp1, fused->mulOp2);
		return true;
	}

	static bool TryLowerAddCopyFused(interpreter::IRCommon* ir, TemporaryMemoryArena& pool, std::vector<interpreter::IRCommon*>& out)
	{
		if (ir->type != interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar)
		{
			return false;
		}
		interpreter::IRBinOpVarVarVar_Add_i4_LdlocVarVar* fused = (interpreter::IRBinOpVarVarVar_Add_i4_LdlocVarVar*)ir;
		EmitRegI32Add(pool, out, fused->addRet, fused->addOp1, fused->addOp2);
		if (fused->copyDst != fused->copySrc)
		{
			EmitRegI32Copy(pool, out, fused->copyDst, fused->copySrc);
		}
		return true;
	}

	static bool ConvertEligibleToReg(interpreter::IRCommon* ir, TemporaryMemoryArena& pool, interpreter::IRCommon*& converted)
	{
		switch (ir->type)
		{
		case interpreter::HiOpcodeEnum::LdlocVarVar:
		{
			interpreter::IRLdlocVarVar* copy = (interpreter::IRLdlocVarVar*)ir;
			if (copy->dst == copy->src)
			{
				return false;
			}
			CreateIR(reg, RegI32Copy);
			reg->dst = copy->dst;
			reg->src = copy->src;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::LdcVarConst_4:
		{
			interpreter::IRLdcVarConst_4* constant = (interpreter::IRLdcVarConst_4*)ir;
			CreateIR(reg, RegI32Ldc);
			reg->dst = constant->dst;
			reg->src = constant->src;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4:
		{
			interpreter::IRBinOpVarVarVar_Add_i4* add = (interpreter::IRBinOpVarVarVar_Add_i4*)ir;
			CreateIR(reg, RegI32Add);
			reg->ret = add->ret;
			reg->op1 = add->op1;
			reg->op2 = add->op2;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Sub_i4:
		{
			interpreter::IRBinOpVarVarVar_Sub_i4* sub = (interpreter::IRBinOpVarVarVar_Sub_i4*)ir;
			CreateIR(reg, RegI32Sub);
			reg->ret = sub->ret;
			reg->op1 = sub->op1;
			reg->op2 = sub->op2;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Mul_i4:
		{
			interpreter::IRBinOpVarVarVar_Mul_i4* mul = (interpreter::IRBinOpVarVarVar_Mul_i4*)ir;
			CreateIR(reg, RegI32Mul);
			reg->ret = mul->ret;
			reg->op1 = mul->op1;
			reg->op2 = mul->op2;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Xor_i4:
		{
			interpreter::IRBinOpVarVarVar_Xor_i4* xorOp = (interpreter::IRBinOpVarVarVar_Xor_i4*)ir;
			CreateIR(reg, RegI32Xor);
			reg->ret = xorOp->ret;
			reg->op1 = xorOp->op1;
			reg->op2 = xorOp->op2;
			converted = reg;
			return true;
		}
		case interpreter::HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i4:
		{
			interpreter::IRBitShiftBinOpVarVarVar_Shr_i4_i4* shr = (interpreter::IRBitShiftBinOpVarVarVar_Shr_i4_i4*)ir;
			CreateIR(reg, RegI32Shr);
			reg->ret = shr->ret;
			reg->value = shr->value;
			reg->shiftAmount = shr->shiftAmount;
			converted = reg;
			return true;
		}
		default:
			return false;
		}
	}

	void LowerTypedRegisterI32(std::vector<interpreter::IRCommon*>& insts, TemporaryMemoryArena& pool)
	{
		std::vector<interpreter::IRCommon*> lowered;
		lowered.reserve(insts.size());
		for (size_t readIdx = 0; readIdx < insts.size();)
		{
			interpreter::IRCommon* ir = insts[readIdx];
			if (TryLowerFusedCopyConstAdd(ir, pool, lowered)
				|| TryLowerFusedCopyConstMul(ir, pool, lowered)
				|| TryLowerAddCopyFused(ir, pool, lowered))
			{
				readIdx++;
				continue;
			}

			if (readIdx + 2 < insts.size()
				&& insts[readIdx]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 1]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 2]->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4)
			{
				interpreter::IRLdlocVarVar* copy1 = (interpreter::IRLdlocVarVar*)insts[readIdx];
				interpreter::IRLdlocVarVar* copy2 = (interpreter::IRLdlocVarVar*)insts[readIdx + 1];
				interpreter::IRBinOpVarVarVar_Add_i4* add = (interpreter::IRBinOpVarVarVar_Add_i4*)insts[readIdx + 2];
				if (add->op1 == copy1->dst && add->op2 == copy2->dst)
				{
					EmitRegI32Add(pool, lowered, add->ret, copy1->src, copy2->src);
					readIdx += 3;
					continue;
				}
			}

			if (readIdx + 1 < insts.size()
				&& insts[readIdx]->type == interpreter::HiOpcodeEnum::LdlocVarVar)
			{
				interpreter::IRLdlocVarVar* copy = (interpreter::IRLdlocVarVar*)insts[readIdx];
				interpreter::IRCommon* next = insts[readIdx + 1];
				if (next->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4)
				{
					interpreter::IRBinOpVarVarVar_Add_i4* add = (interpreter::IRBinOpVarVarVar_Add_i4*)next;
					if (add->op1 == copy->dst)
					{
						EmitRegI32Add(pool, lowered, add->ret, copy->src, add->op2);
						readIdx += 2;
						continue;
					}
					if (add->op2 == copy->dst)
					{
						EmitRegI32Add(pool, lowered, add->ret, add->op1, copy->src);
						readIdx += 2;
						continue;
					}
				}
				if (next->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Sub_i4)
				{
					interpreter::IRBinOpVarVarVar_Sub_i4* sub = (interpreter::IRBinOpVarVarVar_Sub_i4*)next;
					if (sub->op1 == copy->dst)
					{
						EmitRegI32Sub(pool, lowered, sub->ret, copy->src, sub->op2);
						readIdx += 2;
						continue;
					}
					if (sub->op2 == copy->dst)
					{
						EmitRegI32Sub(pool, lowered, sub->ret, sub->op1, copy->src);
						readIdx += 2;
						continue;
					}
				}
				if (next->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Mul_i4)
				{
					interpreter::IRBinOpVarVarVar_Mul_i4* mul = (interpreter::IRBinOpVarVarVar_Mul_i4*)next;
					if (mul->op1 == copy->dst)
					{
						EmitRegI32Mul(pool, lowered, mul->ret, copy->src, mul->op2);
						readIdx += 2;
						continue;
					}
					if (mul->op2 == copy->dst)
					{
						EmitRegI32Mul(pool, lowered, mul->ret, mul->op1, copy->src);
						readIdx += 2;
						continue;
					}
				}
				if (next->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Xor_i4)
				{
					interpreter::IRBinOpVarVarVar_Xor_i4* xorOp = (interpreter::IRBinOpVarVarVar_Xor_i4*)next;
					if (xorOp->op1 == copy->dst)
					{
						EmitRegI32Xor(pool, lowered, xorOp->ret, copy->src, xorOp->op2);
						readIdx += 2;
						continue;
					}
					if (xorOp->op2 == copy->dst)
					{
						EmitRegI32Xor(pool, lowered, xorOp->ret, xorOp->op1, copy->src);
						readIdx += 2;
						continue;
					}
				}
				if (next->type == interpreter::HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i4)
				{
					interpreter::IRBitShiftBinOpVarVarVar_Shr_i4_i4* shr = (interpreter::IRBitShiftBinOpVarVarVar_Shr_i4_i4*)next;
					if (shr->value == copy->dst)
					{
						EmitRegI32Shr(pool, lowered, shr->ret, copy->src, shr->shiftAmount);
						readIdx += 2;
						continue;
					}
				}
			}

			if (readIdx + 2 < insts.size()
				&& insts[readIdx]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 1]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 2]->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Xor_i4)
			{
				interpreter::IRLdlocVarVar* copy1 = (interpreter::IRLdlocVarVar*)insts[readIdx];
				interpreter::IRLdlocVarVar* copy2 = (interpreter::IRLdlocVarVar*)insts[readIdx + 1];
				interpreter::IRBinOpVarVarVar_Xor_i4* xorOp = (interpreter::IRBinOpVarVarVar_Xor_i4*)insts[readIdx + 2];
				if (xorOp->op1 == copy1->dst && xorOp->op2 == copy2->dst)
				{
					EmitRegI32Xor(pool, lowered, xorOp->ret, copy1->src, copy2->src);
					readIdx += 3;
					continue;
				}
			}

			if (readIdx + 2 < insts.size()
				&& insts[readIdx]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 1]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 2]->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Sub_i4)
			{
				interpreter::IRLdlocVarVar* copy1 = (interpreter::IRLdlocVarVar*)insts[readIdx];
				interpreter::IRLdlocVarVar* copy2 = (interpreter::IRLdlocVarVar*)insts[readIdx + 1];
				interpreter::IRBinOpVarVarVar_Sub_i4* sub = (interpreter::IRBinOpVarVarVar_Sub_i4*)insts[readIdx + 2];
				if (sub->op1 == copy1->dst && sub->op2 == copy2->dst)
				{
					EmitRegI32Sub(pool, lowered, sub->ret, copy1->src, copy2->src);
					readIdx += 3;
					continue;
				}
			}

			if (readIdx + 2 < insts.size()
				&& insts[readIdx]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 1]->type == interpreter::HiOpcodeEnum::LdlocVarVar
				&& insts[readIdx + 2]->type == interpreter::HiOpcodeEnum::BinOpVarVarVar_Mul_i4)
			{
				interpreter::IRLdlocVarVar* copy1 = (interpreter::IRLdlocVarVar*)insts[readIdx];
				interpreter::IRLdlocVarVar* copy2 = (interpreter::IRLdlocVarVar*)insts[readIdx + 1];
				interpreter::IRBinOpVarVarVar_Mul_i4* mul = (interpreter::IRBinOpVarVarVar_Mul_i4*)insts[readIdx + 2];
				if (mul->op1 == copy1->dst && mul->op2 == copy2->dst)
				{
					EmitRegI32Mul(pool, lowered, mul->ret, copy1->src, copy2->src);
					readIdx += 3;
					continue;
				}
			}

			interpreter::IRCommon* converted = nullptr;
			if (ConvertEligibleToReg(ir, pool, converted))
			{
				lowered.push_back(converted);
			}
			else
			{
				lowered.push_back(ir);
			}
			readIdx++;
		}
		insts.swap(lowered);
	}

	bool IsTypedRegisterEligibleI32(const interpreter::IRCommon* ir)
	{
		if (ir == nullptr)
		{
			return false;
		}
		switch (ir->type)
		{
		case interpreter::HiOpcodeEnum::LdlocVarVar:
		case interpreter::HiOpcodeEnum::LdlocVarVar_LdlocVarVar:
		case interpreter::HiOpcodeEnum::LdlocVarVar_LdlocVarVar_LdlocVarVar:
		case interpreter::HiOpcodeEnum::LdcVarConst_4:
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4:
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Sub_i4:
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Mul_i4:
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Xor_i4:
		case interpreter::HiOpcodeEnum::BitShiftBinOpVarVarVar_Shr_i4_i4:
		case interpreter::HiOpcodeEnum::BinOpVarVarVar_Add_i4_LdlocVarVar:
		case interpreter::HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4:
		case interpreter::HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpMul_i4:
		case interpreter::HiOpcodeEnum::LdlocVarVar_LdcVarConst_4_BinOpAdd_i4_LdlocVarVar:
		case interpreter::HiOpcodeEnum::RegI32Copy:
		case interpreter::HiOpcodeEnum::RegI32Ldc:
		case interpreter::HiOpcodeEnum::RegI32Add:
		case interpreter::HiOpcodeEnum::RegI32Sub:
		case interpreter::HiOpcodeEnum::RegI32Mul:
		case interpreter::HiOpcodeEnum::RegI32Xor:
		case interpreter::HiOpcodeEnum::RegI32Shr:
		case interpreter::HiOpcodeEnum::RunRegI32NumericTrace:
			return true;
		default:
			return false;
		}
	}

	Hotc233TypedRegisterCoverage AnalyzeTypedRegisterCoverage(const std::vector<interpreter::IRCommon*>& insts)
	{
		Hotc233TypedRegisterCoverage coverage = {};
		coverage.totalInstructions = (uint32_t)insts.size();
		uint32_t currentRun = 0;
		for (interpreter::IRCommon* ir : insts)
		{
			if (IsTypedRegisterEligibleI32(ir))
			{
				coverage.eligibleI32Instructions++;
				currentRun++;
				if (currentRun > coverage.longestI32Sequence)
				{
					coverage.longestI32Sequence = currentRun;
				}
				continue;
			}
			if (currentRun >= 3)
			{
				coverage.i32SequenceCount++;
			}
			currentRun = 0;
		}
		if (currentRun >= 3)
		{
			coverage.i32SequenceCount++;
		}
		return coverage;
	}

	void TransformContext::RecordTypedRegisterCoverage(std::vector<interpreter::IRCommon*>& insts)
	{
		Hotc233TypedRegisterCoverage coverage = AnalyzeTypedRegisterCoverage(insts);
		typedRegisterEligibleI32Instructions += coverage.eligibleI32Instructions;
		if (coverage.longestI32Sequence > typedRegisterLongestI32Sequence)
		{
			typedRegisterLongestI32Sequence = coverage.longestI32Sequence;
		}
		typedRegisterI32SequenceCount += coverage.i32SequenceCount;
	}

	void TransformContext::LowerTypedRegisterI32(std::vector<interpreter::IRCommon*>& insts)
	{
		transform::LowerTypedRegisterI32(insts, pool);
	}
}
}
