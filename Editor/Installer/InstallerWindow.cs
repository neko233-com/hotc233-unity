using System.IO;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Installer
{
    /// <summary>
    /// Hotc233 内置运行时窗口。
    /// </summary>
    public class InstallerWindow : EditorWindow
    {
        private InstallerController _controller;

        private bool _installFromDir;

        private string _installLibil2cppSourceDir;

        private void OnEnable()
        {
            _controller = new InstallerController();
        }

        private void OnGUI()
        {
            var rect = new Rect
            {
                x = EditorGUIUtility.currentViewWidth - 24,
                y = 5,
                width = 24,
                height = 24
            };
            var content = EditorGUIUtility.IconContent("Settings");
            content.tooltip = "Hotc233 Settings";
            if (GUI.Button(rect, content, GUI.skin.GetStyle("IconButton")))
            {
                SettingsService.OpenProjectSettings("Project/Hotc233 Settings");
            }

            bool hasInstall = _controller.HasInstalledHotc233();
            bool hasBundled = _controller.HasBundledLibil2cpp();

            GUILayout.Space(10f);

            EditorGUILayout.BeginVertical("box");
            EditorGUILayout.LabelField($"Builtin runtime ready: {hasInstall}", EditorStyles.boldLabel);
            GUILayout.Space(10f);

            EditorGUILayout.LabelField($"Package Version:      v{_controller.PackageVersion}");
            GUILayout.Space(5f);
            EditorGUILayout.LabelField($"Installed Version:    v{_controller.InstalledLibil2cppVersion ?? " Unknown"}");
            GUILayout.Space(5f);
            EditorGUILayout.LabelField($"Bundled libil2cpp:     {(hasBundled ? "Yes" : "Missing!")}");
            GUILayout.Space(5f);

            if (!hasBundled)
            {
                EditorGUILayout.HelpBox(
                    "未找到包内内置 libil2cpp，请先同步 hotc233-unity 仓库内容。",
                    MessageType.Error);
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "当前包已内置 libil2cpp，后续只需要更新 hotc233-unity 仓库即可。",
                    MessageType.Info);
            }

            GUILayout.Space(10f);

            InstallerController.CompatibleType compatibleType = _controller.GetCompatibleType();
            if (compatibleType != InstallerController.CompatibleType.Incompatible)
            {
                if (compatibleType == InstallerController.CompatibleType.MaybeIncompatible)
                {
                    EditorGUILayout.HelpBox(
                        $"Maybe incompatible with current version, recommend minimum compatible version:{_controller.GetCurrentUnityVersionMinCompatibleVersionStr()}",
                        MessageType.Warning);
                }

                EditorGUILayout.BeginHorizontal();
                _installFromDir = EditorGUILayout.Toggle("从本地目录覆盖 libil2cpp", _installFromDir, GUILayout.MinWidth(100));
                EditorGUI.BeginDisabledGroup(!_installFromDir);
                EditorGUILayout.TextField(_installLibil2cppSourceDir, GUILayout.Width(400));
                if (GUILayout.Button("Choose", GUILayout.Width(100)))
                {
                    _installLibil2cppSourceDir = EditorUtility.OpenFolderPanel("选择 libil2cpp 目录", Application.dataPath, "libil2cpp");
                }
                EditorGUI.EndDisabledGroup();
                EditorGUILayout.EndHorizontal();

                GUILayout.Space(20f);

                EditorGUILayout.BeginHorizontal();
                EditorGUI.BeginDisabledGroup(!hasBundled && !_installFromDir);
                if (GUILayout.Button("Sync Builtin Runtime", GUILayout.Width(160)))
                {
                    InstallHotc233();
                    GUIUtility.ExitGUI();
                }
                EditorGUI.EndDisabledGroup();
                EditorGUILayout.EndHorizontal();
            }
            else
            {
                EditorGUILayout.HelpBox(
                    $"当前环境不受支持，最低要求: {_controller.GetCurrentUnityVersionMinCompatibleVersionStr()}",
                    MessageType.Error);
            }

            EditorGUILayout.EndVertical();
        }

        private void InstallHotc233()
        {
            if (_installFromDir)
            {
                if (!Directory.Exists(_installLibil2cppSourceDir))
                {
                    Debug.LogError($"源 libil2cpp 目录不存在: '{_installLibil2cppSourceDir}'。");
                    return;
                }
                if (!File.Exists($"{_installLibil2cppSourceDir}/il2cpp-config.h") || !File.Exists($"{_installLibil2cppSourceDir}/hybridclr/RuntimeApi.cpp"))
                {
                    Debug.LogError($"源 libil2cpp 目录无效: '{_installLibil2cppSourceDir}'，缺少必要运行时标记文件。");
                    return;
                }
                _controller.InstallFromLocalLibil2cpp(_installLibil2cppSourceDir);
            }
            else
            {
                _controller.InstallDefaultHotc233();
            }
        }
    }
}
