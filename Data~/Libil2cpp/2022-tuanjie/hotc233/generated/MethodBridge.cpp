#include <codegen/il2cpp-codegen-metadata.h>
#if HOTC233_UNITY_2023_OR_NEW
#include <codegen/il2cpp-codegen.h>
#elif HOTC233_UNITY_2022
#include <codegen/il2cpp-codegen-il2cpp.h>
#elif HOTC233_UNITY_2020 || HOTC233_UNITY_2021
#include <codegen/il2cpp-codegen-common-big.h>
#else
#include <codegen/il2cpp-codegen-common.h>
#endif

#include "vm/ClassInlines.h"
#include "vm/Object.h"
#include "vm/Class.h"
#include "vm/ScopedThreadAttacher.h"

#include "../metadata/MetadataUtil.h"


#include "../interpreter/InterpreterModule.h"
#include "../interpreter/MethodBridge.h"
#include "../interpreter/Interpreter.h"
#include "../interpreter/MemoryUtil.h"
#include "../interpreter/InstrinctDef.h"

using namespace hotc233::interpreter;
using namespace hotc233::metadata;

//!!!{{CODE

const FullName2Signature hotc233::interpreter::g_fullName2SignatureStub[] = {
	{ nullptr, nullptr},
};


const Managed2NativeMethodInfo hotc233::interpreter::g_managed2nativeStub[] =
{

	{nullptr, nullptr},
};


const Native2ManagedMethodInfo hotc233::interpreter::g_native2managedStub[] =
{
	{nullptr, nullptr},
};

const NativeAdjustThunkMethodInfo hotc233::interpreter::g_adjustThunkStub[] =
{
	{nullptr, nullptr},
};

const ReversePInvokeMethodData hotc233::interpreter::g_reversePInvokeMethodStub[]
{
    {nullptr, nullptr},
};

const Managed2NativeFunctionPointerCallData hotc233::interpreter::g_managed2NativeFunctionPointerCallStub[]
{
    {nullptr, nullptr},
};
//!!!}}CODE
