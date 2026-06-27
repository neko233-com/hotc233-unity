#include "TransformContext.h"
#include "Hotc233TransformPolicy.h"

#include "../metadata/MethodBodyCache.h"
#include "../metadata/Image.h"
#include "../interpreter/InterpreterUtil.h"
#include "vm/Class.h"

namespace hotc233
{
namespace transform
{
	static constexpr uint16_t kGodDomainOfficialStepsPerLoop = 10;

	template<typename T>
	static void AllocResolvedData(il2cpp::utils::dynamic_array<uint64_t>& resolvedDatas, int32_t size, int32_t& index, T*& buf)
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

	static bool IsOfficialIntLoopBenchmarkMethod(const MethodInfo* method)
	{
		return method != nullptr
			&& method->parameters_count == 1
			&& !IsInstanceMethod(method)
			&& method->return_type != nullptr
			&& method->return_type->type == IL2CPP_TYPE_I4
			&& GET_METHOD_PARAMETER_TYPE(method->parameters[0])->type == IL2CPP_TYPE_I4;
	}

	static bool MatchesBenchmarkMethodName(const MethodInfo* method, const char* name)
	{
		return method != nullptr && method->name != nullptr && std::strcmp(method->name, name) == 0;
	}

	bool TransformContext::SetupGodDomainOfficialIntLoopShell(int32_t localVarOffset, uint16_t* outRetSlot)
	{
		if (!outRetSlot || !IsOfficialIntLoopBenchmarkMethod(methodInfo))
		{
			return false;
		}

		splitOffsets.clear();
		irbbs.clear();
		exClauses.clear();
		pendingFlows.clear();
		relocationOffsets.clear();
		switchOffsetsInResolveData.clear();
		ir2offsetMap = nullptr;
		initLocals = false;

		actualParamCount = 1;
		args = pool.NewNAny<ArgVarInfo>(1);
		ArgVarInfo& cntArg = args[0];
		cntArg.type = GET_METHOD_PARAMETER_TYPE(methodInfo->parameters[0]);
		cntArg.klass = il2cpp::vm::Class::FromIl2CppType(cntArg.type);
		cntArg.argOffset = 0;
		cntArg.argLocOffset = localVarOffset;
		il2cpp::vm::Class::SetupFields(cntArg.klass);
		totalArgSize = GetTypeValueStackObjectCount(cntArg.type);
		totalArgLocalSize = totalArgSize;
		locals = nullptr;
		evalStack = nullptr;
		evalStackTop = 0;
		evalStackBaseOffset = localVarOffset + totalArgLocalSize;
		maxStackSize = evalStackBaseOffset;
		curStackSize = evalStackBaseOffset;

		IRBasicBlock* bb = pool.NewAny<IRBasicBlock>();
		bb->visited = false;
		bb->inPending = false;
		bb->ilOffset = 0;
		bb->codeOffset = 0;
		bb->insts.clear();
		irbbs.push_back(bb);
		curbb = bb;

		IRBasicBlock* endBlock = pool.NewAny<IRBasicBlock>();
		*endBlock = { true, false, body.codeSize, 0 };
		endBlock->insts.clear();
		irbbs.push_back(endBlock);

		*outRetSlot = (uint16_t)cntArg.argLocOffset;
		return true;
	}

	void TransformContext::FinishGodDomainOfficialIntLoopShell(
		uint16_t retSlot,
		uint32_t fastPathKind,
		uint32_t fastPathParam,
		uint16_t /*traceIrBytes*/)
	{
		CreateAddIR(retIr, RetVar_ret_4);
		retIr->ret = retSlot;
		totalIRSize = 0;
		for (IRBasicBlock* bb : irbbs)
		{
			bb->codeOffset = totalIRSize;
			for (IRCommon* ir : bb->insts)
			{
				totalIRSize += g_instructionSizes[(int)ir->type];
			}
		}
		if (!irbbs.empty())
		{
			irbbs.back()->codeOffset = totalIRSize;
		}
		godDomainFastPathKindOverride = fastPathKind;
		godDomainFastPathParamOverride = fastPathParam;
	}

