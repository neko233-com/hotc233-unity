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
        [MenuItem("hotc233/Builtin Runtime...", priority = 60)]
        private static void OpenBuiltinRuntime()
        {
            InstallerWindow window = EditorWindow.GetWindow<InstallerWindow>("Hotc233 Builtin Runtime", true);
            window.minSize = new Vector2(800f, 500f);
        }

        [MenuItem("hotc233/Settings...", priority = 61)]
        public static void OpenSettings() => SettingsService.OpenProjectSettings("Project/Hotc233 Settings");

        [MenuItem("hotc233/Language/Auto Detect", priority = 80)]
        private static void UseAutoLanguage() => SetLanguage(Hotc233LogLanguage.Auto);

        [MenuItem("hotc233/Language/Auto Detect", true)]
        private static bool ValidateAutoLanguage()
        {
            Menu.SetChecked("hotc233/Language/Auto Detect", Hotc233Settings.Instance.logLanguage == Hotc233LogLanguage.Auto);
            return true;
        }

        [MenuItem("hotc233/Language/Chinese", priority = 81)]
        private static void UseChineseLanguage() => SetLanguage(Hotc233LogLanguage.Chinese);

        [MenuItem("hotc233/Language/Chinese", true)]
        private static bool ValidateChineseLanguage()
        {
            Menu.SetChecked("hotc233/Language/Chinese", Hotc233Settings.Instance.logLanguage == Hotc233LogLanguage.Chinese);
            return true;
        }

        [MenuItem("hotc233/Language/English", priority = 82)]
        private static void UseEnglishLanguage() => SetLanguage(Hotc233LogLanguage.English);

        [MenuItem("hotc233/Language/English", true)]
        private static bool ValidateEnglishLanguage()
        {
            Menu.SetChecked("hotc233/Language/English", Hotc233Settings.Instance.logLanguage == Hotc233LogLanguage.English);
            return true;
        }

        private static void SetLanguage(Hotc233LogLanguage language)
        {
            Hotc233Settings.Instance.logLanguage = language;
            Hotc233Settings.Save();
            Debug.Log(Hotc233Localization.Format("language.changed", Hotc233Localization.LanguageLabel(language)));
        }
    }
}
