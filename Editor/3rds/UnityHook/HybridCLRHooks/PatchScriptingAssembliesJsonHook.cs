
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
#if UNITY_2022 && (UNITY_WEBGL || UNITY_WEIXINMINIGAME)
    [InitializeOnLoad]
    /// <summary>
    /// 用途: 在 WebGL / 微信小游戏打包缓存生成后，补丁 ScriptingAssemblies 列表。
    /// 关键点: 仅覆盖当前项目实际使用的 Unity 2022 平台链路。
    /// 注意事项: Hook 的代理函数必须保留最小方法体长度，不能删空。
    /// </summary>
    public class PatchScriptingAssembliesJsonHook
    {
        private static MethodHook _hook;

        static PatchScriptingAssembliesJsonHook()
        {
            if (_hook == null)
            {
                Type type = typeof(UnityEditor.EditorApplication);
                MethodInfo miTarget = type.GetMethod("BuildMainWindowTitle", BindingFlags.Static | BindingFlags.NonPublic);

                MethodInfo miReplacement = new Func<string>(BuildMainWindowTitle).Method;
                MethodInfo miProxy = new Func<string>(BuildMainWindowTitleProxy).Method;

                _hook = new MethodHook(miTarget, miReplacement, miProxy);
                _hook.Install();
            }
        }

        private static string BuildMainWindowTitle()
        {
            string cacheDir = $"{Application.dataPath}/../Library/PlayerDataCache";
            if (Directory.Exists(cacheDir))
            {
                foreach (string tempJsonPath in Directory.GetDirectories(cacheDir, "*", SearchOption.TopDirectoryOnly))
                {
                    string dirName = Path.GetFileName(tempJsonPath);
#if UNITY_WEIXINMINIGAME
                    if (!dirName.Contains("WeixinMiniGame"))
                    {
                        continue;
                    }
#else
                    if (!dirName.Contains("WebGL"))
                    {
                        continue;
                    }
#endif

                    PatchScriptingAssemblyList patcher = new PatchScriptingAssemblyList();
                    patcher.PathScriptingAssembilesFile(tempJsonPath);
                }
            }

            string newTitle = BuildMainWindowTitleProxy();
            return newTitle;
        }

        [MethodImpl(MethodImplOptions.NoOptimization)]
        private static string BuildMainWindowTitleProxy()
        {
            // 为满足MonoHook要求的最小代码长度而特地加入的无用填充代码，
            UnityEngine.Debug.Log(12345.ToString());
            return string.Empty;
        }
    }
#endif
}
