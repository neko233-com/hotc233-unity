using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.SceneManagement;
using UnityEngine;
using Hotc233.Editor.Settings;

namespace Hotc233.Editor.Commands
{
    public static class PrebuildCommand
    {
        /// <summary>
        /// Runs every generation step required before an IL2CPP player build.
        /// </summary>
        /// <remarks>
        /// The order is intentional: hot update DLLs and linker data must exist
        /// before stripped AOT assemblies are collected, and stripped AOT
        /// assemblies are required before MethodBridge and AOT reference output
        /// can be generated.
        /// </remarks>
        [MenuItem("hotc233/Generate/All", priority = 200)]
        public static void GenerateAll()
        {
            GenerateAll(EditorUserBuildSettings.activeBuildTarget, EditorUserBuildSettings.development, false);
        }

        [MenuItem("hotc233/Generate/All_ForceRebuild", priority = 201)]
        public static void GenerateAllForceRebuild()
        {
            GenerateAll(EditorUserBuildSettings.activeBuildTarget, EditorUserBuildSettings.development, true);
        }

        public static void GenerateAll(BuildTarget target, bool developmentBuild, bool forceRebuild)
        {
            Debug.Log(Hotc233Localization.Format("generate.start", target, developmentBuild, forceRebuild));
            SaveDirtyOpenScenesWithoutPrompt();

            var installer = new Installer.InstallerController();
            installer.EnsureBuiltinRuntimeReady();

            if (!installer.HasInstalledHotc233())
            {
                throw new BuildFailedException(Hotc233Localization.Text("generate.runtimeInitFailed"));
            }

            PrebuildPipeline.Run(target, developmentBuild, forceRebuild);
            Debug.Log(Hotc233Localization.Format("generate.finish", target, developmentBuild, forceRebuild));
        }

        private static void SaveDirtyOpenScenesWithoutPrompt()
        {
            // In the interactive editor, BuildPipeline.BuildPlayer can show a modal
            // "scene not saved" prompt. Generate/All may call a hidden player build
            // to collect stripped AOT DLLs, so save dirty scenes up front and keep
            // the command usable from menus, CI wrappers, and automation scripts.
            if (Application.isBatchMode)
            {
                return;
            }

            var blockedByUntitledScene = false;
            for (int index = 0; index < EditorSceneManager.sceneCount; index++)
            {
                var scene = EditorSceneManager.GetSceneAt(index);
                if (!scene.IsValid() || !scene.isDirty)
                {
                    continue;
                }

                if (string.IsNullOrEmpty(scene.path))
                {
                    // Untitled scenes have no deterministic path. We cannot save
                    // them silently without choosing a location for the user.
                    // Stop before BuildPipeline can show a blocking modal dialog.
                    blockedByUntitledScene = true;
                    Debug.LogWarning(Hotc233Localization.Format("generate.skipUnsavedScene", scene.name));
                    continue;
                }

                if (EditorSceneManager.SaveScene(scene))
                {
                    Debug.Log(Hotc233Localization.Format("generate.saveDirtyScene", scene.path));
                }
            }

            if (blockedByUntitledScene)
            {
                throw new BuildFailedException(Hotc233Localization.Text("generate.unsavedSceneBlocked"));
            }

            AssetDatabase.SaveAssets();
        }
    }

    internal static class PrebuildPipeline
    {
        // The cache is deliberately stored outside generated C++ and DLL output.
        // Deleting build artifacts should force regeneration through hasOutputs,
        // while keeping this small state file lets unchanged stages skip work.
        [Serializable]
        private sealed class PipelineState
        {
            public List<StageState> stages = new List<StageState>();
        }

        [Serializable]
        private sealed class StageState
        {
            public string name;
            public string fingerprint;
            public string updatedAtUtc;
        }

        private readonly struct PipelineContext
        {
            public PipelineContext(BuildTarget target, bool developmentBuild)
            {
                Target = target;
                DevelopmentBuild = developmentBuild;
            }

            public BuildTarget Target { get; }

            public bool DevelopmentBuild { get; }
        }

