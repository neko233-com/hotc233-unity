using System;
using System.Reflection;
using UnityEngine.Scripting;

namespace HybridCLR
{
    public enum HomologousImageMode
    {
        Consistent = 0,
        SuperSet = 1,
    }

    public enum LoadImageErrorCode
    {
        OK = 0,
        BAD_IMAGE = 1,
        NOT_IMPLEMENT = 2,
        AOT_ASSEMBLY_NOT_FIND = 3,
        HOMOLOGOUS_ONLY_SUPPORT_AOT_ASSEMBLY = 4,
        HOMOLOGOUS_ASSEMBLY_HAS_LOADED = 5,
        INVALID_HOMOLOGOUS_MODE = 6,
        PDB_BAD_FILE = 7,
        UNKNOWN_IMAGE_FORMAT = 8,
        UNSUPPORT_FORMAT_VERSION = 9,
        UNMATCH_FORMAT_VARIANT = 10,
    }

    public enum RuntimeOptionId
    {
        InterpreterThreadObjectStackSize = 1,
        InterpreterThreadFrameStackSize = 2,
        ThreadExceptionFlowSize = 3,
        MaxMethodBodyCacheSize = 4,
        MaxMethodInlineDepth = 5,
        MaxInlineableMethodBodySize = 6,
    }

    [Preserve]
    public static class Hotc233_HybridclrAdapt
    {
        public static LoadImageErrorCode LoadMetadataForAOTAssembly(byte[] dllBytes, HomologousImageMode mode)
        {
            var result = Hotc233.RuntimeApi.LoadMetadataForAOTAssembly(
                dllBytes,
                (Hotc233.HomologousImageMode)(int)mode);
            return (LoadImageErrorCode)(int)result;
        }

        public static bool PreJitMethod(MethodInfo method)
        {
            return Hotc233.RuntimeApi.PreJitMethod(method);
        }

        public static bool PreJitClass(Type type)
        {
            return Hotc233.RuntimeApi.PreJitClass(type);
        }

        public static int GetInterpreterThreadObjectStackSize()
        {
            return Hotc233.RuntimeApi.GetInterpreterThreadObjectStackSize();
        }

        public static void SetInterpreterThreadObjectStackSize(int size)
        {
            Hotc233.RuntimeApi.SetInterpreterThreadObjectStackSize(size);
        }

        public static int GetInterpreterThreadFrameStackSize()
        {
            return Hotc233.RuntimeApi.GetInterpreterThreadFrameStackSize();
        }

        public static void SetInterpreterThreadFrameStackSize(int size)
        {
            Hotc233.RuntimeApi.SetInterpreterThreadFrameStackSize(size);
        }

        public static void SetRuntimeOption(RuntimeOptionId optionId, int value)
        {
            Hotc233.RuntimeApi.SetRuntimeOption((Hotc233.RuntimeOptionId)(int)optionId, value);
        }

        public static int GetRuntimeOption(RuntimeOptionId optionId)
        {
            return Hotc233.RuntimeApi.GetRuntimeOption((Hotc233.RuntimeOptionId)(int)optionId);
        }
    }

    [Preserve]
    public static class RuntimeApi
    {
        public static LoadImageErrorCode LoadMetadataForAOTAssembly(byte[] dllBytes, HomologousImageMode mode)
        {
            return Hotc233_HybridclrAdapt.LoadMetadataForAOTAssembly(dllBytes, mode);
        }

        public static bool PreJitMethod(MethodInfo method)
        {
            return Hotc233_HybridclrAdapt.PreJitMethod(method);
        }

        public static bool PreJitClass(Type type)
        {
            return Hotc233_HybridclrAdapt.PreJitClass(type);
        }

        public static int GetInterpreterThreadObjectStackSize()
        {
            return Hotc233_HybridclrAdapt.GetInterpreterThreadObjectStackSize();
        }

        public static void SetInterpreterThreadObjectStackSize(int size)
        {
            Hotc233_HybridclrAdapt.SetInterpreterThreadObjectStackSize(size);
        }

        public static int GetInterpreterThreadFrameStackSize()
        {
            return Hotc233_HybridclrAdapt.GetInterpreterThreadFrameStackSize();
        }

        public static void SetInterpreterThreadFrameStackSize(int size)
        {
            Hotc233_HybridclrAdapt.SetInterpreterThreadFrameStackSize(size);
        }

        public static void SetRuntimeOption(RuntimeOptionId optionId, int value)
        {
            Hotc233_HybridclrAdapt.SetRuntimeOption(optionId, value);
        }

        public static int GetRuntimeOption(RuntimeOptionId optionId)
        {
            return Hotc233_HybridclrAdapt.GetRuntimeOption(optionId);
        }
    }
}
