using System.IO;
using UnityEditorInternal;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public class Hotc233Settings : ScriptableObject
    {
        [Tooltip("enable Hotc233")]
        public bool enable = true;

        [Tooltip("use il2cpp in unity editor installation location")]
        public bool useGlobalIl2cpp;

        [Tooltip("hot update assembly definitions(asd)")]
        public AssemblyDefinitionAsset[] hotUpdateAssemblyDefinitions;

        [Tooltip("hot update assembly names(without .dll suffix)")]
        public string[] hotUpdateAssemblies;

        [Tooltip("preserved hot update assembly names(without .dll suffix)")]
        public string[] preserveHotUpdateAssemblies;

        [Tooltip("output directory of compiling hot update assemblies")]
        public string hotUpdateDllCompileOutputRootDir = "HybridCLRData/HotUpdateDlls";

        [Tooltip("searching paths of external hot update assemblies")]
        public string[] externalHotUpdateAssembliyDirs;

        [Tooltip("output directory of stripped AOT assemblies")]
        public string strippedAOTDllOutputRootDir = "HybridCLRData/AssembliesPostIl2CppStrip";

        [Tooltip("supplementary metadata assembly names(without .dll suffix)")]
        public string[] patchAOTAssemblies;

        [Tooltip("output file of automatic generated link.xml by scanning hot update assemblies")]
        public string outputLinkFile = "HybridCLRGenerate/link.xml";

        [Tooltip("output file of automatic generated AOTGenericReferences.cs")]
        public string outputAOTGenericReferenceFile = "HybridCLRGenerate/AOTGenericReferences.cs";

        [Tooltip("max iteration count of searching generic methods in hot update assemblies")]
        public int maxGenericReferenceIteration = 10;

        [Tooltip("max iteration count of searching method bridge generic methods in AOT assemblies")]
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
            s_Instance = objs.Length > 0 ? (Hotc233Settings)objs[0] : (s_Instance ?? CreateInstance<Hotc233Settings>());
            return s_Instance;
        }

        public static void Save()
        {
            if (!s_Instance)
            {
                return;
            }

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
