using System.IO;
using UnityEditorInternal;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public enum Hotc233LogLanguage
    {
        // Auto keeps the package friendly for shared repositories: every editor
        // user gets logs in their system language unless the project explicitly
        // pins Chinese or English.
        Auto = 0,
        Chinese = 1,
        English = 2,
    }

    /// <summary>
    /// Hotc233 项目级编辑器配置。
    /// </summary>
    public class Hotc233Settings : ScriptableObject
    {
        [Tooltip("启用 Hotc233")]
        public bool enable = true;

        [Tooltip("使用编辑器安装目录内置的 il2cpp")]
        public bool useGlobalIl2cpp;

        [Tooltip("日志语言。Auto 会根据系统语言自动选择中文或英文；手动选择会保存到 ProjectSettings。")]
        public Hotc233LogLanguage logLanguage = Hotc233LogLanguage.Auto;

        [Tooltip("热更新程序集 asmdef 列表")]
        public AssemblyDefinitionAsset[] hotUpdateAssemblyDefinitions;

        [Tooltip("热更新程序集名称，不带 .dll 后缀")]
        public string[] hotUpdateAssemblies;

        [Tooltip("需要保留的热更新程序集名称，不带 .dll 后缀")]
        public string[] preserveHotUpdateAssemblies;

        [Tooltip("热更新程序集编译输出目录")]
        public string hotUpdateDllCompileOutputRootDir = "Hotc233Data/HotUpdateDlls";

        [Tooltip("外部热更新程序集搜索目录")]
        public string[] externalHotUpdateAssembliyDirs;

        [Tooltip("裁剪后 AOT 程序集输出目录")]
        public string strippedAOTDllOutputRootDir = "Hotc233Data/AssembliesPostIl2CppStrip";

        [Tooltip("补充元数据程序集名称，不带 .dll 后缀")]
        public string[] patchAOTAssemblies;

        [Tooltip("自动生成的 link.xml 输出路径")]
        public string outputLinkFile = "Hotc233Generate/link.xml";

        [Tooltip("自动生成的 AOTGenericReferences.cs 输出路径")]
        public string outputAOTGenericReferenceFile = "Hotc233Generate/AOTGenericReferences.cs";

        [Tooltip("扫描热更新泛型引用时的最大迭代次数")]
        public int maxGenericReferenceIteration = 10;

        [Tooltip("扫描 MethodBridge 泛型引用时的最大迭代次数")]
        public int maxMethodBridgeGenericIteration = 10;

        private static Hotc233Settings s_Instance;

        public static Hotc233Settings Instance
        {
            get
            {
                if (!s_Instance)
                {
                    LoadOrCreate();
                }
                return s_Instance;
            }
        }

        private static string GetFilePath()
        {
            return "ProjectSettings/Hotc233Settings.asset";
        }

        public static Hotc233Settings LoadOrCreate()
        {
            string filePath = GetFilePath();
            Object[] objs = InternalEditorUtility.LoadSerializedFileAndForget(filePath);
            if (objs.Length > 0 && objs[0] is Hotc233Settings settings)
            {
                s_Instance = settings;
                return s_Instance;
            }

            if (objs.Length > 0 && objs[0] != null)
            {
                Debug.LogWarning(
                    $"[hotc233] Ignoring incompatible settings asset at {filePath}: " +
                    $"{objs[0].GetType().FullName}. It will be recreated with the current package script GUID.");
            }

            s_Instance = s_Instance ?? CreateInstance<Hotc233Settings>();
            Save();
            return s_Instance;
        }

        public static void Save()
        {
            if (!s_Instance)
            {
                return;
            }

            // InternalEditorUtility writes a Unity-native serialized asset under
            // ProjectSettings, which is how this package persists editor-only
            // preferences without requiring a runtime asset in Assets/.
            string filePath = GetFilePath();
            string directoryName = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(directoryName))
            {
                Directory.CreateDirectory(directoryName);
            }
            var obj = new Object[1] { s_Instance };
            InternalEditorUtility.SaveToSerializedFileAndForget(obj, filePath, true);
        }
    }
}
