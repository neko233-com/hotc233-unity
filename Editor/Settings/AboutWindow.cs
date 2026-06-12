using System.IO;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    /// <summary>
    /// Hotc233 About window with quick links for beginners.
    /// </summary>
    public sealed class AboutWindow : EditorWindow
    {
        private Vector2 scroll;

        [MenuItem("hotc233/About", priority = 0)]
        public static void Open()
        {
            var window = GetWindow<AboutWindow>("Hotc233 About", true);
            window.minSize = new Vector2(520f, 420f);
        }

        private void OnGUI()
        {
            scroll = EditorGUILayout.BeginScrollView(scroll);
            EditorGUILayout.LabelField("Hotc233", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("Unity 2022 / 团结 1.8.0+ 原生 C# 热更新方案");
            EditorGUILayout.Space(8);

            EditorGUILayout.LabelField("快速入门", EditorStyles.boldLabel);
            DrawDocButton("功能验证报告 (feature-report.md)", "Assets/EditorForBuild/Generated/feature-report.md");
            DrawDocButton("性能基准报告 (performance-report.md)", "Assets/EditorForBuild/Generated/performance-report.md");
            DrawDocButton("新手文档：CompileDll vs GenerateAll", "docs/hotc233-getting-started.md");
            DrawDocButton("验证矩阵与不支持用法", "docs/unsupported-csharp-usage.md");
            DrawDocButton("项目验证流程", "docs/README.md");
            DrawDocButton("包内架构说明", "Assets/neko233/hotc233/README.md");

            EditorGUILayout.Space(8);
            EditorGUILayout.LabelField("常用菜单", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "CompileDll：只编译热更 DLL（开发调试用）。\n" +
                "Generate/All：发布前完整生成（桥接 + 裁剪元数据 + AOT 参考）。\n" +
                "EditorForBuild/Run Full Verification + Comparison：一键 AB + YooAsset + HybridCLR 对比报告。",
                MessageType.Info);

            EditorGUILayout.Space(8);
            if (GUILayout.Button("打开内置运行时同步面板"))
            {
                GetWindow<Installer.InstallerWindow>("Hotc233 Builtin Runtime", true);
            }

            if (GUILayout.Button("打开 Project Settings / Hotc233"))
            {
                SettingsService.OpenProjectSettings("Project/Hotc233 Settings");
            }

            if (GUILayout.Button("运行全量验证 + 对比"))
            {
                EditorApplication.ExecuteMenuItem("hotc233/EditorForBuild/Run Full Verification + Comparison");
            }

            EditorGUILayout.EndScrollView();
        }

        private static void DrawDocButton(string label, string relativePath)
        {
            if (!GUILayout.Button(label))
            {
                return;
            }

            string projectRoot = Path.GetDirectoryName(Application.dataPath) ?? Application.dataPath;
            string fullPath = Path.Combine(projectRoot, relativePath.Replace('/', Path.DirectorySeparatorChar));
            if (File.Exists(fullPath))
            {
                EditorUtility.RevealInFinder(fullPath);
            }
            else
            {
                EditorUtility.DisplayDialog("Hotc233", "文档不存在: " + fullPath, "OK");
            }
        }
    }
}
