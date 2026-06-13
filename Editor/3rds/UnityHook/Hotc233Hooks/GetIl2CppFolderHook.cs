
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;
using UnityEditor;
using System.Runtime.CompilerServices;
using MonoHook;
using Hotc233.Editor.BuildProcessors;
using System.IO;

namespace Hotc233.Editor
{
#if UNITY_2022
    [InitializeOnLoad]
    /// <summary>
    /// 用途: 接管 UnityEditor 的 il2cpp 目录解析，强制切到 Hotc233 本地工作目录。
    /// 关键点: 当前包只面向 Unity 2022；这里不再保留 2023 及其它版本分支。
    /// 注意事项: 该 Hook 只负责路径改写，不负责安装流程本身。
    /// </summary>
    public class GetIl2CppFolderHook
    {
        private static MethodHook _hook;

        static GetIl2CppFolderHook()
        {
            if (_hook == null)
            {
                Type type = typeof(UnityEditor.EditorApplication).Assembly.GetType("UnityEditorInternal.IL2CPPUtils");
                MethodInfo miTarget = type.GetMethod("GetIl2CppFolder", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public, null,
                    new Type[] { typeof(bool).MakeByRefType() }, null);

                MethodInfo miReplacement = new StripAssembliesDel(OverrideMethod).Method;
                MethodInfo miProxy = new StripAssembliesDel(PlaceHolderMethod).Method;

                _hook = new MethodHook(miTarget, miReplacement, miProxy);
                _hook.Install();
            }
        }

        private delegate string StripAssembliesDel(out bool isDevelopmentLocation);

        private static string OverrideMethod(out bool isDevelopmentLocation)
        {
            //Debug.Log("[GetIl2CppFolderHook] OverrideMethod");
            string result = PlaceHolderMethod(out isDevelopmentLocation);
            isDevelopmentLocation = false;
            return result;
        }

        [MethodImpl(MethodImplOptions.NoOptimization)]
        private static string PlaceHolderMethod(out bool isDevelopmentLocation)
        {
            Debug.LogError("[GetIl2CppFolderHook] PlaceHolderMethod");
            isDevelopmentLocation = false;
            return null;
        }
    }
#endif
}
