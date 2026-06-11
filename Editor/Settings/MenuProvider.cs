using Hotc233.Editor.Installer;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public static class MenuProvider
    {
        [MenuItem("Hotc233/About", priority = 0)]
        public static void OpenAbout() => Debug.Log("Hotc233 - Unity Hot Update Solution by neko233");

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