        public static void Run(BuildTarget target, bool developmentBuild, bool forceRebuild)
        {
            var context = new PipelineContext(target, developmentBuild);
            var state = LoadState(context);

            RunStage(context, state, "CompileDll", forceRebuild, ComputeCompileFingerprint, HasCompiledDlls, () =>
            {
                CompileDllCommand.CompileDll(target, developmentBuild);
            });

            RunStage(context, state, "GenerateIl2CppDef", forceRebuild, ComputeProjectFingerprint, HasIl2CppDefOutputs, () =>
            {
                Il2CppDefGeneratorCommand.GenerateIl2CppDef();
            });

            RunStage(context, state, "GenerateLinkXml", forceRebuild, ComputeProjectFingerprint, HasLinkOutput, () =>
            {
                LinkGeneratorCommand.GenerateLinkXml(target);
            });

            RunStage(context, state, "GenerateStrippedAotDlls", forceRebuild, ComputeStripFingerprint, HasStrippedAotDlls, () =>
            {
                StripAOTDllCommand.GenerateStripedAOTDlls(target);
            });

            RunStage(context, state, "GenerateMethodBridge", forceRebuild, ComputeMethodBridgeFingerprint, HasMethodBridgeOutput, () =>
            {
                MethodBridgeGeneratorCommand.GenerateMethodBridgeAndReversePInvokeWrapper(target);
            });

            RunStage(context, state, "GenerateAotReference", forceRebuild, ComputeAotReferenceFingerprint, HasAotReferenceOutput, () =>
            {
                AOTReferenceGeneratorCommand.GenerateAOTGenericReference(target);
            });

            SaveState(context, state);
        }

        private static void RunStage(PipelineContext context, PipelineState state, string stageName, bool forceRebuild,
            Func<PipelineContext, string> computeFingerprint, Func<PipelineContext, bool> hasOutputs, Action action)
        {
            string fingerprint = computeFingerprint(context);
            StageState stageState = GetOrCreateStageState(state, stageName);
            bool outputsReady = hasOutputs(context);
            bool fingerprintMatches = stageState.fingerprint == fingerprint;

            // A stage can only be skipped when both the output files still exist
            // and the inputs that matter to that stage have not changed.
            if (!forceRebuild && outputsReady && fingerprintMatches)
            {
                Debug.Log(Hotc233Localization.Format("pipeline.skip", stageName, context.Target));
                return;
            }

            Debug.Log(Hotc233Localization.Format("pipeline.run", stageName, context.Target, forceRebuild, outputsReady, fingerprintMatches));
            action();
            stageState.fingerprint = fingerprint;
            stageState.updatedAtUtc = DateTime.UtcNow.ToString("O");
        }

        private static StageState GetOrCreateStageState(PipelineState state, string stageName)
        {
            StageState stageState = state.stages.FirstOrDefault(stage => stage.name == stageName);
            if (stageState != null)
            {
                return stageState;
            }

            stageState = new StageState { name = stageName };
            state.stages.Add(stageState);
            return stageState;
        }

        private static PipelineState LoadState(PipelineContext context)
        {
            string stateFile = GetStateFilePath(context);
            if (!File.Exists(stateFile))
            {
                return new PipelineState();
            }

            try
            {
                return JsonUtility.FromJson<PipelineState>(File.ReadAllText(stateFile)) ?? new PipelineState();
            }
            catch (Exception exception)
            {
                Debug.LogWarning(Hotc233Localization.Format("pipeline.stateLoadFailed", stateFile, exception.Message));
                return new PipelineState();
            }
        }

        private static void SaveState(PipelineContext context, PipelineState state)
        {
            string stateFile = GetStateFilePath(context);
            Directory.CreateDirectory(Path.GetDirectoryName(stateFile) ?? SettingsUtil.Hotc233DataDir);
            File.WriteAllText(stateFile, JsonUtility.ToJson(state, true));
        }

        private static string GetStateFilePath(PipelineContext context)
        {
            return Path.Combine(SettingsUtil.Hotc233DataDir, "PrebuildCache", $"{context.Target}-{(context.DevelopmentBuild ? "dev" : "release")}.json");
        }

