using System.IO;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Installer
{
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
            EditorGUILayout.LabelField($"Installed: {hasInstall}", EditorStyles.boldLabel);
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
                    "Bundled libil2cpp not found! Please git pull the hotc233-unity repo to get the latest libil2cpp.",
                    MessageType.Error);
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "libil2cpp is bundled inside the package. Update via: git pull",
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
                _installFromDir = EditorGUILayout.Toggle("Copy libil2cpp from local", _installFromDir, GUILayout.MinWidth(100));
                EditorGUI.BeginDisabledGroup(!_installFromDir);
                EditorGUILayout.TextField(_installLibil2cppSourceDir, GUILayout.Width(400));
                if (GUILayout.Button("Choose", GUILayout.Width(100)))
                {
                    _installLibil2cppSourceDir = EditorUtility.OpenFolderPanel("Select libil2cpp", Application.dataPath, "libil2cpp");
                }
                EditorGUI.EndDisabledGroup();
                EditorGUILayout.EndHorizontal();

                GUILayout.Space(20f);

                EditorGUILayout.BeginHorizontal();
                EditorGUI.BeginDisabledGroup(!hasBundled && !_installFromDir);
                if (GUILayout.Button("Install", GUILayout.Width(100)))
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
                    $"Incompatible with current version, minimum compatible version:{_controller.GetCurrentUnityVersionMinCompatibleVersionStr()}",
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
                    Debug.LogError($"Source libil2cpp:'{_installLibil2cppSourceDir}' doesn't exist.");
                    return;
                }
                if (!File.Exists($"{_installLibil2cppSourceDir}/il2cpp-config.h") || !File.Exists($"{_installLibil2cppSourceDir}/hybridclr/RuntimeApi.cpp"))
                {
                    Debug.LogError($"Source libil2cpp:'{_installLibil2cppSourceDir}' is invalid (missing il2cpp-config.h or hybridclr/RuntimeApi.cpp)");
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
