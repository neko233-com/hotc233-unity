using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditorInternal;
using UnityEngine;
using Hotc233.Editor.Settings;

namespace Hotc233.Editor
{
    /// <summary>
    /// Hotc233 编辑器侧路径与配置入口。
    /// </summary>
    public static class SettingsUtil
    {
        public static bool Enable
        {
            get => Hotc233Settings.Instance.enable;
            set
            {
                Hotc233Settings.Instance.enable = value;
                Hotc233Settings.Save();
            }
        }

        public static string PackageName { get; } = "com.neko233.hotc233-unity";

        public static string Hotc233DataPathInPackage => $"Assets/neko233/hotc233-unity/Data~";

        public static string TemplatePathInPackage => $"{Hotc233DataPathInPackage}/Templates";

        public static string ProjectDir { get; } = Directory.GetParent(Application.dataPath).ToString();

        public static string ScriptingAssembliesJsonFile { get; } = "ScriptingAssemblies.json";

        public static string HotUpdateDllsRootOutputDir => Hotc233Settings.Instance.hotUpdateDllCompileOutputRootDir;

        public static string AssembliesPostIl2CppStripDir => Hotc233Settings.Instance.strippedAOTDllOutputRootDir;

        public static string Hotc233DataDir => $"{ProjectDir}/Hotc233Data";

        public static string LocalUnityDataDir => GetLocalUnityDataDir(EditorUserBuildSettings.activeBuildTarget);

        public static string LocalIl2CppDir => GetLocalIl2CppDir(EditorUserBuildSettings.activeBuildTarget);

        public static string GeneratedCppDir => GetGeneratedCppDir(EditorUserBuildSettings.activeBuildTarget);

        public static string GetLocalUnityDataDir(BuildTarget target)
        {
            return $"{Hotc233DataDir}/LocalIl2CppData-{GetLocalIl2CppDataSuffix(target)}";
        }

        public static string GetLocalIl2CppDir(BuildTarget target)
        {
            return $"{GetLocalUnityDataDir(target)}/il2cpp";
        }

        public static string GetGeneratedCppDir(BuildTarget target)
        {
            return $"{GetLocalIl2CppDir(target)}/libil2cpp/hotc233/generated";
        }

        public static string GetLocalIl2CppDataSuffix(BuildTarget target)
        {
            switch (target)
            {
                case BuildTarget.StandaloneWindows:
                case BuildTarget.StandaloneWindows64:
                    return "StandaloneWindows";
                case BuildTarget.StandaloneOSX:
                    return "StandaloneOSX";
                case BuildTarget.StandaloneLinux64:
                    return "StandaloneLinux64";
                case BuildTarget.WebGL:
                    return "WebGL";
                case BuildTarget.Android:
                    return "Android";
                case BuildTarget.iOS:
                    return "iOS";
                case BuildTarget.tvOS:
                    return "tvOS";
                default:
                    return target.ToString();
            }
        }

        public static string Il2CppBuildCacheDir { get; } = $"{ProjectDir}/Library/Il2cppBuildCache";

        public static string GlobalgamemanagersBinFile { get; } = "globalgamemanagers";

        public static string Dataunity3dBinFile { get; } = "data.unity3d";

        public static string GetHotUpdateDllsOutputDirByTarget(BuildTarget target)
        {
            return $"{HotUpdateDllsRootOutputDir}/{target}";
        }

        public static string GetAssembliesPostIl2CppStripDir(BuildTarget target)
        {
            return $"{AssembliesPostIl2CppStripDir}/{target}";
        }

        class AssemblyDefinitionData
        {
            public string name;
        }

        public static List<string> HotUpdateAssemblyNamesExcludePreserved
        {
            get
            {
                var gs = Hotc233Settings.Instance;
                var hotfixAssNames = (gs.hotUpdateAssemblyDefinitions ?? Array.Empty<AssemblyDefinitionAsset>()).Select(ad => JsonUtility.FromJson<AssemblyDefinitionData>(ad.text));

                var hotfixAssembles = new List<string>();
                foreach (var assName in hotfixAssNames)
                {
                    hotfixAssembles.Add(assName.name);
                }
                hotfixAssembles.AddRange(gs.hotUpdateAssemblies ?? Array.Empty<string>());
                return hotfixAssembles.ToList();
            }
        }

        public static List<string> HotUpdateAssemblyFilesExcludePreserved => HotUpdateAssemblyNamesExcludePreserved.Select(dll => dll + ".dll").ToList();

        public static List<string> HotUpdateAssemblyNamesIncludePreserved
        {
            get
            {
                List<string> allAsses = HotUpdateAssemblyNamesExcludePreserved;
                string[] preserveAssemblyNames = Hotc233Settings.Instance.preserveHotUpdateAssemblies;
                if (preserveAssemblyNames != null && preserveAssemblyNames.Length > 0)
                {
                    foreach (var assemblyName in preserveAssemblyNames)
                    {
                        if (allAsses.Contains(assemblyName))
                        {
                            throw new Exception($"[HotUpdateAssemblyNamesIncludePreserved] assembly:'{assemblyName}' is duplicated");
                        }
                        allAsses.Add(assemblyName);
                    }
                }

                return allAsses;
            }
        }

        public static List<string> HotUpdateAssemblyFilesIncludePreserved => HotUpdateAssemblyNamesIncludePreserved.Select(ass => ass + ".dll").ToList();

        public static List<string> AOTAssemblyNames => Hotc233Settings.Instance.patchAOTAssemblies.ToList();

        public static Hotc233Settings Hotc233Settings => Hotc233Settings.Instance;
    }
}
