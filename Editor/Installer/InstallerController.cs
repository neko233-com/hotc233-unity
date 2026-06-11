using System;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Hotc233.Editor.Installer
{
    public class InstallerController
    {
        public int MajorVersion => _curVersion.major;

        private readonly UnityVersion _curVersion;

        public string PackageVersion { get; private set; }

        public string InstalledLibil2cppVersion { get; private set; }

        /// <summary>
        /// The libil2cpp is bundled inside the package at Data~/Libil2cpp/2022-tuanjie/.
        /// Users update it via `git pull` on the hotc233-unity repo — no separate repo needed.
        /// </summary>
        public string BundledLibil2cppDir => $"{SettingsUtil.ProjectDir}/{SettingsUtil.Hotc233DataPathInPackage}/Libil2cpp/2022-tuanjie";

        public InstallerController()
        {
            _curVersion = ParseUnityVersion(Application.unityVersion);
            PackageVersion = LoadPackageVersion();
            InstalledLibil2cppVersion = ReadLocalVersion();
        }

        private string LoadPackageVersion()
        {
            try
            {
                string packageJson = $"{SettingsUtil.ProjectDir}/Assets/neko233/hotc233-unity/package.json";
                if (File.Exists(packageJson))
                {
                    var json = File.ReadAllText(packageJson);
                    var match = Regex.Match(json, @"""version""\s*:\s*""([^""]+)""");
                    if (match.Success)
                    {
                        return match.Groups[1].Value;
                    }
                }
            }
            catch { }
            return "1.0.0";
        }

        public string GetCurrentUnityVersionMinCompatibleVersionStr()
        {
            return "2022.3.0";
        }

        public enum CompatibleType
        {
            Compatible,
            MaybeIncompatible,
            Incompatible,
        }

        public CompatibleType GetCompatibleType()
        {
            UnityVersion version = _curVersion;
            if (version == null)
            {
                return CompatibleType.Incompatible;
            }
            if (!version.isTuanjieEngine || version.major != 2022)
            {
                return CompatibleType.MaybeIncompatible;
            }
            return CompatibleType.Compatible;
        }

        public string ApplicationIl2cppPath
        {
            get
            {
                return $"{EditorApplication.applicationContentsPath}/il2cpp";
            }
        }

        public string LocalVersionFile => $"{SettingsUtil.LocalIl2CppDir}/libil2cpp/hybridclr/generated/libil2cpp-version.txt";

        private string ReadLocalVersion()
        {
            if (!File.Exists(LocalVersionFile))
            {
                return null;
            }
            return File.ReadAllText(LocalVersionFile, System.Text.Encoding.UTF8);
        }

        public void WriteLocalVersion()
        {
            InstalledLibil2cppVersion = PackageVersion;
            string dir = Path.GetDirectoryName(LocalVersionFile);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
            {
                Directory.CreateDirectory(dir);
            }
            File.WriteAllText(LocalVersionFile, PackageVersion, System.Text.Encoding.UTF8);
            Debug.Log($"Write installed version:'{PackageVersion}' to {LocalVersionFile}");
        }

        /// <summary>
        /// Install from the bundled libil2cpp inside the package.
        /// The bundled libil2cpp is updated via `git pull` on the hotc233-unity repo.
        /// </summary>
        public void InstallDefaultHotc233()
        {
            string bundledDir = BundledLibil2cppDir;
            if (!Directory.Exists(bundledDir))
            {
                throw new Exception(
                    $"Bundled libil2cpp not found at: {bundledDir}\n" +
                    "Please make sure the hotc233-unity package is up-to-date (git pull).");
            }
            if (!Directory.Exists($"{bundledDir}/hybridclr"))
            {
                throw new Exception(
                    $"Invalid bundled libil2cpp at: {bundledDir}\n" +
                    "The hybridclr subdirectory is missing. Please git pull the latest hotc233-unity.");
            }
            InstallFromLocalLibil2cpp(bundledDir);
        }

        public void InstallFromLocalLibil2cpp(string libil2cppSourceDir)
        {
            RunInitLocalIl2CppData(ApplicationIl2cppPath, libil2cppSourceDir);
        }

        private void RunInitLocalIl2CppData(string editorIl2cppPath, string libil2cppSourceDir)
        {
            if (GetCompatibleType() == CompatibleType.Incompatible)
            {
                Debug.LogError($"Incompatible with current version, minimum compatible version: {GetCurrentUnityVersionMinCompatibleVersionStr()}");
                return;
            }
            string workDir = SettingsUtil.HybridCLRDataDir;
            Directory.CreateDirectory(workDir);

            // create LocalIl2Cpp
            string localUnityDataDir = SettingsUtil.LocalUnityDataDir;
            BashUtil.RecreateDir(localUnityDataDir);

            // copy il2cpp from Unity Editor installation
            BashUtil.CopyDir(editorIl2cppPath, SettingsUtil.LocalIl2CppDir, true);

            // replace libil2cpp with the bundled version
            string dstLibil2cppDir = $"{SettingsUtil.LocalIl2CppDir}/libil2cpp";
            BashUtil.CopyDir(libil2cppSourceDir, dstLibil2cppDir, true);

            // clean Il2cppBuildCache
            BashUtil.RemoveDir($"{SettingsUtil.ProjectDir}/Library/Il2cppBuildCache", true);

            if (HasInstalledHotc233())
            {
                WriteLocalVersion();
                Debug.Log("Hotc233 Install Successfully!");
            }
            else
            {
                Debug.LogError("Hotc233 Installation failed!");
            }
        }

        public bool HasInstalledHotc233()
        {
            return Directory.Exists($"{SettingsUtil.LocalIl2CppDir}/libil2cpp/hybridclr");
        }

        /// <summary>
        /// Check if the bundled libil2cpp exists in the package
        /// </summary>
        public bool HasBundledLibil2cpp()
        {
            return Directory.Exists(BundledLibil2cppDir)
                && Directory.Exists($"{BundledLibil2cppDir}/hybridclr");
        }

        private class UnityVersion
        {
            public int major;
            public int minor1;
            public int minor2;
            public bool isTuanjieEngine;

            public override string ToString()
            {
                return $"{major}.{minor1}.{minor2}";
            }
        }

        private static readonly Regex s_unityVersionPat = new Regex(@"(\d+)\.(\d+)\.(\d+)");

        private UnityVersion ParseUnityVersion(string versionStr)
        {
            var matches = s_unityVersionPat.Matches(versionStr);
            if (matches.Count == 0)
            {
                return null;
            }
            var match = matches[matches.Count - 1];
            int major = int.Parse(match.Groups[1].Value);
            int minor1 = int.Parse(match.Groups[2].Value);
            int minor2 = int.Parse(match.Groups[3].Value);
            bool isTuanjieEngine = versionStr.Contains("t");
            return new UnityVersion { major = major, minor1 = minor1, minor2 = minor2, isTuanjieEngine = isTuanjieEngine };
        }
    }
}
