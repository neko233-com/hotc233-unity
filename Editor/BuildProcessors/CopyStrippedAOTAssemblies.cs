using Hotc233.Editor.AOT;
using Hotc233.Editor.Installer;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEditor.UnityLinker;
using UnityEngine;
#if !UNITY_2021_1_OR_NEWER
using UnityEditor.Il2Cpp;
#endif

namespace Hotc233.Editor.BuildProcessors
{
    internal class CopyStrippedAOTAssemblies : IPostprocessBuildWithReport, IPreprocessBuildWithReport
#if !UNITY_2021_1_OR_NEWER
     , IIl2CppProcessor
#endif
    {

        public int callbackOrder => 0;

#if UNITY_2021_1_OR_NEWER
        public static string GetStripAssembliesDir2021(BuildTarget target)
        {
            string projectDir = SettingsUtil.ProjectDir;
            switch (target)
            {
                case BuildTarget.StandaloneWindows:
                    case BuildTarget.StandaloneWindows64:
                    return $"{projectDir}/Library/Bee/artifacts/WinPlayerBuildProgram/ManagedStripped";
                case BuildTarget.StandaloneLinux64:
                    return $"{projectDir}/Library/Bee/artifacts/LinuxPlayerBuildProgram/ManagedStripped";
                case BuildTarget.WSAPlayer:
                    return $"{projectDir}/Library/Bee/artifacts/UWPPlayerBuildProgram/ManagedStripped";
                case BuildTarget.Android:
                    return $"{projectDir}/Library/Bee/artifacts/Android/ManagedStripped";
#if TUANJIE_2022_3_OR_NEWER
                case BuildTarget.HMIAndroid:
                    return $"{projectDir}/Library/Bee/artifacts/HMIAndroid/ManagedStripped";
#endif
                case BuildTarget.iOS:
#if UNITY_TVOS
                case BuildTarget.tvOS:
#endif
                return $"{projectDir}/Library/Bee/artifacts/iOS/ManagedStripped";
#if UNITY_VISIONOS
                case BuildTarget.VisionOS:
#if UNITY_6000_0_OR_NEWER
                return $"{projectDir}/Library/Bee/artifacts/VisionOS/ManagedStripped";
#else
                return $"{projectDir}/Library/Bee/artifacts/iOS/ManagedStripped";
#endif
#endif
                case BuildTarget.WebGL:
                    return $"{projectDir}/Library/Bee/artifacts/WebGL/ManagedStripped";
                case BuildTarget.StandaloneOSX:
                    return $"{projectDir}/Library/Bee/artifacts/MacStandalonePlayerBuildProgram/ManagedStripped";
                case BuildTarget.PS4:
                    return $"{projectDir}/Library/Bee/artifacts/PS4PlayerBuildProgram/ManagedStripped";
                case BuildTarget.PS5:
                    return $"{projectDir}/Library/Bee/artifacts/PS5PlayerBuildProgram/ManagedStripped";
                case BuildTarget.GameCoreXboxOne:
                case BuildTarget.GameCoreXboxSeries:
                    return $"{projectDir}/Library/Bee/artifacts/GameCorePlayerBuildProgram/ManagedStripped";
#if UNITY_WEIXINMINIGAME
                case BuildTarget.WeixinMiniGame:
                    return $"{projectDir}/Library/Bee/artifacts/WeixinMiniGame/ManagedStripped";
#endif
#if UNITY_OPENHARMONY
                case BuildTarget.OpenHarmony:
                    return $"{projectDir}/Library/Bee/artifacts/OpenHarmonyPlayerBuildProgram/ManagedStripped";
#endif
                default: return "";
            }
        }
#else
        private string GetStripAssembliesDir2020(BuildTarget target)
        {
            string subPath = target == BuildTarget.Android ?
                "assets/bin/Data/Managed" :
                "Data/Managed/";
            return $"{SettingsUtil.ProjectDir}/Temp/StagingArea/{subPath}";
        }

        public void OnBeforeConvertRun(BuildReport report, Il2CppBuildPipelineData data)
        {
            BuildTarget target = report.summary.platform;
            CopyStripDlls(GetStripAssembliesDir2020(target), target);
        }
#endif

        public static void CopyStripDlls(string srcStripDllPath, BuildTarget target)
        {
            if (!SettingsUtil.Enable)
            {
                Debug.Log($"[CopyStrippedAOTAssemblies] disabled");
                return;
            }
            Debug.Log($"[CopyStrippedAOTAssemblies] CopyScripDlls. src:{srcStripDllPath} target:{target}");

            var dstPath = SettingsUtil.GetAssembliesPostIl2CppStripDir(target);

            Directory.CreateDirectory(dstPath);

            foreach (var fileFullPath in Directory.GetFiles(srcStripDllPath, "*.dll"))
            {
                var file = Path.GetFileName(fileFullPath);
                Debug.Log($"[CopyStrippedAOTAssemblies] copy strip dll {fileFullPath} ==> {dstPath}/{file}");
                string destinationPath = Path.Combine(dstPath, file);
                File.Copy($"{fileFullPath}", destinationPath, true);
                if (SettingsUtil.Hotc233Settings.enableMetadataOptimization)
                {
                    byte[] baselineBytes = File.ReadAllBytes(destinationPath);
                    byte[] optimizedBytes = AOTAssemblyMetadataStripper.Strip(baselineBytes);
                    File.WriteAllBytes(destinationPath + ".hotc233-baseline", baselineBytes);
                    File.WriteAllBytes(destinationPath, optimizedBytes);
                }
            }

            if (SettingsUtil.Hotc233Settings.enableMetadataOptimization)
            {
                var rows = new List<Hotc233MetadataOptimizationAssemblyRow>();
                long baselineTotal = 0;
                long optimizedTotal = 0;
                foreach (string optimizedPath in Directory.GetFiles(dstPath, "*.dll"))
                {
                    if (optimizedPath.EndsWith(".hotc233-baseline", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    string baselinePath = optimizedPath + ".hotc233-baseline";
                    if (!File.Exists(baselinePath))
                    {
                        continue;
                    }

                    long baseline = new FileInfo(baselinePath).Length;
                    long optimized = new FileInfo(optimizedPath).Length;
                    baselineTotal += baseline;
                    optimizedTotal += optimized;
                    rows.Add(new Hotc233MetadataOptimizationAssemblyRow
                    {
                        name = Path.GetFileNameWithoutExtension(optimizedPath),
                        baselineBytes = baseline,
                        optimizedBytes = optimized,
                        savingPercent = Hotc233MetadataOptimizationReporter.ComputeSavingPercent(baseline, optimized),
                    });
                }

                var report = Hotc233MetadataOptimizationReporter.BuildReport(rows.ToArray(), target, baselineTotal, optimizedTotal, null);
                string reportPath = Path.Combine(SettingsUtil.Hotc233DataDir, "Generated", "metadata-optimization-report.json");
                Hotc233MetadataOptimizationReporter.WriteReport(reportPath, report);
                Debug.Log($"[CopyStrippedAOTAssemblies] metadata optimization: {report.message}; report={reportPath}");
                if (!report.success)
                {
                    throw new BuildFailedException("[CopyStrippedAOTAssemblies] metadata optimization did not meet P0 savings threshold.");
                }
            }
        }

        public void OnPostprocessBuild(BuildReport report)
        {
#if UNITY_2021_1_OR_NEWER
            BuildTarget target = report.summary.platform;
            string srcStripDllPath = GetStripAssembliesDir2021(target);
            if (!string.IsNullOrEmpty(srcStripDllPath) && Directory.Exists(srcStripDllPath))
            {
                CopyStripDlls(srcStripDllPath, target);
            }
#endif
        }

        public void OnPreprocessBuild(BuildReport report)
        {
            BuildTarget target = report.summary.platform;
            var dstPath = SettingsUtil.GetAssembliesPostIl2CppStripDir(target);
            BashUtil.RecreateDir(dstPath);
        }
    }
}