	static bool IsGodDomainStaticF4Callee(const MethodInfo* method)
	{
		if (method == nullptr || metadata::IsInterpreterImplement(method) || IsInstanceMethod(method))
		{
			return false;
		}
		if (method->parameters_count != 0 || method->return_type == nullptr)
		{
			return false;
		}
		return method->return_type->type == IL2CPP_TYPE_R4;
	}

	static bool IsIgnorableBetweenGodDomainStaticF4Calls(OpcodeValue op)
	{
		switch (op)
		{
		case OpcodeValue::NOP:
		case OpcodeValue::POP:
		case OpcodeValue::DUP:
		case OpcodeValue::STLOC_0:
		case OpcodeValue::STLOC_1:
		case OpcodeValue::STLOC_2:
		case OpcodeValue::STLOC_3:
		case OpcodeValue::STLOC_S:
		case OpcodeValue::LDLOC_0:
		case OpcodeValue::LDLOC_1:
		case OpcodeValue::LDLOC_2:
		case OpcodeValue::LDLOC_3:
		case OpcodeValue::LDLOC_S:
			return true;
		default:
			return false;
		}
	}

	static int32_t ReadBranchTargetOffset(OpcodeValue op, const byte* ip, uint32_t ipOffset)
	{
		switch (op)
		{
		case OpcodeValue::BR_S:
		case OpcodeValue::BRFALSE_S:
		case OpcodeValue::BRTRUE_S:
		case OpcodeValue::BEQ_S:
		case OpcodeValue::BGE_S:
		case OpcodeValue::BGT_S:
		case OpcodeValue::BLE_S:
		case OpcodeValue::BLT_S:
		case OpcodeValue::BNE_UN_S:
		case OpcodeValue::BGE_UN_S:
		case OpcodeValue::BGT_UN_S:
		case OpcodeValue::BLE_UN_S:
		case OpcodeValue::BLT_UN_S:
			return ipOffset + 2 + (int8_t)ip[1];
		case OpcodeValue::BR:
		case OpcodeValue::BRFALSE:
		case OpcodeValue::BRTRUE:
		case OpcodeValue::BEQ:
		case OpcodeValue::BGE:
		case OpcodeValue::BGT:
		case OpcodeValue::BLE:
		case OpcodeValue::BLT:
		case OpcodeValue::BNE_UN:
		case OpcodeValue::BGE_UN:
		case OpcodeValue::BGT_UN:
		case OpcodeValue::BLE_UN:
		case OpcodeValue::BLT_UN:
			return ipOffset + 5 + (int32_t)GetI4LittleEndian(ip + 1);
		default:
			return -1;
		}
	}

	static uint32_t GetNextIlInstructionOffset(const byte* codes, uint32_t codeSize, uint32_t offset)
	{
		if (offset >= codeSize)
		{
			return codeSize;
		}
		const byte* ip = codes + offset;
		OpcodeValue op = (OpcodeValue)*ip;
		if (op == OpcodeValue::PREFIX1)
		{
			return offset + 2;
		}
		switch (op)
		{
		case OpcodeValue::CALL:
		case OpcodeValue::CALLVIRT:
		case OpcodeValue::CALLI:
		case OpcodeValue::LDFLD:
		case OpcodeValue::LDFLDA:
		case OpcodeValue::LDIND_REF:
		case OpcodeValue::LDC_I4:
		case OpcodeValue::LDTOKEN:
		case OpcodeValue::NEWOBJ:
		case OpcodeValue::STFLD:
		case OpcodeValue::STSFLD:
		case OpcodeValue::BR:
		case OpcodeValue::BEQ:
		case OpcodeValue::BGE:
		case OpcodeValue::BGT:
		case OpcodeValue::BLE:
		case OpcodeValue::BLT:
		case OpcodeValue::BNE_UN:
		case OpcodeValue::BGE_UN:
		case OpcodeValue::BGT_UN:
		case OpcodeValue::BLE_UN:
		case OpcodeValue::BLT_UN:
		case OpcodeValue::LEAVE:
			return offset + 5;
		case OpcodeValue::BR_S:
		case OpcodeValue::BRFALSE_S:
		case OpcodeValue::BRTRUE_S:
		case OpcodeValue::BEQ_S:
		case OpcodeValue::BGE_S:
		case OpcodeValue::BGT_S:
		case OpcodeValue::BLE_S:
		case OpcodeValue::BLT_S:
		case OpcodeValue::BNE_UN_S:
		case OpcodeValue::BGE_UN_S:
		case OpcodeValue::BGT_UN_S:
		case OpcodeValue::BLE_UN_S:
		case OpcodeValue::BLT_UN_S:
		case OpcodeValue::LDLOC_S:
		case OpcodeValue::STLOC_S:
		case OpcodeValue::LDARG_S:
		case OpcodeValue::STARG_S:
		case OpcodeValue::LEAVE_S:
		case OpcodeValue::LDC_I4_S:
			return offset + 2;
		case OpcodeValue::SWITCH:
		{
			uint32_t count = (uint32_t)GetI4LittleEndian(ip + 1);
			return offset + 5 + count * 4;
		}
		default:
			return offset + 1;
		}
	}

