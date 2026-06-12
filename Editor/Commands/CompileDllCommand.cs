using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Build.Player;
using UnityEngine;
using Hotc233.Editor.Settings;

namespace Hotc233.Editor.Commands
{
    public class CompileDllCommand
    {
        public static void CompileDll(string buildDir, BuildTarget target, bool developmentBuild)
        {
            Debug.Log(Hotc233Localization.Format("compile.start", target, developmentBuild, buildDir));

            var group = BuildPipeline.GetBuildTargetGroup(target);

            ScriptCompilationSettings scriptCompilationSettings = new ScriptCompilationSettings();
            scriptCompilationSettings.group = group;
            scriptCompilationSettings.target = target;
            scriptCompilationSettings.options = developmentBuild ? ScriptCompilationOptions.DevelopmentBuild : ScriptCompilationOptions.None;
            Directory.CreateDirectory(buildDir);
            ScriptCompilationResult scriptCompilationResult = PlayerBuildInterface.CompilePlayerScripts(scriptCompilationSettings, buildDir);
#if UNITY_2022
            UnityEditor.EditorUtility.ClearProgressBar();
#endif
            Debug.Log(Hotc233Localization.Format("compile.finish", target, developmentBuild, buildDir));
        }

        public static void CompileDll(BuildTarget target)
        {
            CompileDll(target, EditorUserBuildSettings.development);
        }

        public static void CompileDll(BuildTarget target, bool developmentBuild)
        {
            CompileDll(SettingsUtil.GetHotUpdateDllsOutputDirByTarget(target), target, developmentBuild);
        }

        [MenuItem("hotc233/CompileDll/ActiveBuildTarget", priority = 100)]
        public static void CompileDllActiveBuildTarget()
        {
            CompileDll(EditorUserBuildSettings.activeBuildTarget, EditorUserBuildSettings.development);
        }

        [MenuItem("hotc233/CompileDll/ActiveBuildTarget_Release", priority = 102)]
        public static void CompileDllActiveBuildTargetRelease()
        {
            CompileDll(EditorUserBuildSettings.activeBuildTarget, false);
        }

        [MenuItem("hotc233/CompileDll/ActiveBuildTarget_Development", priority = 104)]
        public static void CompileDllActiveBuildTargetDevelopment()
        {
            CompileDll(EditorUserBuildSettings.activeBuildTarget, true);
        }

        [MenuItem("hotc233/CompileDll/Win32", priority = 200)]
        public static void CompileDllWin32()
        {
            CompileDll(BuildTarget.StandaloneWindows);
        }

        [MenuItem("hotc233/CompileDll/Win64", priority = 201)]
        public static void CompileDllWin64()
        {
            CompileDll(BuildTarget.StandaloneWindows64);
        }

        [MenuItem("hotc233/CompileDll/MacOS", priority = 202)]
        public static void CompileDllMacOS()
        {
            CompileDll(BuildTarget.StandaloneOSX);
        }

        [MenuItem("hotc233/CompileDll/Linux", priority = 203)]
        public static void CompileDllLinux()
        {
            CompileDll(BuildTarget.StandaloneLinux64);
        }

        [MenuItem("hotc233/CompileDll/Android", priority = 210)]
        public static void CompileDllAndroid()
        {
            CompileDll(BuildTarget.Android);
        }

        [MenuItem("hotc233/CompileDll/IOS", priority = 220)]
        public static void CompileDllIOS()
        {
            CompileDll(BuildTarget.iOS);
        }

        [MenuItem("hotc233/CompileDll/WebGL", priority = 230)]
        public static void CompileDllWebGL()
        {
            CompileDll(BuildTarget.WebGL);
        }
    }
}
