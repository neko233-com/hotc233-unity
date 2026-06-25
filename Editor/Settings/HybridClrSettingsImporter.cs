using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditorInternal;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public static class HybridClrSettingsImporter
    {
        private const string HybridClrSettingsPath = "ProjectSettings/HybridCLRSettings.asset";

        [MenuItem("hotc233/HybridCLR Compatibility/Import HybridCLR Settings", priority = 250)]
        public static void ImportHybridClrSettings()
        {
            Import(mirrorOutputPaths: false);
        }

        [MenuItem("hotc233/HybridCLR Compatibility/Import HybridCLR Settings (Mirror Output Paths)", priority = 251)]
        public static void ImportHybridClrSettingsMirrorOutputPaths()
        {
            Import(mirrorOutputPaths: true);
        }

        public static void Import(bool mirrorOutputPaths)
        {
            if (!File.Exists(HybridClrSettingsPath))
            {
                throw new FileNotFoundException(
                    "HybridCLR settings were not found. Open the old project once with HybridCLR or copy ProjectSettings/HybridCLRSettings.asset before importing.",
                    HybridClrSettingsPath);
            }

            var imported = HybridClrSerializedSettings.Read(HybridClrSettingsPath);
            var settings = Hotc233Settings.Instance;
            settings.enable = imported.enable ?? settings.enable;
            settings.useGlobalIl2cpp = imported.useGlobalIl2cpp ?? settings.useGlobalIl2cpp;
            settings.hotUpdateAssemblyDefinitions = ResolveAsmdefs(imported.hotUpdateAssemblyDefinitionGuids);
            settings.hotUpdateAssemblies = imported.hotUpdateAssemblies;
            settings.preserveHotUpdateAssemblies = imported.preserveHotUpdateAssemblies;
            settings.externalHotUpdateAssembliyDirs = imported.externalHotUpdateAssembliyDirs;
            settings.patchAOTAssemblies = imported.patchAOTAssemblies;
            settings.maxGenericReferenceIteration = imported.maxGenericReferenceIteration ?? settings.maxGenericReferenceIteration;
            settings.maxMethodBridgeGenericIteration = imported.maxMethodBridgeGenericIteration ?? settings.maxMethodBridgeGenericIteration;

            if (mirrorOutputPaths)
            {
                settings.hotUpdateDllCompileOutputRootDir = imported.hotUpdateDllCompileOutputRootDir ?? settings.hotUpdateDllCompileOutputRootDir;
                settings.strippedAOTDllOutputRootDir = imported.strippedAOTDllOutputRootDir ?? settings.strippedAOTDllOutputRootDir;
                settings.outputLinkFile = imported.outputLinkFile ?? settings.outputLinkFile;
                settings.outputAOTGenericReferenceFile = imported.outputAOTGenericReferenceFile ?? settings.outputAOTGenericReferenceFile;
            }

            Hotc233Settings.Save();
            AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);

            Debug.Log(
                "[hotc233] Imported HybridCLR settings: " +
                $"{settings.hotUpdateAssemblyDefinitions?.Length ?? 0} asmdef refs, " +
                $"{settings.hotUpdateAssemblies?.Length ?? 0} assembly names, " +
                $"{settings.patchAOTAssemblies?.Length ?? 0} AOT metadata assemblies. " +
                $"Mirror output paths: {mirrorOutputPaths}");
        }

        private static AssemblyDefinitionAsset[] ResolveAsmdefs(IEnumerable<string> guids)
        {
            var result = new List<AssemblyDefinitionAsset>();
            foreach (string guid in guids.Where(item => !string.IsNullOrWhiteSpace(item)).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                string assetPath = AssetDatabase.GUIDToAssetPath(guid);
                if (string.IsNullOrEmpty(assetPath))
                {
                    Debug.LogWarning("[hotc233] HybridCLR asmdef GUID could not be resolved: " + guid);
                    continue;
                }

                var asset = AssetDatabase.LoadAssetAtPath<AssemblyDefinitionAsset>(assetPath);
                if (asset == null)
                {
                    Debug.LogWarning("[hotc233] HybridCLR asmdef GUID resolved to a non-asmdef asset: " + assetPath);
                    continue;
                }

                result.Add(asset);
            }

            return result.ToArray();
        }

        private sealed class HybridClrSerializedSettings
        {
            public bool? enable;
            public bool? useGlobalIl2cpp;
            public string[] hotUpdateAssemblyDefinitionGuids = Array.Empty<string>();
            public string[] hotUpdateAssemblies = Array.Empty<string>();
            public string[] preserveHotUpdateAssemblies = Array.Empty<string>();
            public string hotUpdateDllCompileOutputRootDir;
            public string[] externalHotUpdateAssembliyDirs = Array.Empty<string>();
            public string strippedAOTDllOutputRootDir;
            public string[] patchAOTAssemblies = Array.Empty<string>();
            public string outputLinkFile;
            public string outputAOTGenericReferenceFile;
            public int? maxGenericReferenceIteration;
            public int? maxMethodBridgeGenericIteration;

            public static HybridClrSerializedSettings Read(string path)
            {
                string[] lines = File.ReadAllLines(path);
                return new HybridClrSerializedSettings
                {
                    enable = ReadBool(lines, "enable"),
                    useGlobalIl2cpp = ReadBool(lines, "useGlobalIl2cpp"),
                    hotUpdateAssemblyDefinitionGuids = ReadObjectReferenceGuids(lines, "hotUpdateAssemblyDefinitions"),
                    hotUpdateAssemblies = ReadStringArray(lines, "hotUpdateAssemblies"),
                    preserveHotUpdateAssemblies = ReadStringArray(lines, "preserveHotUpdateAssemblies"),
                    hotUpdateDllCompileOutputRootDir = ReadScalar(lines, "hotUpdateDllCompileOutputRootDir"),
                    externalHotUpdateAssembliyDirs = ReadStringArray(lines, "externalHotUpdateAssembliyDirs"),
                    strippedAOTDllOutputRootDir = ReadScalar(lines, "strippedAOTDllOutputRootDir"),
                    patchAOTAssemblies = ReadStringArray(lines, "patchAOTAssemblies"),
                    outputLinkFile = ReadScalar(lines, "outputLinkFile"),
                    outputAOTGenericReferenceFile = ReadScalar(lines, "outputAOTGenericReferenceFile"),
                    maxGenericReferenceIteration = ReadInt(lines, "maxGenericReferenceIteration"),
                    maxMethodBridgeGenericIteration = ReadInt(lines, "maxMethodBridgeGenericIteration"),
                };
            }

            private static bool? ReadBool(string[] lines, string key)
            {
                string value = ReadScalar(lines, key);
                if (value == null)
                {
                    return null;
                }

                return value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase);
            }

            private static int? ReadInt(string[] lines, string key)
            {
                string value = ReadScalar(lines, key);
                return int.TryParse(value, out int parsed) ? parsed : (int?)null;
            }

            private static string ReadScalar(string[] lines, string key)
            {
                string prefix = "  " + key + ":";
                foreach (string line in lines)
                {
                    string trimmedEnd = line.TrimEnd();
                    if (trimmedEnd.StartsWith(prefix, StringComparison.Ordinal))
                    {
                        return CleanYamlScalar(trimmedEnd.Substring(prefix.Length).Trim());
                    }
                }

                return null;
            }

            private static string[] ReadStringArray(string[] lines, string key)
            {
                return ReadArrayLines(lines, key)
                    .Select(CleanYamlScalar)
                    .Where(item => !string.IsNullOrEmpty(item))
                    .ToArray();
            }

            private static string[] ReadObjectReferenceGuids(string[] lines, string key)
            {
                return ReadArrayLines(lines, key)
                    .Select(line => Regex.Match(line, @"guid:\s*([0-9a-fA-F]{32})"))
                    .Where(match => match.Success)
                    .Select(match => match.Groups[1].Value)
                    .ToArray();
            }

            private static IEnumerable<string> ReadArrayLines(string[] lines, string key)
            {
                string prefix = "  " + key + ":";
                for (int i = 0; i < lines.Length; i++)
                {
                    string trimmedEnd = lines[i].TrimEnd();
                    if (!trimmedEnd.StartsWith(prefix, StringComparison.Ordinal))
                    {
                        continue;
                    }

                    string inlineValue = trimmedEnd.Substring(prefix.Length).Trim();
                    if (inlineValue == "[]" || inlineValue == string.Empty)
                    {
                        for (int j = i + 1; j < lines.Length; j++)
                        {
                            if (!lines[j].StartsWith("  -", StringComparison.Ordinal))
                            {
                                break;
                            }

                            yield return lines[j].Substring(3).Trim();
                        }
                    }
                    else
                    {
                        yield return inlineValue;
                    }

                    yield break;
                }
            }

            private static string CleanYamlScalar(string value)
            {
                if (string.IsNullOrEmpty(value) || value == "[]")
                {
                    return string.Empty;
                }

                return value.Trim().Trim('"', '\'');
            }
        }
    }
}