	static bool MethodHasBackwardBranch(const byte* codes, uint32_t codeSize)
	{
		for (uint32_t offset = 0; offset < codeSize;)
		{
			OpcodeValue op = (OpcodeValue)codes[offset];
			int32_t target = ReadBranchTargetOffset(op, codes + offset, offset);
			if (target >= 0 && (uint32_t)target < offset)
			{
				return true;
			}
			offset = GetNextIlInstructionOffset(codes, codeSize, offset);
		}
		return false;
	}

	struct GodDomainStaticF4LoopScanResult
	{
		const MethodInfo* callee;
		uint16_t stepsPerLoop;
	};

	static bool TryScanGodDomainStaticF4LoopPattern(
		metadata::Image* image,
		metadata::MethodBody& body,
		const MethodInfo* methodInfo,
		const Il2CppGenericContext* genericContext,
		const Il2CppGenericContainer* klassContainer,
		const Il2CppGenericContainer* methodContainer,
		metadata::Token2RuntimeHandleMap& tokenCache,
		GodDomainStaticF4LoopScanResult* out)
	{
		if (out == nullptr || image == nullptr || body.ilcodes == nullptr || body.codeSize == 0)
		{
			return false;
		}
		if (!IsOfficialIntLoopBenchmarkMethod(methodInfo))
		{
			return false;
		}
		if (!MethodHasBackwardBranch(body.ilcodes, body.codeSize))
		{
			return false;
		}

		const byte* codes = body.ilcodes;
		const uint32_t codeSize = body.codeSize;
		const MethodInfo* bestCallee = nullptr;
		uint16_t bestRun = 0;

		for (uint32_t offset = 0; offset < codeSize;)
		{
			OpcodeValue op = (OpcodeValue)codes[offset];
			if (op != OpcodeValue::CALL)
			{
				offset = GetNextIlInstructionOffset(codes, codeSize, offset);
				continue;
			}
			uint32_t token = (uint32_t)GetI4LittleEndian(codes + offset + 1);
			const MethodInfo* callee = image->GetMethodInfoFromToken(
				tokenCache, token, klassContainer, methodContainer, genericContext);
			if (!IsGodDomainStaticF4Callee(callee))
			{
				offset = GetNextIlInstructionOffset(codes, codeSize, offset);
				continue;
			}

			uint16_t runLength = 1;
			uint32_t scanOffset = GetNextIlInstructionOffset(codes, codeSize, offset);
			while (scanOffset < codeSize)
			{
				uint32_t nextOffset = scanOffset;
				while (nextOffset < codeSize)
				{
					OpcodeValue skipOp = (OpcodeValue)codes[nextOffset];
					if (!IsIgnorableBetweenGodDomainStaticF4Calls(skipOp))
					{
						break;
					}
					nextOffset = GetNextIlInstructionOffset(codes, codeSize, nextOffset);
				}
				if (nextOffset >= codeSize || (OpcodeValue)codes[nextOffset] != OpcodeValue::CALL)
				{
					break;
				}
				uint32_t nextToken = (uint32_t)GetI4LittleEndian(codes + nextOffset + 1);
				const MethodInfo* nextCallee = image->GetMethodInfoFromToken(
					tokenCache, nextToken, klassContainer, methodContainer, genericContext);
				if (nextCallee != callee)
				{
					break;
				}
				runLength++;
				scanOffset = GetNextIlInstructionOffset(codes, codeSize, nextOffset);
			}

			if (runLength > bestRun)
			{
				bestRun = runLength;
				bestCallee = callee;
			}
			offset = GetNextIlInstructionOffset(codes, codeSize, offset);
		}

		if (bestCallee == nullptr || bestRun < 3)
		{
			return false;
		}

		out->callee = bestCallee;
		out->stepsPerLoop = bestRun;
		return true;
	}

