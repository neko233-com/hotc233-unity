using Hotc233.Editor.Settings;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using static UnityEngine.GraphicsBuffer;

namespace Hotc233.Editor.BuildProcessors
{
    internal class CheckSettings : IPreprocessBuildWithReport
    {
        public int callbackOrder => 0;

        public static bool DisableMethodBridgeDevelopmentFlagChecking { get; set; }

        public void OnPreprocessBuild(BuildReport report)
        {
            Hotc233Settings globalSettings = SettingsUtil.Hotc233Settings;
            BuildTarget target = report.summary.platform;
            string targetLocalIl2CppDir = SettingsUtil.GetLocalIl2CppDir(target);
            if (!globalSettings.enable || globalSettings.useGlobalIl2cpp)
            {
                string oldIl2cppPath = Environment.GetEnvironmentVariable("UNITY_IL2CPP_PATH");
                if (!string.IsNullOrEmpty(oldIl2cppPath))
                {
                    Environment.SetEnvironmentVariable("UNITY_IL2CPP_PATH", "");
                    Debug.Log($"[CheckSettings] clean process environment variable: UNITY_IL2CPP_PATH, old vlaue:'{oldIl2cppPath}'");
                }
            }
            else
            {
                string curIl2cppPath = Environment.GetEnvironmentVariable("UNITY_IL2CPP_PATH");
                if (curIl2cppPath != targetLocalIl2CppDir)
                {
                    Environment.SetEnvironmentVariable("UNITY_IL2CPP_PATH", targetLocalIl2CppDir);
                    Debug.Log($"[CheckSettings] UNITY_IL2CPP_PATH old value:'{curIl2cppPath}'， new value:'{targetLocalIl2CppDir}'");
                }
            }
            if (!globalSettings.enable)
            {
                return;
            }
            BuildTargetGroup buildTargetGroup = BuildPipeline.GetBuildTargetGroup(EditorUserBuildSettings.activeBuildTarget);
#if UNITY_6000_0_OR_NEWER
            NamedBuildTarget namedBuildTarget = NamedBuildTarget.FromBuildTargetGroup(buildTargetGroup);
            ScriptingImplementation curScriptingImplementation = PlayerSettings.GetScriptingBackend(namedBuildTarget);
#else
            ScriptingImplementation curScriptingImplementation = PlayerSettings.GetScriptingBackend(buildTargetGroup);
#endif
            ScriptingImplementation targetScriptingImplementation = ScriptingImplementation.IL2CPP;
            if (curScriptingImplementation != targetScriptingImplementation)
            {
                Debug.LogError($"[CheckSettings] current ScriptingBackend:{curScriptingImplementation}，have been switched to:{targetScriptingImplementation} automatically");
#if UNITY_6000_0_OR_NEWER
                PlayerSettings.SetScriptingBackend(namedBuildTarget, targetScriptingImplementation);
#else
                PlayerSettings.SetScriptingBackend(buildTargetGroup, targetScriptingImplementation);
#endif
            }

            var installer = new Installer.InstallerController(target);
            installer.EnsureBuiltinRuntimeReady();

            if (!installer.HasInstalledHotc233())
            {
                throw new BuildFailedException("Hotc233 builtin runtime is not ready. Use 'hotc233/Builtin Runtime...' or run Generate/All.");
            }

            if (!installer.HasGeneratedIl2CppDefinitions())
            {
                throw new BuildFailedException("Hotc233 generated IL2CPP definitions are missing or stale. Run 'hotc233/Generate/All' before building.");
            }

            Hotc233Settings gs = SettingsUtil.Hotc233Settings;
            if (((gs.hotUpdateAssemblies?.Length + gs.hotUpdateAssemblyDefinitions?.Length) ?? 0) == 0)
            {
                Debug.LogWarning("[CheckSettings] No hot update modules configured in Hotc233Settings");
            }

            if (!DisableMethodBridgeDevelopmentFlagChecking)
            {
                string methodBridgeFile = $"{SettingsUtil.GetGeneratedCppDir(target)}/MethodBridge.cpp";
                if (!File.Exists(methodBridgeFile))
                {
                    throw new BuildFailedException("[CheckSettings] MethodBridge.cpp missing. Please run 'hotc233/Generate/All' before building.");
                }

                var match = Regex.Match(File.ReadAllText(methodBridgeFile), @"// DEVELOPMENT=(\d)");
                if (match.Success)
                {
                    int developmentFlagInMethodBridge = int.Parse(match.Groups[1].Value);
                    int developmentFlagInEditorSettings = EditorUserBuildSettings.development ? 1 : 0;
                    if (developmentFlagInMethodBridge != developmentFlagInEditorSettings)
                    {
                        throw new BuildFailedException($"[CheckSettings] MethodBridge.cpp DEVELOPMENT flag:{developmentFlagInMethodBridge} is inconsistent with EditorUserBuildSettings.development:{developmentFlagInEditorSettings}. Please run 'hotc233/Generate/All' before building.");
                    }
                }
                else
                {
                    throw new BuildFailedException("[CheckSettings] MethodBridge.cpp DEVELOPMENT flag not found. Please run 'hotc233/Generate/All' before building.");
                }
            }
        }
    }
}
