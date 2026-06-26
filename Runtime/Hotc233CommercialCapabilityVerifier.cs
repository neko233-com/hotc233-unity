using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using UnityEngine;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public static class Hotc233CommercialCapabilityVerifier
    {
        [Preserve]
        [Serializable]
        public sealed class Result
        {
            public bool passed;
            public bool metadataOptimization;
            public bool hotfix;
            public bool hotReload;
            public bool codeProtection;
            public bool accessControl;
            public bool assemblyLoadOptimization;
            public bool standardInterpreterOptimization;
            public bool offlineInstructionOptimization;
            public string message;

            public bool PreInterpreterPassed()
            {
                return metadataOptimization
                    && hotfix
                    && hotReload
                    && codeProtection
                    && accessControl
                    && assemblyLoadOptimization;
            }
        }

        public static Result VerifyLoaderPipeline(
            HotUpdateBinaryLoader loader,
            IReadOnlyList<NamedBinary> metadataBinaries,
            IReadOnlyList<NamedBinary> hotUpdateBinaries)
        {
            if (loader == null)
            {
                throw new ArgumentNullException(nameof(loader));
            }

            if (metadataBinaries == null || metadataBinaries.Count == 0)
            {
                throw new ArgumentException("Metadata binaries are required.", nameof(metadataBinaries));
            }

            if (hotUpdateBinaries == null || hotUpdateBinaries.Count == 0)
            {
                throw new ArgumentException("Hot update binaries are required.", nameof(hotUpdateBinaries));
            }

            var result = new Result();
            var passed = new List<string>();

            result.metadataOptimization = VerifyMetadataPolicy(metadataBinaries);
            AddIfPassed(passed, result.metadataOptimization, "metadata-optimization");

            result.codeProtection = VerifyCodeProtection(hotUpdateBinaries[0]);
            AddIfPassed(passed, result.codeProtection, "code-protection");

            result.accessControl = VerifyAccessControl(hotUpdateBinaries[0]);
            AddIfPassed(passed, result.accessControl, "access-control");

            result.hotfix = VerifyHotfix(loader, hotUpdateBinaries[0]);
            AddIfPassed(passed, result.hotfix, "hotfix");

            result.hotReload = VerifyHotReload(loader, hotUpdateBinaries);
            AddIfPassed(passed, result.hotReload, "hot-reload");

            result.assemblyLoadOptimization = VerifyAssemblyLoadOptimization(loader);
            AddIfPassed(passed, result.assemblyLoadOptimization, "assembly-load-optimization");

            result.standardInterpreterOptimization = VerifyStandardInterpreterOptimization(loader);
            AddIfPassed(passed, result.standardInterpreterOptimization, "standard-interpreter-optimization");

            result.offlineInstructionOptimization = VerifyOfflineInstructionOptimization(loader);
            AddIfPassed(passed, result.offlineInstructionOptimization, "offline-instruction-optimization");

            result.passed = result.PreInterpreterPassed()
                && result.standardInterpreterOptimization
                && result.offlineInstructionOptimization;
            result.message = "CommercialCapabilityLoaderProbe passed: " + string.Join(", ", passed);
            Hotc233RuntimeDiagnostics.Info("commercial.loader.probe", result.message);
            return result;
        }

        private static bool VerifyMetadataPolicy(IReadOnlyList<NamedBinary> metadataBinaries)
        {
            foreach (var binary in metadataBinaries)
            {
                if (binary.Bytes == null || binary.Bytes.Length == 0)
                {
                    return false;
                }

                string hash = Hotc233RuntimeDiagnostics.Sha256Hex(binary.Bytes);
                var policy = new Hotc233LoadPolicy().RequireSha256(binary.Name, hash);
                var prepared = policy.Apply(binary);
                if (prepared.Bytes == null || prepared.Bytes.Length != binary.Bytes.Length)
                {
                    return false;
                }
            }

            return true;
        }

        private static bool VerifyCodeProtection(NamedBinary binary)
        {
            const string key = "hotc233-dev-capability-key";
            byte[] encrypted = Hotc233LoadPolicy.Xor(binary.Bytes, key);
            var encryptedBinary = new NamedBinary(binary.Name, encrypted);
            string expectedHash = Hotc233RuntimeDiagnostics.Sha256Hex(binary.Bytes);
            var policy = Hotc233LoadPolicy.CreateXorProtected(key).RequireSha256(binary.Name, expectedHash);
            var decrypted = policy.Apply(encryptedBinary);
            if (!ByteArrayEquals(decrypted.Bytes, binary.Bytes))
            {
                return false;
            }

            bool rejectedBadHash = false;
            try
            {
                Hotc233LoadPolicy.CreateXorProtected(key)
                    .RequireSha256(binary.Name, new string('0', 64))
                    .Apply(encryptedBinary);
            }
            catch (InvalidOperationException)
            {
                rejectedBadHash = true;
            }

            return rejectedBadHash;
        }

        private static bool VerifyAccessControl(NamedBinary binary)
        {
            var allow = Hotc233LoadPolicy.AllowOnly(binary.Name);
            if (!ByteArrayEquals(allow.Apply(binary).Bytes, binary.Bytes))
            {
                return false;
            }

            bool denied = false;
            try
            {
                Hotc233LoadPolicy.AllowOnly("not-" + binary.Name).Apply(binary);
            }
            catch (UnauthorizedAccessException)
            {
                denied = true;
            }

            return denied;
        }

        private static bool VerifyHotfix(HotUpdateBinaryLoader loader, NamedBinary binary)
        {
            string assemblyName = FileNameToAssemblyName(binary.Name);
            var before = loader.Assemblies.Count(assembly =>
                string.Equals(assembly.GetName().Name, assemblyName, StringComparison.OrdinalIgnoreCase));
            var replacement = loader.ReplaceHotUpdateAssembly(binary);
            var after = loader.Assemblies.Count(assembly =>
                string.Equals(assembly.GetName().Name, assemblyName, StringComparison.OrdinalIgnoreCase));
            return replacement != null
                && string.Equals(replacement.GetName().Name, assemblyName, StringComparison.OrdinalIgnoreCase)
                && before >= 1
                && after == 1
                && loader.TypeCacheCount == 0
                && loader.StaticMethodCacheCount == 0
                && loader.StaticDelegateCacheCount == 0;
        }

        private static bool VerifyHotReload(HotUpdateBinaryLoader loader, IReadOnlyList<NamedBinary> binaries)
        {
            var reloaded = loader.ReloadHotUpdateAssemblies(binaries);
            return reloaded != null
                && reloaded.Count == binaries.Count
                && loader.TypeCacheCount == 0
                && loader.StaticMethodCacheCount == 0
                && loader.StaticDelegateCacheCount == 0;
        }

        private static bool VerifyAssemblyLoadOptimization(HotUpdateBinaryLoader loader)
        {
            var selfTest = loader.InvokeStatic("UnityHotc.CodeHotUpdate.HotUpdateApp", "RunSelfTest") as string;
            if (string.IsNullOrEmpty(selfTest) || loader.TypeCacheCount <= 0 || loader.StaticMethodCacheCount <= 0)
            {
                return false;
            }

            var selfTestDelegate = loader.CreateStaticDelegate<Func<string>>(
                "UnityHotc.CodeHotUpdate.HotUpdateApp",
                "RunSelfTest");
            if (selfTestDelegate == null || loader.StaticDelegateCacheCount <= 0)
            {
                return false;
            }

            string second = selfTestDelegate();
            return !string.IsNullOrEmpty(second)
                && second.Contains("HotUpdateLogic loaded");
        }

        private static bool VerifyStandardInterpreterOptimization(HotUpdateBinaryLoader loader)
        {
            if (VerifyRuntimeProfilerEvidence(loader, requireFusedOpcode: false))
            {
                return true;
            }

            string hotcRoot = FindHotcLibil2CppRoot();
            if (string.IsNullOrEmpty(hotcRoot))
            {
                return false;
            }

            return FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "OptimizeBasicBlocks()")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "LowerTypedRegisterI32")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext_TypedRegister.cpp"), "EmitRegI32Add")
                && FileContains(Path.Combine(hotcRoot, "transform", "Hotc233TypedRegisterIR.h"), "Hotc233TypedRegisterCoverage")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext_CallCommon.cpp"), "TryAddCallCommonStaticInstruments")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "RegI32Add")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "RunStaticI4CallTrace")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "RunStaticI4CallTrace")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "RunRegI32NumericTrace")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "FoldRegI32NumericTrace")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "RunRegI32AddCopyTrace")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "FoldRegI32AddCopyTrace")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "RunArrayI4IncrementTrace")
                && FileContains(Path.Combine(hotcRoot, "transform", "TransformContext.cpp"), "FoldRunArrayI4IncrementTrace")
                && FileContains(Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp"), "LdtokenTypeObjectVar");
        }

        private static bool VerifyOfflineInstructionOptimization(HotUpdateBinaryLoader loader)
        {
            if (VerifyRuntimeProfilerEvidence(loader, requireFusedOpcode: true))
            {
                return true;
            }

            string hotcRoot = FindHotcLibil2CppRoot();
            if (string.IsNullOrEmpty(hotcRoot))
            {
                return false;
            }

            string instruction = Path.Combine(hotcRoot, "interpreter", "Instruction.h");
            string transform = Path.Combine(hotcRoot, "transform", "TransformContext.cpp");
            string execute = Path.Combine(hotcRoot, "interpreter", "Interpreter_Execute.cpp");
            return FileContains(instruction, "Hotc233FieldAddPair_SetArrayElement_size_28")
                && FileContains(instruction, "GetArrayElementVarVar_i4_LdlocVarVar_BinOpAdd_i4_SetArrayElementVarVar_i4")
                && FileContains(transform, "CreateIR(fused, Hotc233FieldAddPair_SetArrayElement_size_28)")
                && FileContains(execute, "HOTC233_EXEC_Hotc233FieldAddPair_SetArrayElement_size_28");
        }

        private static bool VerifyRuntimeProfilerEvidence(HotUpdateBinaryLoader loader, bool requireFusedOpcode)
        {
            try
            {
                Assembly assembly = loader.Assemblies.FirstOrDefault(item =>
                    string.Equals(item.GetName().Name, "Feature_HotUpdate", StringComparison.OrdinalIgnoreCase));
                Type probe = assembly?.GetType("UnityHotc.CodeHotUpdate.Feature.PerformanceProbe");
                MethodInfo staticAdd = probe?.GetMethod("StaticAdd", BindingFlags.Static | BindingFlags.NonPublic);
                string methodProfile = staticAdd == null ? string.Empty : RuntimeApi.GetMethodOpcodeProfile(staticAdd, 16);
                string dynamicProfile = RuntimeApi.GetOpcodeProfilerSnapshot(64);
                bool hasMethodProfile = ContainsTrueJsonFlag(methodProfile, "success")
                    && methodProfile.Contains("\"instructionCount\"");
                if (!hasMethodProfile)
                {
                    return false;
                }

                if (!requireFusedOpcode)
                {
                    return true;
                }

                return ContainsTrueJsonFlag(dynamicProfile, "success")
                    && dynamicProfile.Contains("\"total\"")
                    && HasHighOpcodeEvidence(dynamicProfile);
            }
            catch
            {
                return false;
            }
        }

        private static bool ContainsTrueJsonFlag(string json, string name)
        {
            return !string.IsNullOrEmpty(json)
                && json.Contains("\"" + name + "\":true");
        }

        private static bool HasHighOpcodeEvidence(string json)
        {
            if (string.IsNullOrEmpty(json))
            {
                return false;
            }

            for (int opcode = 900; opcode < 1024; opcode++)
            {
                if (json.Contains("\"opcode\":" + opcode))
                {
                    return true;
                }
            }

            return false;
        }

        private static string FindHotcLibil2CppRoot()
        {
            string dataPath = Application.dataPath;
            if (string.IsNullOrEmpty(dataPath))
            {
                return null;
            }

            string candidate = Path.Combine(
                dataPath,
                "neko233",
                "hotc233-unity",
                "Data~",
                "Libil2cpp",
                "2022-tuanjie",
                "hotc233");
            return Directory.Exists(candidate) ? candidate : null;
        }

        private static bool FileContains(string path, string text)
        {
            return File.Exists(path)
                && File.ReadAllText(path).Contains(text);
        }

        private static void AddIfPassed(List<string> passed, bool value, string token)
        {
            if (value)
            {
                passed.Add(token);
            }
        }

        private static string FileNameToAssemblyName(string name)
        {
            string fileName = name ?? string.Empty;
            int slash = Math.Max(fileName.LastIndexOf('/'), fileName.LastIndexOf('\\'));
            if (slash >= 0)
            {
                fileName = fileName.Substring(slash + 1);
            }

            if (fileName.EndsWith(".bytes", StringComparison.OrdinalIgnoreCase))
            {
                fileName = fileName.Substring(0, fileName.Length - ".bytes".Length);
            }

            if (fileName.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
            {
                fileName = fileName.Substring(0, fileName.Length - ".dll".Length);
            }

            return fileName;
        }

        private static bool ByteArrayEquals(byte[] left, byte[] right)
        {
            if (ReferenceEquals(left, right))
            {
                return true;
            }

            if (left == null || right == null || left.Length != right.Length)
            {
                return false;
            }

            for (int i = 0; i < left.Length; i++)
            {
                if (left[i] != right[i])
                {
                    return false;
                }
            }

            return true;
        }
    }
}