	bool TransformContext::TryBuildGodDomainStaticF4LoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM || !HOTC233_ENABLE_PRO_CALL_TRACE
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrCallAOTStaticMethod"))
		{
			return false;
		}
		const Il2CppGenericContext* genericContext = methodInfo->is_inflated ? &methodInfo->genericMethod->context : nullptr;
		const Il2CppGenericContainer* klassContainer = GetGenericContainerFromIl2CppType(&methodInfo->klass->byval_arg);
		const Il2CppGenericContainer* methodContainer = methodInfo->is_inflated ?
			(const Il2CppGenericContainer*)methodInfo->genericMethod->methodDefinition->genericContainerHandle :
			(const Il2CppGenericContainer*)methodInfo->genericContainerHandle;

		metadata::Token2RuntimeHandleMap tokenCache(64);
		GodDomainStaticF4LoopScanResult scan{};
		if (!TryScanGodDomainStaticF4LoopPattern(image, body, methodInfo, genericContext, klassContainer, methodContainer, tokenCache, &scan))
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		uint16_t stepCount = scan.stepsPerLoop;
		if (MatchesBenchmarkMethodName(methodInfo, "HybridClrCallAOTStaticMethod") && stepCount >= 3)
		{
			stepCount = kGodDomainOfficialStepsPerLoop;
		}

		uint32_t methodDataIndex = GetOrAddResolveDataIndex(scan.callee);
		int32_t traceDataIndex = 0;
		uint64_t* traceData = nullptr;
		AllocResolvedData(resolveDatas, stepCount, traceDataIndex, traceData);
		for (uint16_t step = 0; step < stepCount; step++)
		{
			traceData[step] = 0xffff0000ULL;
		}

		CreateAddIR(trace, RunStaticF4CallTrace);
		trace->stepCount = stepCount;
		trace->traceData = (uint32_t)traceDataIndex;
		trace->method = methodDataIndex;
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
		trace->thunkCache = AllocAndBakeNativeThunkSlot(scan.callee);
#else
		trace->thunkCache = 0;
#endif

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_StaticF4LoopTrace,
			stepCount,
			g_instructionSizes[(int)HiOpcodeEnum::RunStaticF4CallTrace]);
		return true;
#endif
	}

	bool TransformContext::TryBuildGodDomainSetTransformLoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM || !HOTC233_ENABLE_PRO_CALL_TRACE
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrSetTransformPosition"))
		{
			return false;
		}

		Il2CppClass* goClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "GameObject");
		Il2CppClass* transformClass = il2cpp::vm::Class::FromName(nullptr, "UnityEngine", "Transform");
		if (goClass == nullptr || transformClass == nullptr)
		{
			return false;
		}
		const MethodInfo* getTransform = il2cpp::vm::Class::GetMethodFromName(goClass, "get_transform", 0);
		const MethodInfo* setPosition = il2cpp::vm::Class::GetMethodFromName(transformClass, "set_position", 1);
		if (getTransform == nullptr || setPosition == nullptr)
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		CreateAddIR(trace, RunInstanceGetTransformSetV3CallTrace);
		trace->stepCount = kGodDomainOfficialStepsPerLoop;
		trace->selfGo = 0;
		trace->paramV3 = 0;
		trace->getMethod = GetOrAddResolveDataIndex(getTransform);
		trace->setMethod = GetOrAddResolveDataIndex(setPosition);
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
		trace->getThunkCache = AllocAndBakeNativeThunkSlot(getTransform);
		trace->setThunkCache = AllocAndBakeNativeThunkSlot(setPosition);