        private static string ComputeCompileFingerprint(PipelineContext context)
        {
            return ComputeFingerprint(context, false, false, false);
        }

        private static string ComputeProjectFingerprint(PipelineContext context)
        {
            return ComputeFingerprint(context, true, false, false);
        }

        private static string ComputeStripFingerprint(PipelineContext context)
        {
            return ComputeFingerprint(context, true, true, false);
        }

        private static string ComputeMethodBridgeFingerprint(PipelineContext context)
        {
            return ComputeFingerprint(context, true, true, true);
        }

        private static string ComputeAotReferenceFingerprint(PipelineContext context)
        {
            return ComputeFingerprint(context, true, false, true);
        }

        private static string ComputeFingerprint(PipelineContext context, bool includeBuildSettings, bool includeScenes, bool includeOutputs)
        {
            var parts = new List<string>
            {
                $"target={context.Target}",
                $"development={context.DevelopmentBuild}",
                $"unityVersion={Application.unityVersion}",
#if TUANJIE_1_1_OR_NEWER
                $"tuanjieVersion={Application.tuanjieVersion}",
#endif
                $"settings={ReadFileIfExists(Path.Combine(SettingsUtil.ProjectDir, "ProjectSettings", "Hotc233Settings.asset"))}",
                $"assemblies={string.Join(";", SettingsUtil.HotUpdateAssemblyNamesIncludePreserved.OrderBy(name => name, StringComparer.Ordinal))}",
                $"patchAot={string.Join(";", SettingsUtil.AOTAssemblyNames.OrderBy(name => name, StringComparer.Ordinal))}",
            };

            if (includeBuildSettings)
            {
                parts.Add($"buildSettings={ReadFileIfExists(Path.Combine(SettingsUtil.ProjectDir, "ProjectSettings", "EditorBuildSettings.asset"))}");
            }

            if (includeScenes)
            {
                parts.Add($"enabledScenes={string.Join(";", EditorBuildSettings.scenes.Where(scene => scene.enabled).Select(scene => scene.path).OrderBy(path => path, StringComparer.Ordinal))}");
            }

            foreach (string sourceFile in EnumerateHotUpdateSourceFiles())
            {
                var fileInfo = new FileInfo(sourceFile);
                parts.Add($"src|{sourceFile}|{fileInfo.Length}|{fileInfo.LastWriteTimeUtc.Ticks}");
            }

            if (includeOutputs)
            {
                foreach (string outputFile in EnumerateOutputFiles(context))
                {
                    if (!File.Exists(outputFile))
                    {
                        parts.Add($"out|{outputFile}|missing");
                        continue;
                    }

                    var fileInfo = new FileInfo(outputFile);
                    parts.Add($"out|{outputFile}|{fileInfo.Length}|{fileInfo.LastWriteTimeUtc.Ticks}");
                }
            }

            using var sha256 = SHA256.Create();
            byte[] hashBytes = sha256.ComputeHash(Encoding.UTF8.GetBytes(string.Join("\n", parts)));
            return BitConverter.ToString(hashBytes).Replace("-", string.Empty);
        }

        private static IEnumerable<string> EnumerateHotUpdateSourceFiles()
        {
            return SettingsUtil.Hotc233Settings.hotUpdateAssemblyDefinitions
                .Where(asset => asset != null)
                .Select(asset => AssetDatabase.GetAssetPath(asset))
                .Where(path => !string.IsNullOrWhiteSpace(path))
                .SelectMany(path => EnumerateAssemblyDirectoryFiles(Path.GetDirectoryName(path) ?? string.Empty))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(path => path, StringComparer.Ordinal);
        }

        private static IEnumerable<string> EnumerateAssemblyDirectoryFiles(string assetDirectory)
        {
            if (string.IsNullOrWhiteSpace(assetDirectory))
            {
                yield break;
            }

            string absoluteDirectory = Path.Combine(SettingsUtil.ProjectDir, assetDirectory);
            if (!Directory.Exists(absoluteDirectory))
            {
                yield break;
            }

            foreach (string file in Directory.GetFiles(absoluteDirectory, "*", SearchOption.AllDirectories)
                .Where(file => !file.EndsWith(".meta", StringComparison.OrdinalIgnoreCase)))
            {
                yield return file;
            }
        }

