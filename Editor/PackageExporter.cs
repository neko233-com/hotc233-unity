using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor
{
    public static class PackageExporter
    {
        private const string PackageRoot = "Assets/neko233/hotc233";
        private const string DefaultOutputDirectory = "Build/Packages";
        // Unity's package exporter intentionally omits package-private "~"
        // folders. hotc233-unity keeps bundled libil2cpp data in Data~, so the
        // exported .unitypackage is useful as a snapshot but not as the canonical
        // distribution format.
        private const string PartialPackageWarning =
            "Unity .unitypackage export does not include package-private folders ending with '~' such as Data~ and Documentation~. " +
            "Use the Assets/neko233/hotc233 folder or UPM/local package distribution for a complete hotc233-unity package.";

        [MenuItem("hotc233/Export/Export unitypackage...", priority = 500)]
        public static void ExportPackageWithDialog()
        {
            string defaultName = BuildDefaultPackageName();
            string selectedPath = EditorUtility.SaveFilePanel(
                "Export hotc233-unity unitypackage",
                Path.GetFullPath(DefaultOutputDirectory),
                defaultName,
                "unitypackage");

            if (string.IsNullOrEmpty(selectedPath))
            {
                return;
            }

            ExportPackage(selectedPath);
        }

        [MenuItem("hotc233/Export/Export unitypackage to Build/Packages", priority = 501)]
        public static void ExportDefaultPackage()
        {
            string outputPath = Path.Combine(DefaultOutputDirectory, BuildDefaultPackageName());
            ExportPackage(outputPath);
        }

        public static void ExportPackage(string outputPath)
        {
            if (string.IsNullOrWhiteSpace(outputPath))
            {
                throw new ArgumentException("Output path is required.", nameof(outputPath));
            }

            if (!AssetDatabase.IsValidFolder(PackageRoot))
            {
                throw new DirectoryNotFoundException("hotc233-unity package root not found: " + PackageRoot);
            }

            string fullOutputPath = Path.GetFullPath(outputPath);
            Directory.CreateDirectory(Path.GetDirectoryName(fullOutputPath) ?? DefaultOutputDirectory);

            AssetDatabase.ExportPackage(
                PackageRoot,
                fullOutputPath,
                ExportPackageOptions.Recurse);

            Debug.Log("[hotc233] Exported unitypackage: " + fullOutputPath);
            Debug.LogWarning("[hotc233] Exported partial unitypackage. " + PartialPackageWarning);
            if (!Application.isBatchMode)
            {
                EditorUtility.RevealInFinder(fullOutputPath);
            }
        }

        private static string BuildDefaultPackageName()
        {
            string version = TryReadPackageVersion();
            return string.IsNullOrEmpty(version)
                ? "hotc233-unity.unitypackage"
                : $"hotc233-unity-{version}.unitypackage";
        }

        private static string TryReadPackageVersion()
        {
            string packageJsonPath = Path.Combine(PackageRoot, "package.json");
            if (!File.Exists(packageJsonPath))
            {
                return string.Empty;
            }

            // Avoid pulling an editor JSON dependency into this tiny exporter.
            // package.json is controlled by the package, so a focused version
            // scan is enough and keeps this menu available in older Unity installs.
            string json = File.ReadAllText(packageJsonPath);
            const string marker = "\"version\"";
            int markerIndex = json.IndexOf(marker, StringComparison.Ordinal);
            if (markerIndex < 0)
            {
                return string.Empty;
            }

            int colonIndex = json.IndexOf(':', markerIndex + marker.Length);
            int firstQuote = colonIndex < 0 ? -1 : json.IndexOf('"', colonIndex + 1);
            int secondQuote = firstQuote < 0 ? -1 : json.IndexOf('"', firstQuote + 1);
            return firstQuote >= 0 && secondQuote > firstQuote
                ? json.Substring(firstQuote + 1, secondQuote - firstQuote - 1)
                : string.Empty;
        }
    }
}
