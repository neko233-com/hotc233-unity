using Hotc233.Editor.Installer;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    /// <summary>
    /// Hotc233 菜单入口。
    /// </summary>
    public static class MenuProvider
    {
        [MenuItem("Hotc233/About", priority = 0)]
        public static void OpenAbout() => Debug.Log("Hotc233 - 面向 Unity 2022 与团结引擎 1.8.0+ 的原生热更新方案");

        [MenuItem("Hotc233/Installer...", priority = 60)]
        private static void OpenInstaller()
        {
            InstallerWindow window = EditorWindow.GetWindow<InstallerWindow>("Hotc233 Installer", true);
            window.minSize = new Vector2(800f, 500f);
        }

        [MenuItem("Hotc233/Settings...", priority = 61)]
        public static void OpenSettings() => SettingsService.OpenProjectSettings("Project/Hotc233 Settings");
    }
}
