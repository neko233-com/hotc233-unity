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

        [Header("Unity 2022+ Business Compatibility")]
        [Tooltip("启用完全泛型共享兼容路径。hotc233-unity 最低支持 Unity/Tuanjie 2022+。")]
        public bool enableFullGenericSharing = true;

        [Tooltip("启用元数据优化与完整性校验配置。WebGL/minigame 建议保持开启。")]
        public bool enableMetadataOptimization = true;

        [Tooltip("启用 RuntimeFast 标准解释优化。性能对比必须使用 RuntimeFast。")]
        public bool enableStandardInterpreterOptimization = true;

        [Tooltip("启用离线指令优化产物，用于生成合成 IR 与解释器快路径。")]
        public bool enableOfflineInstructionOptimization = true;

        [Tooltip("启用 Hotfix 动态热修复入口。运行时通过 HotUpdateBinaryLoader.ReplaceHotUpdateAssembly 应用。")]
        public bool enableHotfix = true;

        [Tooltip("启用热重载工作流入口。运行时通过 HotUpdateBinaryLoader.ReloadHotUpdateAssemblies 应用。")]
        public bool enableHotReloadWorkflow = true;

        [Tooltip("启用热更 payload 加密/完整性校验策略。具体解密由 Hotc233LoadPolicy 提供。")]
        public bool enableCodeProtection = true;

        [Tooltip("启用热更 payload 访问控制策略。具体白名单或签名校验由 Hotc233LoadPolicy 提供。")]
        public bool enableAccessControl = true;

        [Tooltip("代码保护使用的默认密钥标识。不要在仓库中提交真实生产密钥。")]
        public string payloadProtectionKeyId = "dev-hotc233-key";

        [Tooltip("热更 payload manifest 默认路径")]
        public string payloadManifestPath = "Assets/StreamingAssets/Hotc233Probe/Payload/payload-manifest.json";

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
