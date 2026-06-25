using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public static class RuntimeApi
    {
#if UNITY_EDITOR
        public static unsafe LoadImageErrorCode LoadMetadataForAOTAssembly(byte[] dllBytes, HomologousImageMode mode)
        {
            return LoadImageErrorCode.OK;
        }
#else
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern LoadImageErrorCode LoadMetadataForAOTAssembly(byte[] dllBytes, HomologousImageMode mode);
#endif

#if UNITY_EDITOR
        public static bool PreJitMethod(MethodInfo method)
        {
            return false;
        }
#else
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool PreJitMethod(MethodInfo method);
#endif

#if UNITY_EDITOR
        public static bool PreJitClass(Type type)
        {
            return false;
        }
#else
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool PreJitClass(Type type);
#endif

        public static int GetInterpreterThreadObjectStackSize()
        {
            return GetRuntimeOption(RuntimeOptionId.InterpreterThreadObjectStackSize);
        }

        public static void SetInterpreterThreadObjectStackSize(int size)
        {
            SetRuntimeOption(RuntimeOptionId.InterpreterThreadObjectStackSize, size);
        }

        public static int GetInterpreterThreadFrameStackSize()
        {
            return GetRuntimeOption(RuntimeOptionId.InterpreterThreadFrameStackSize);
        }

        public static void SetInterpreterThreadFrameStackSize(int size)
        {
            SetRuntimeOption(RuntimeOptionId.InterpreterThreadFrameStackSize, size);
        }

#if UNITY_EDITOR
        private static readonly Dictionary<RuntimeOptionId, int> s_runtimeOptions = new Dictionary<RuntimeOptionId, int>();

        public static void SetRuntimeOption(RuntimeOptionId optionId, int value)
        {
            s_runtimeOptions[optionId] = value;
        }
#else
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void SetRuntimeOption(RuntimeOptionId optionId, int value);
#endif

#if UNITY_EDITOR
        public static int GetRuntimeOption(RuntimeOptionId optionId)
        {
            if (s_runtimeOptions.TryGetValue(optionId, out var value))
            {
                return value;
            }
            return 0;
        }
#else
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern int GetRuntimeOption(RuntimeOptionId optionId);
#endif
    }
}

namespace HybridCLR
{
    using System;
    using System.Reflection;

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

    [UnityEngine.Scripting.Preserve]
    public static class RuntimeApi
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
}
