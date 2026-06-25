using Hotc233.Editor.Commands;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public static class HybridClrMenuAliases
    {
        [MenuItem("HybridCLR/Settings...", priority = 61)]
        public static void OpenSettingsAlias()
        {
            SettingsService.OpenProjectSettings("Project/Hotc233 Settings");
        }

        [MenuItem("HybridCLR/About", priority = 0)]
        public static void OpenAboutAlias()
        {
            Application.OpenURL("https://github.com/neko233-com/hotc233-unity");
        }

        [MenuItem("HybridCLR/CompileDll/ActiveBuildTarget", priority = 100)]
        public static void CompileDllActiveBuildTargetAlias()
        {
            CompileDllCommand.CompileDllActiveBuildTarget();
        }

        [MenuItem("HybridCLR/CompileDll/ActiveBuildTarget_Release", priority = 102)]
        public static void CompileDllActiveBuildTargetReleaseAlias()
        {
            CompileDllCommand.CompileDllActiveBuildTargetRelease();
        }

        [MenuItem("HybridCLR/CompileDll/ActiveBuildTarget_Development", priority = 104)]
        public static void CompileDllActiveBuildTargetDevelopmentAlias()
        {
            CompileDllCommand.CompileDllActiveBuildTargetDevelopment();
        }

        [MenuItem("HybridCLR/CompileDll/Win32", priority = 200)]
        public static void CompileDllWin32Alias()
        {
            CompileDllCommand.CompileDllWin32();
        }

        [MenuItem("HybridCLR/CompileDll/Win64", priority = 201)]
        public static void CompileDllWin64Alias()
        {
            CompileDllCommand.CompileDllWin64();
        }

        [MenuItem("HybridCLR/CompileDll/MacOS", priority = 202)]
        public static void CompileDllMacOSAlias()
        {
            CompileDllCommand.CompileDllMacOS();
        }

        [MenuItem("HybridCLR/CompileDll/Linux", priority = 203)]
        public static void CompileDllLinuxAlias()
        {
            CompileDllCommand.CompileDllLinux();
        }

        [MenuItem("HybridCLR/CompileDll/Android", priority = 210)]
        public static void CompileDllAndroidAlias()
        {
            CompileDllCommand.CompileDllAndroid();
        }

        [MenuItem("HybridCLR/CompileDll/IOS", priority = 220)]
        public static void CompileDllIOSAlias()
        {
            CompileDllCommand.CompileDllIOS();
        }

        [MenuItem("HybridCLR/CompileDll/WebGL", priority = 230)]
        public static void CompileDllWebGLAlias()
        {
            CompileDllCommand.CompileDllWebGL();
        }

        [MenuItem("HybridCLR/Generate/LinkXml", priority = 100)]
        public static void GenerateLinkXmlAlias()
        {
            LinkGeneratorCommand.GenerateLinkXml();
        }

        [MenuItem("HybridCLR/Generate/MethodBridgeAndReversePInvokeWrapper", priority = 101)]
        public static void GenerateMethodBridgeAndReversePInvokeWrapperAlias()
        {
            MethodBridgeGeneratorCommand.GenerateMethodBridgeAndReversePInvokeWrapper();
        }

        [MenuItem("HybridCLR/Generate/AOTGenericReference", priority = 102)]
        public static void GenerateAOTGenericReferenceAlias()
        {
            AOTReferenceGeneratorCommand.CompileAndGenerateAOTGenericReference();
        }

        [MenuItem("HybridCLR/Generate/Il2CppDef", priority = 104)]
        public static void GenerateIl2CppDefAlias()
        {
            Il2CppDefGeneratorCommand.GenerateIl2CppDef();
        }

        [MenuItem("HybridCLR/Generate/AOTDlls", priority = 105)]
        public static void GenerateAOTDllsAlias()
        {
            StripAOTDllCommand.GenerateStripedAOTDlls();
        }

        [MenuItem("HybridCLR/Generate/All", priority = 200)]
        public static void GenerateAllAlias()
        {
            PrebuildCommand.GenerateAll();
        }
    }
}
