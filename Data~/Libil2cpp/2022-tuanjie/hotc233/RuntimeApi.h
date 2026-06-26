#pragma once

#include <stdint.h>
#include "CommonDef.h"

namespace hotc233
{
	class RuntimeApi
	{
	public:
		static void RegisterInternalCalls();

		static int32_t LoadMetadataForAOTAssembly(Il2CppArray* dllData, int32_t mode);

		static int32_t GetRuntimeOption(int32_t optionId);
		static void SetRuntimeOption(int32_t optionId, int32_t value);

		static int32_t PreJitClass(Il2CppReflectionType* type);
		static int32_t PreJitMethod(Il2CppReflectionMethod* method);
		static Il2CppString* GetMethodOpcodeProfile(Il2CppReflectionMethod* method, int32_t maxRows);
		static void ResetOpcodeProfiler();
		static void SetOpcodeProfilerEnabled(int32_t enabled);
		static Il2CppString* GetOpcodeProfilerSnapshot(int32_t maxRows);
		static Il2CppString* GetInterpreterStackTraceJson(int32_t maxFrames);
	};
}