        private static IEnumerable<string> EnumerateOutputFiles(PipelineContext context)
        {
            string hotUpdateDir = SettingsUtil.GetHotUpdateDllsOutputDirByTarget(context.Target);
            foreach (string assemblyName in SettingsUtil.HotUpdateAssemblyNamesExcludePreserved)
            {
                yield return Path.Combine(hotUpdateDir, assemblyName + ".dll");
            }

            yield return Path.Combine(SettingsUtil.LocalIl2CppDir, "libil2cpp", "hotc233", "Generated", "UnityVersion.h");
            yield return Path.Combine(SettingsUtil.LocalIl2CppDir, "libil2cpp", "hotc233", "Generated", "AssemblyManifest.cpp");
            yield return Path.Combine(Application.dataPath, SettingsUtil.Hotc233Settings.outputLinkFile);
            yield return Path.Combine(Application.dataPath, SettingsUtil.Hotc233Settings.outputAOTGenericReferenceFile);
            yield return Path.Combine(SettingsUtil.GeneratedCppDir, "MethodBridge.cpp");

            string strippedAotDir = SettingsUtil.GetAssembliesPostIl2CppStripDir(context.Target);
            if (Directory.Exists(strippedAotDir))
            {
                foreach (string aotDll in Directory.GetFiles(strippedAotDir, "*.dll", SearchOption.TopDirectoryOnly).OrderBy(path => path, StringComparer.Ordinal))
                {
                    yield return aotDll;
                }
            }
        }

        private static bool HasCompiledDlls(PipelineContext context)
        {
            string hotUpdateDir = SettingsUtil.GetHotUpdateDllsOutputDirByTarget(context.Target);
            return SettingsUtil.HotUpdateAssemblyNamesExcludePreserved.All(assemblyName => File.Exists(Path.Combine(hotUpdateDir, assemblyName + ".dll")));
        }

        private static bool HasIl2CppDefOutputs(PipelineContext context)
        {
            string unityVersionFile = Path.Combine(SettingsUtil.LocalIl2CppDir, "libil2cpp", "hotc233", "Generated", "UnityVersion.h");
            string assemblyManifestFile = Path.Combine(SettingsUtil.LocalIl2CppDir, "libil2cpp", "hotc233", "Generated", "AssemblyManifest.cpp");
            return FileContains(unityVersionFile, "#define HOTC233_UNITY_VERSION ")
                && SettingsUtil.HotUpdateAssemblyNamesIncludePreserved.All(assemblyName => FileContains(assemblyManifestFile, $"\"{assemblyName}\""));
        }

        private static bool HasLinkOutput(PipelineContext context)
        {
            return File.Exists(Path.Combine(Application.dataPath, SettingsUtil.Hotc233Settings.outputLinkFile));
        }

        private static bool HasStrippedAotDlls(PipelineContext context)
        {
            string strippedAotDir = SettingsUtil.GetAssembliesPostIl2CppStripDir(context.Target);
            return Directory.Exists(strippedAotDir) && Directory.GetFiles(strippedAotDir, "*.dll", SearchOption.TopDirectoryOnly).Length > 0;
        }

        private static bool HasMethodBridgeOutput(PipelineContext context)
        {
            return File.Exists(Path.Combine(SettingsUtil.GeneratedCppDir, "MethodBridge.cpp"));
        }

        private static bool HasAotReferenceOutput(PipelineContext context)
        {
            return File.Exists(Path.Combine(Application.dataPath, SettingsUtil.Hotc233Settings.outputAOTGenericReferenceFile));
        }

        private static string ReadFileIfExists(string path)
        {
            return File.Exists(path) ? File.ReadAllText(path) : string.Empty;
        }

        private static bool FileContains(string path, string value)
        {
            return File.Exists(path) && File.ReadAllText(path).Contains(value);
        }
    }
}
