using System;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Hotc233.Editor.Installer
{
    /// <summary>
    /// 用途: 管理 Hotc233 内置 libil2cpp 就绪流程与本地 libil2cpp 覆盖逻辑。
    /// 关键点: 当前包只面向 Unity 2022 + 团结引擎 1.8.0+；默认直接使用包内内置源码。
    /// 注意事项: 底层运行时代码仍复用既有 native 目录结构；这里仅收口编辑器侧文案与安装入口。
    /// </summary>
    public class InstallerController
    {
        public int MajorVersion => _curVersion.major;

        private readonly UnityVersion _curVersion;

        public string PackageVersion { get; private set; }

        public string InstalledLibil2cppVersion { get; private set; }

        /// <summary>包内内置的 libil2cpp 源码目录。</summary>
        public string BundledLibil2cppDir => $"{SettingsUtil.ProjectDir}/{SettingsUtil.Hotc233DataPathInPackage}/Libil2cpp/2022-tuanjie";

        private string RuntimeMarkerRelativePath => "libil2cpp/hybridclr";

        private string LocalRuntimeMarkerDir => $"{SettingsUtil.LocalIl2CppDir}/{RuntimeMarkerRelativePath}";

        private string BundledRuntimeMarkerDir => $"{BundledLibil2cppDir}/hybridclr";

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
                string packageJson = $"{SettingsUtil.ProjectDir}/Assets/neko233/hotc233/package.json";
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
            return "Unity 2022 + 团结引擎 1.8.0+";
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
                return CompatibleType.Incompatible;
            }
            if (!version.HasMinTuanjieVersion(1, 8, 0))
            {
                return CompatibleType.Incompatible;
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

        public string LocalVersionFile => $"{LocalRuntimeMarkerDir}/generated/libil2cpp-version.txt";

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

        /// <summary>确保包内内置 libil2cpp 已同步到 Unity 可使用的本地工作目录。</summary>
        public void EnsureBuiltinRuntimeReady()
        {
            InstallDefaultHotc233();
        }

        /// <summary>从包内内置 libil2cpp 同步 Hotc233 运行时。</summary>
        public void InstallDefaultHotc233()
        {
            string bundledDir = BundledLibil2cppDir;
            if (!Directory.Exists(bundledDir))
            {
                throw new Exception(
                    $"Bundled libil2cpp not found at: {bundledDir}\n" +
                    "请先更新 hotc233-unity 仓库内容。");
            }
            if (!Directory.Exists(BundledRuntimeMarkerDir))
            {
                throw new Exception(
                    $"Invalid bundled libil2cpp at: {bundledDir}\n" +
                    "运行时标记目录缺失，请同步最新的 hotc233-unity 内容。");
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
                Debug.LogError($"当前环境不受支持，最低要求: {GetCurrentUnityVersionMinCompatibleVersionStr()}");
                return;
            }
            string workDir = SettingsUtil.Hotc233DataDir;
            Directory.CreateDirectory(workDir);

            // 参考 HybridCLR 的本地工作目录思路，尽量保持增量同步，避免每次整目录重拷导致编辑器长时间无响应。
            string localIl2CppDir = SettingsUtil.LocalIl2CppDir;
            Directory.CreateDirectory(localIl2CppDir);

            bool editorFilesChanged = BashUtil.SyncDirIncremental(editorIl2cppPath, localIl2CppDir, true);

            string dstLibil2cppDir = $"{SettingsUtil.LocalIl2CppDir}/libil2cpp";
            bool runtimeFilesChanged = BashUtil.SyncDirIncremental(libil2cppSourceDir, dstLibil2cppDir, true);

            if (editorFilesChanged || runtimeFilesChanged)
            {
                BashUtil.RemoveDir($"{SettingsUtil.ProjectDir}/Library/Il2cppBuildCache", true);
            }
            else
            {
                Debug.Log("Hotc233 安装跳过了未变化文件，同步完成，保留现有 Il2CppBuildCache。");
            }

            if (HasInstalledHotc233())
            {
                WriteLocalVersion();
                Debug.Log("Hotc233 安装完成。");
            }
            else
            {
                Debug.LogError("Hotc233 安装失败。");
            }
        }

        public bool HasInstalledHotc233()
        {
            return Directory.Exists(LocalRuntimeMarkerDir);
        }

        /// <summary>检查包内是否已经包含可安装的 libil2cpp。</summary>
        public bool HasBundledLibil2cpp()
        {
            return Directory.Exists(BundledLibil2cppDir)
                && Directory.Exists(BundledRuntimeMarkerDir);
        }

        private class UnityVersion
        {
            public int major;
            public int minor1;
            public int minor2;
            public bool isTuanjieEngine;
            public int tuanjieMajor;
            public int tuanjieMinor1;
            public int tuanjieMinor2;

            public bool HasMinTuanjieVersion(int majorVersion, int minorVersion, int patchVersion)
            {
                if (!this.isTuanjieEngine)
                {
                    return false;
                }
                if (this.tuanjieMajor != majorVersion)
                {
                    return this.tuanjieMajor > majorVersion;
                }
                if (this.tuanjieMinor1 != minorVersion)
                {
                    return this.tuanjieMinor1 > minorVersion;
                }
                return this.tuanjieMinor2 >= patchVersion;
            }

            public override string ToString()
            {
                return $"{major}.{minor1}.{minor2}";
            }
        }

        private static readonly Regex s_unityVersionPat = new Regex(@"(\d+)\.(\d+)\.(\d+)");

#if TUANJIE_1_1_OR_NEWER
        private static readonly Regex s_tuanjieVersionPat = new Regex(@"(\d+)\.(\d+)\.(\d+)");
#endif

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

            UnityVersion version = new UnityVersion
            {
                major = major,
                minor1 = minor1,
                minor2 = minor2,
                isTuanjieEngine = isTuanjieEngine,
            };

#if TUANJIE_1_1_OR_NEWER
            if (isTuanjieEngine)
            {
                MatchCollection tuanjieMatches = s_tuanjieVersionPat.Matches(Application.tuanjieVersion);
                if (tuanjieMatches.Count > 0)
                {
                    Match tuanjieMatch = tuanjieMatches[tuanjieMatches.Count - 1];
                    version.tuanjieMajor = int.Parse(tuanjieMatch.Groups[1].Value);
                    version.tuanjieMinor1 = int.Parse(tuanjieMatch.Groups[2].Value);
                    version.tuanjieMinor2 = int.Parse(tuanjieMatch.Groups[3].Value);
                }
            }
#endif
            return version;
        }
    }
}