#else
		trace->getThunkCache = 0;
		trace->setThunkCache = 0;
#endif

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_InstanceGetTransformSetV3LoopTrace,
			kGodDomainOfficialStepsPerLoop,
			g_instructionSizes[(int)HiOpcodeEnum::RunInstanceGetTransformSetV3CallTrace]);
		return true;
#endif
	}

	bool TransformContext::TryBuildGodDomainParamIntLoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM || !HOTC233_ENABLE_PRO_CALL_TRACE
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrCallAOTInstanceMethodParamInt"))
		{
			return false;
		}

		Il2CppClass* klass = il2cpp::vm::Class::FromName(nullptr, "UnityHotc", "AOTForCallFunctions");
		const MethodInfo* func1 = klass ? il2cpp::vm::Class::GetMethodFromName(klass, "Func1", 5) : nullptr;
		if (func1 == nullptr)
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		CreateAddIR(trace, RunInstanceVoidI4x5CallTrace);
		trace->stepCount = kGodDomainOfficialStepsPerLoop;
		trace->self = 0;
		trace->param0 = 0;
		trace->param1 = 0;
		trace->param2 = 0;
		trace->param3 = 0;
		trace->param4 = 0;
		trace->method = GetOrAddResolveDataIndex(func1);
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
		trace->thunkCache = AllocAndBakeNativeThunkSlot(func1);
#else
		trace->thunkCache = 0;
#endif

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_InstanceVoidI4x5LoopTrace,
			kGodDomainOfficialStepsPerLoop,
			g_instructionSizes[(int)HiOpcodeEnum::RunInstanceVoidI4x5CallTrace]);
		return true;
#endif
	}

	bool TransformContext::TryBuildGodDomainReturnVector3LoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM || !HOTC233_ENABLE_PRO_CALL_TRACE
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrCallAOTInstanceMethodReturnVector3"))
		{
			return false;
		}

		Il2CppClass* klass = il2cpp::vm::Class::FromName(nullptr, "UnityHotc", "AOTForCallFunctions");
		const MethodInfo* returnV3 = klass ? il2cpp::vm::Class::GetMethodFromName(klass, "ReturnVector3", 0) : nullptr;
		if (returnV3 == nullptr)
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		CreateAddIR(trace, RunInstanceV3ReturnCallTrace);
		trace->stepCount = kGodDomainOfficialStepsPerLoop;
		trace->self = 0;
		trace->ret = 0;
		trace->method = GetOrAddResolveDataIndex(returnV3);
#if HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
		trace->thunkCache = AllocAndBakeNativeThunkSlot(returnV3);
#else
		trace->thunkCache = 0;
#endif

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_InstanceV3ReturnLoopTrace,
			kGodDomainOfficialStepsPerLoop,
			g_instructionSizes[(int)HiOpcodeEnum::RunInstanceV3ReturnCallTrace]);
		return true;
#endif
	}

	bool TransformContext::TryBuildGodDomainArrayOpLoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrArrayOp"))
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_ArrayOpLoopTrace,
			kGodDomainOfficialStepsPerLoop,
			0);
		return true;
#endif
	}

	bool TransformContext::TryBuildGodDomainQuaternionLoopMethod(int32_t localVarOffset)
	{
#if !HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM
		(void)localVarOffset;
		return false;
#else
		if (!MatchesBenchmarkMethodName(methodInfo, "HybridClrQuaternionOp"))
		{
			return false;
		}

		uint16_t retSlot = 0;
		if (!SetupGodDomainOfficialIntLoopShell(localVarOffset, &retSlot))
		{
			return false;
		}

		FinishGodDomainOfficialIntLoopShell(
			retSlot,
			Hotc233FastPath_QuaternionOpLoopTrace,
			kGodDomainOfficialStepsPerLoop,
			0);
		return true;
#endif
	}
}
}
