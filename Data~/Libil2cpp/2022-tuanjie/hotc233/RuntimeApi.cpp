#include "RuntimeApi.h"

#include "codegen/il2cpp-codegen.h"
#include "vm/InternalCalls.h"
#include "vm/Array.h"
#include "vm/Exception.h"
#include "vm/Class.h"
#include "vm/String.h"

#include "metadata/MetadataModule.h"
#include "metadata/MetadataUtil.h"
#include "interpreter/Instruction.h"
#include "interpreter/InterpreterModule.h"
#include "RuntimeConfig.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace hotc233
{
	namespace
	{
		constexpr uint32_t kOpcodeProfileCapacity = 2048;

		struct OpcodeProfileRow
		{
			uint32_t opcode;
			uint64_t count;
			uint16_t instructionSize;
		};

		struct OpcodePairProfileRow
		{
			uint32_t previousOpcode;
			uint32_t opcode;
			uint64_t count;
		};

		void AppendJsonEscaped(std::ostringstream& os, const char* value)
		{
			if (!value)
			{
				return;
			}
			for (const char* ch = value; *ch; ++ch)
			{
				switch (*ch)
				{
				case '\\': os << "\\\\"; break;
				case '"': os << "\\\""; break;
				case '\n': os << "\\n"; break;
				case '\r': os << "\\r"; break;
				case '\t': os << "\\t"; break;
				default:
					if ((uint8_t)*ch < 0x20)
					{
						os << "\\u00";
						const char* hex = "0123456789abcdef";
						os << hex[((uint8_t)*ch) >> 4] << hex[((uint8_t)*ch) & 0xf];
					}
					else
					{
						os << *ch;
					}
					break;
				}
			}
		}

		Il2CppString* NewJsonString(const std::string& value)
		{
			return il2cpp::vm::String::NewLen(value.c_str(), (uint32_t)value.size());
		}
	}

	void RuntimeApi::RegisterInternalCalls()
	{
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::LoadMetadataForAOTAssembly(System.Byte[],Hotc233.HomologousImageMode)", (Il2CppMethodPointer)LoadMetadataForAOTAssembly);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::GetRuntimeOption(Hotc233.RuntimeOptionId)", (Il2CppMethodPointer)GetRuntimeOption);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::SetRuntimeOption(Hotc233.RuntimeOptionId,System.Int32)", (Il2CppMethodPointer)SetRuntimeOption);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::PreJitClass(System.Type)", (Il2CppMethodPointer)PreJitClass);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::PreJitMethod(System.Reflection.MethodInfo)", (Il2CppMethodPointer)PreJitMethod);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::GetMethodOpcodeProfile(System.Reflection.MethodInfo,System.Int32)", (Il2CppMethodPointer)GetMethodOpcodeProfile);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::ResetOpcodeProfiler()", (Il2CppMethodPointer)ResetOpcodeProfiler);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::SetOpcodeProfilerEnabled(System.Int32)", (Il2CppMethodPointer)SetOpcodeProfilerEnabled);
		il2cpp::vm::InternalCalls::Add("Hotc233.RuntimeApi::GetOpcodeProfilerSnapshot(System.Int32)", (Il2CppMethodPointer)GetOpcodeProfilerSnapshot);
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

	Il2CppString* RuntimeApi::GetMethodOpcodeProfile(Il2CppReflectionMethod* method, int32_t maxRows)
	{
		if (!method || !method->method)
		{
			return NewJsonString("{\"success\":false,\"reason\":\"null-method\"}");
		}

		const MethodInfo* methodInfo = method->method;
		if (!methodInfo->isInterpterImpl)
		{
			return NewJsonString("{\"success\":false,\"reason\":\"not-interpreter-method\"}");
		}

		interpreter::InterpMethodInfo* imi = methodInfo->interpData
			? (interpreter::InterpMethodInfo*)methodInfo->interpData
			: interpreter::InterpreterModule::GetInterpMethodInfo(methodInfo);
		if (!imi || !imi->codes)
		{
			return NewJsonString("{\"success\":false,\"reason\":\"missing-interpreter-body\"}");
		}

		uint64_t counts[kOpcodeProfileCapacity] = {};
		uint64_t instructionCount = 0;
		uint32_t offset = 0;
		bool malformed = false;
		while (offset < imi->codeLength)
		{
			uint32_t opcode = (uint32_t)(*(interpreter::HiOpcodeEnum*)(imi->codes + offset));
			if (opcode >= kOpcodeProfileCapacity)
			{
				malformed = true;
				break;
			}

			uint16_t size = interpreter::g_instructionSizes[opcode];
			if (size == 0 || offset + size > imi->codeLength)
			{
				malformed = true;
				break;
			}

			counts[opcode]++;
			instructionCount++;
			offset += size;
		}

		std::vector<OpcodeProfileRow> rows;
		for (uint32_t opcode = 0; opcode < kOpcodeProfileCapacity; ++opcode)
		{
			if (counts[opcode] == 0)
			{
				continue;
			}
			rows.push_back({ opcode, counts[opcode], interpreter::g_instructionSizes[opcode] });
		}
		std::sort(rows.begin(), rows.end(), [](const OpcodeProfileRow& a, const OpcodeProfileRow& b)
		{
			if (a.count != b.count)
			{
				return a.count > b.count;
			}
			return a.opcode < b.opcode;
		});

		if (maxRows <= 0)
		{
			maxRows = 64;
		}
		maxRows = std::min(maxRows, 256);

		std::ostringstream os;
		os << "{\"success\":true";
		os << ",\"method\":\"";
		if (methodInfo->klass)
		{
			AppendJsonEscaped(os, methodInfo->klass->namespaze);
			if (methodInfo->klass->namespaze && methodInfo->klass->namespaze[0])
			{
				os << ".";
			}
			AppendJsonEscaped(os, methodInfo->klass->name);
			os << "::";
		}
		AppendJsonEscaped(os, methodInfo->name);
		os << "\"";
		os << ",\"codeLength\":" << imi->codeLength;
		os << ",\"instructionCount\":" << instructionCount;
		os << ",\"malformed\":" << (malformed ? "true" : "false");
		os << ",\"rows\":[";
		int32_t rowCount = std::min((int32_t)rows.size(), maxRows);
		for (int32_t i = 0; i < rowCount; ++i)
		{
			const OpcodeProfileRow& row = rows[i];
			if (i > 0)
			{
				os << ",";
			}
			os << "{\"opcode\":" << row.opcode
				<< ",\"count\":" << row.count
				<< ",\"instructionSize\":" << row.instructionSize
				<< "}";
		}
		os << "]}";
		return NewJsonString(os.str());
	}

	void RuntimeApi::ResetOpcodeProfiler()
	{
		interpreter::ResetOpcodeProfiler();
	}

	void RuntimeApi::SetOpcodeProfilerEnabled(int32_t enabled)
	{
		interpreter::SetOpcodeProfilerEnabled(enabled != 0);
	}

	Il2CppString* RuntimeApi::GetOpcodeProfilerSnapshot(int32_t maxRows)
	{
		std::vector<OpcodeProfileRow> rows;
		std::vector<OpcodePairProfileRow> pairs;
		for (uint32_t opcode = 0; opcode < interpreter::kDynamicOpcodeProfileCapacity; ++opcode)
		{
			uint64_t count = interpreter::g_opcodeProfilerCounts[opcode];
			if (count == 0)
			{
				continue;
			}
			rows.push_back({ opcode, count, interpreter::g_instructionSizes[opcode] });
		}
		for (uint32_t slot = 0; slot < interpreter::kDynamicOpcodePairProfileCapacity; ++slot)
		{
			uint64_t count = interpreter::g_opcodeProfilerPairCounts[slot];
			if (count == 0)
			{
				continue;
			}
			uint32_t key = interpreter::g_opcodeProfilerPairKeys[slot];
			uint32_t previousOpcode = key >> 16;
			uint32_t opcode = key & 0xffffu;
			pairs.push_back({ previousOpcode, opcode, count });
		}
		std::sort(rows.begin(), rows.end(), [](const OpcodeProfileRow& a, const OpcodeProfileRow& b)
		{
			if (a.count != b.count)
			{
				return a.count > b.count;
			}
			return a.opcode < b.opcode;
		});
		std::sort(pairs.begin(), pairs.end(), [](const OpcodePairProfileRow& a, const OpcodePairProfileRow& b)
		{
			if (a.count != b.count)
			{
				return a.count > b.count;
			}
			if (a.previousOpcode != b.previousOpcode)
			{
				return a.previousOpcode < b.previousOpcode;
			}
			return a.opcode < b.opcode;
		});

		if (maxRows <= 0)
		{
			maxRows = 64;
		}
		maxRows = std::min(maxRows, 256);

		std::ostringstream os;
		os << "{\"success\":true";
		os << ",\"enabled\":" << (interpreter::g_opcodeProfilerEnabled ? "true" : "false");
		os << ",\"total\":" << interpreter::g_opcodeProfilerTotal;
		os << ",\"pairTotal\":" << interpreter::g_opcodeProfilerPairTotal;
		os << ",\"rows\":[";
		int32_t rowCount = std::min((int32_t)rows.size(), maxRows);
		for (int32_t i = 0; i < rowCount; ++i)
		{
			const OpcodeProfileRow& row = rows[i];
			if (i > 0)
			{
				os << ",";
			}
			os << "{\"opcode\":" << row.opcode
				<< ",\"count\":" << row.count
				<< ",\"instructionSize\":" << row.instructionSize
				<< "}";
		}
		os << "],\"pairs\":[";
		int32_t pairRowCount = std::min((int32_t)pairs.size(), maxRows);
		for (int32_t i = 0; i < pairRowCount; ++i)
		{
			const OpcodePairProfileRow& pair = pairs[i];
			if (i > 0)
			{
				os << ",";
			}
			os << "{\"previousOpcode\":" << pair.previousOpcode
				<< ",\"opcode\":" << pair.opcode
				<< ",\"count\":" << pair.count
				<< "}";
		}
		os << "]}";
		return NewJsonString(os.str());
	}
}
