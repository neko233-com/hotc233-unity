using Hotc233.Editor.Link;
using Hotc233.Editor.Settings;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.Commands
{

    public static class Il2CppDefGeneratorCommand
    {

        [MenuItem("hotc233/Generate/Il2CppDef", priority = 104)]
        public static void GenerateIl2CppDef()
        {
            GenerateIl2CppDef(EditorUserBuildSettings.activeBuildTarget);
        }

        public static void GenerateIl2CppDef(BuildTarget target)
        {
            var options = new Il2CppDef.Il2CppDefGenerator.Options()
            {
                UnityVersion = Application.unityVersion,
                HotUpdateAssemblies = SettingsUtil.HotUpdateAssemblyNamesIncludePreserved,
                UnityVersionTemplateFile = $"{SettingsUtil.TemplatePathInPackage}/UnityVersion.h.tpl",
                UnityVersionOutputFile = $"{SettingsUtil.GetGeneratedCppDir(target)}/UnityVersion.h",
                AssemblyManifestTemplateFile = $"{SettingsUtil.TemplatePathInPackage}/AssemblyManifest.cpp.tpl",
                AssemblyManifestOutputFile = $"{SettingsUtil.GetGeneratedCppDir(target)}/AssemblyManifest.cpp",
            };

            var g = new Il2CppDef.Il2CppDefGenerator(options);
            g.Generate();
        }
    }
}
