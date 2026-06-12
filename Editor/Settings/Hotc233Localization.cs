using System;
using System.Collections.Generic;
using UnityEngine;

namespace Hotc233.Editor.Settings
{
    public static class Hotc233Localization
    {
        // Editor logs are the only localized surface for now. Keeping messages in
        // one table makes CompileDll and Generate/All use the same language rules
        // without requiring every command to know about system-language detection.
        private static readonly Dictionary<string, (string zh, string en)> s_Messages =
            new Dictionary<string, (string zh, string en)>(StringComparer.Ordinal)
            {
                {
                    "language.changed",
                    ("[hotc233] 日志语言已切换为：{0}", "[hotc233] Log language changed to: {0}")
                },
                {
                    "language.autoLabel",
                    ("自动识别 ({0})", "Auto detect ({0})")
                },
                {
                    "compile.start",
                    ("[hotc233] 开始编译热更 DLL：目标平台={0}，Development={1}，输出目录={2}",
                        "[hotc233] Start compiling hot update DLLs: target={0}, development={1}, output={2}")
                },
                {
                    "compile.finish",
                    ("[hotc233] 热更 DLL 编译完成：目标平台={0}，Development={1}，输出目录={2}",
                        "[hotc233] Hot update DLL compile finished: target={0}, development={1}, output={2}")
                },
                {
                    "generate.start",
                    ("[hotc233] 开始 Generate/All：目标平台={0}，Development={1}，强制重建={2}",
                        "[hotc233] Start Generate/All: target={0}, development={1}, forceRebuild={2}")
                },
                {
                    "generate.finish",
                    ("[hotc233] Generate/All 完成：目标平台={0}，Development={1}，强制重建={2}",
                        "[hotc233] Generate/All finished: target={0}, development={1}, forceRebuild={2}")
                },
                {
                    "generate.runtimeInitFailed",
                    ("Hotc233 内置 libil2cpp 初始化失败。请检查 Assets/neko233/hotc233/Data~/Libil2cpp。",
                        "Hotc233 builtin libil2cpp initialization failed. Check Assets/neko233/hotc233/Data~/Libil2cpp.")
                },
                {
                    "generate.saveDirtyScene",
                    ("[hotc233] Generate/All 前已保存场景：{0}",
                        "[hotc233] Saved scene before Generate/All: {0}")
                },
                {
                    "generate.skipUnsavedScene",
                    ("[hotc233] 检测到未保存路径的新场景，无法自动保存：{0}",
                        "[hotc233] Found an untitled scene without a path; cannot auto-save: {0}")
                },
                {
                    "pipeline.skip",
                    ("[PrebuildPipeline] 跳过 {0}：目标平台={1}（缓存命中）",
                        "[PrebuildPipeline] Skip {0}: target={1} (cache hit)")
                },
                {
                    "pipeline.run",
                    ("[PrebuildPipeline] 执行 {0}：目标平台={1}（强制重建={2}，输出已存在={3}，指纹匹配={4}）",
                        "[PrebuildPipeline] Run {0}: target={1} (forceRebuild={2}, outputsReady={3}, fingerprintMatches={4})")
                },
                {
                    "pipeline.stateLoadFailed",
                    ("[PrebuildPipeline] 读取缓存状态失败：'{0}'，将重建全部阶段。{1}",
                        "[PrebuildPipeline] Failed to load state file '{0}', rebuild all stages. {1}")
                },
            };

        public static Hotc233LogLanguage EffectiveLanguage
        {
            get
            {
                var configured = Hotc233Settings.Instance.logLanguage;
                if (configured != Hotc233LogLanguage.Auto)
                {
                    return configured;
                }

                // Auto is evaluated at read time so a project can be shared by
                // Chinese and English editor users without committing a language
                // preference into ProjectSettings.
                return IsSystemChinese() ? Hotc233LogLanguage.Chinese : Hotc233LogLanguage.English;
            }
        }

        public static string EffectiveLanguageDisplayName
        {
            get
            {
                return EffectiveLanguage == Hotc233LogLanguage.Chinese ? "中文" : "English";
            }
        }

        public static string Text(string key)
        {
            if (!s_Messages.TryGetValue(key, out var message))
            {
                return key;
            }

            return EffectiveLanguage == Hotc233LogLanguage.Chinese ? message.zh : message.en;
        }

        public static string Format(string key, params object[] args)
        {
            return string.Format(Text(key), args);
        }

        public static string LanguageLabel(Hotc233LogLanguage language)
        {
            switch (language)
            {
                case Hotc233LogLanguage.Chinese:
                    return "中文";
                case Hotc233LogLanguage.English:
                    return "English";
                default:
                    return Format("language.autoLabel", EffectiveLanguageDisplayName);
            }
        }

        private static bool IsSystemChinese()
        {
            // Unity splits Chinese variants into names such as ChineseSimplified
            // and ChineseTraditional, so string matching is more future-proof
            // across supported editor versions than checking one enum value.
            string languageName = Application.systemLanguage.ToString();
            return languageName.IndexOf("Chinese", StringComparison.OrdinalIgnoreCase) >= 0;
        }
    }
}
