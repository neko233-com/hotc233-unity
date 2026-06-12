using Hotc233.Editor.Link;
using Hotc233.Editor.Meta;
using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Commands
{
    using Analyzer = Hotc233.Editor.Link.Analyzer;

    public static class LinkGeneratorCommand
    {

        [MenuItem("hotc233/Generate/LinkXml", priority = 100)]
        public static void GenerateLinkXml()
        {
            BuildTarget target = EditorUserBuildSettings.activeBuildTarget;
            CompileDllCommand.CompileDll(target);
            GenerateLinkXml(target);
        }

        public static void GenerateLinkXml(BuildTarget target)
        {
            var ls = SettingsUtil.Hotc233Settings;

            List<string> hotfixAssemblies = SettingsUtil.HotUpdateAssemblyNamesExcludePreserved;

            var analyzer = new Analyzer(MetaUtil.CreateHotUpdateAndAOTAssemblyResolver(target, hotfixAssemblies));
            var refTypes = analyzer.CollectRefs(hotfixAssemblies);

            Debug.Log($"[LinkGeneratorCommand] hotfix assembly count:{hotfixAssemblies.Count}, ref type count:{refTypes.Count} output:{Application.dataPath}/{ls.outputLinkFile}");
            var linkXmlWriter = new LinkXmlWriter();
            linkXmlWriter.Write($"{Application.dataPath}/{ls.outputLinkFile}", refTypes);
            AssetDatabase.Refresh();
        }
    }
}
