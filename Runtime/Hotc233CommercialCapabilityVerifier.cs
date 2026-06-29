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
    public sealed class MetadataOptimizationSummary
    {
        public long baselineTotalBytes;
        public long optimizedTotalBytes;
        public double savingPercent;
        public double loadElapsedMs;
        public long peakHeapDeltaBytes;

        public bool MeetsP0Threshold(double minSavingPercent = 10.0)
        {
            return baselineTotalBytes > 0
                && optimizedTotalBytes > 0
                && optimizedTotalBytes < baselineTotalBytes
                && savingPercent + 0.001 >= minSavingPercent;
        }
    }

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
            public bool hotfixDifferentContent;
            public bool hotReload;
            public bool codeProtection;
            public bool accessControl;
            public bool assemblyLoadOptimization;
            public bool standardInterpreterOptimization;
            public bool offlineInstructionOptimization;
            public long metadataTotalBytes;
            public int metadataAssemblyCount;
            public long metadataBaselineTotalBytes;
            public double metadataSavingPercent;
            public double metadataLoadElapsedMs;
            public long metadataPeakHeapDeltaBytes;
            public string message;

            /// <summary>P0 商业能力（有/无）：不含标准解释优化、离线指令优化等性能项。</summary>
            public bool BusinessCapabilitiesPassed()
            {
                return metadataOptimization
                    && hotfix
                    && hotReload
                    && codeProtection
                    && accessControl
                    && assemblyLoadOptimization;
            }

            public bool PreInterpreterPassed()
            {
                return BusinessCapabilitiesPassed();
            }
        }

        public static Result VerifyLoaderPipeline(
            HotUpdateBinaryLoader loader,
            IReadOnlyList<NamedBinary> metadataBinaries,
            IReadOnlyList<NamedBinary> hotUpdateBinaries,
            IReadOnlyList<NamedBinary> hotfixProbeBinaries = null,
            MetadataOptimizationSummary metadataSummary = null)
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

            var result = new Result
            {
                metadataAssemblyCount = metadataBinaries.Count,
                metadataTotalBytes = metadataBinaries.Sum(binary => binary.Bytes?.LongLength ?? 0),
            };
            var passed = new List<string>();

            result.metadataOptimization = VerifyMetadataPolicy(metadataBinaries);
            if (metadataSummary != null)
            {
                result.metadataBaselineTotalBytes = metadataSummary.baselineTotalBytes;
                result.metadataSavingPercent = metadataSummary.savingPercent;
                result.metadataLoadElapsedMs = metadataSummary.loadElapsedMs;
                result.metadataPeakHeapDeltaBytes = metadataSummary.peakHeapDeltaBytes;
                if (!metadataSummary.MeetsP0Threshold())
                {
                    result.metadataOptimization = false;
                }
            }

            AddIfPassed(passed, result.metadataOptimization, "metadata-optimization");
            if (result.metadataTotalBytes > 0)
            {
                passed.Add("metadata-bytes=" + result.metadataTotalBytes);
            }

            if (metadataSummary != null && metadataSummary.baselineTotalBytes > 0)
            {
                passed.Add("metadata-saving-percent=" + metadataSummary.savingPercent.ToString("F1"));
                passed.Add("metadata-load-ms=" + metadataSummary.loadElapsedMs.ToString("F2"));
                passed.Add("metadata-peak-heap=" + metadataSummary.peakHeapDeltaBytes);
            }

            result.codeProtection = VerifyCodeProtection(hotUpdateBinaries[0]);
            AddIfPassed(passed, result.codeProtection, "code-protection");

            result.accessControl = VerifyAccessControl(hotUpdateBinaries[0]);
            AddIfPassed(passed, result.accessControl, "access-control");

            bool hasDifferentContentHotfixProbe = hotfixProbeBinaries != null && hotfixProbeBinaries.Count >= 2;
            result.hotfixDifferentContent = !hasDifferentContentHotfixProbe || VerifyDifferentContentHotfix(loader, hotfixProbeBinaries);
            result.hotfix = VerifyHotfix(loader, hotUpdateBinaries[0]) && result.hotfixDifferentContent;
            AddIfPassed(passed, result.hotfix, "hotfix");
            AddIfPassed(passed, hasDifferentContentHotfixProbe && result.hotfixDifferentContent, "hotfix-different-content");

            result.hotReload = VerifyHotReload(loader, hotUpdateBinaries);
            AddIfPassed(passed, result.hotReload, "hot-reload");

            result.assemblyLoadOptimization = VerifyAssemblyLoadOptimization(loader);
            AddIfPassed(passed, result.assemblyLoadOptimization, "assembly-load-optimization");

            result.standardInterpreterOptimization = VerifyStandardInterpreterOptimization(loader);
            AddIfPassed(passed, result.standardInterpreterOptimization, "standard-interpreter-optimization");

            result.offlineInstructionOptimization = VerifyOfflineInstructionOptimization(loader);
            AddIfPassed(passed, result.offlineInstructionOptimization, "offline-instruction-optimization");

            result.passed = result.BusinessCapabilitiesPassed();
            if (result.standardInterpreterOptimization)
            {
                passed.Add("standard-interpreter-optimization");
            }

            if (result.offlineInstructionOptimization)
            {
                passed.Add("offline-instruction-optimization");
            }

            result.message = (result.passed ? "CommercialCapabilityLoaderProbe passed: " : "CommercialCapabilityLoaderProbe failed: ")
                + string.Join(", ", passed);
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
            if (replacement == null
                || !string.Equals(replacement.GetName().Name, assemblyName, StringComparison.OrdinalIgnoreCase)
                || before < 1
                || after != 1
                || loader.TypeCacheCount != 0
                || loader.StaticMethodCacheCount != 0
                || loader.StaticDelegateCacheCount != 0)
            {
                return false;
            }

            return VerifyFeatureVerificationAfterLoaderChange(loader);
        }

        private static bool VerifyDifferentContentHotfix(HotUpdateBinaryLoader loader, IReadOnlyList<NamedBinary> binaries)
        {
            try
            {
                var ordered = binaries
                    .Where(binary => binary.Bytes != null && binary.Bytes.Length > 0)
                    .OrderBy(binary => binary.Name, StringComparer.OrdinalIgnoreCase)
                    .ToArray();
                if (ordered.Length < 2)
                {
                    return false;
                }

                string hashA = Hotc233RuntimeDiagnostics.Sha256Hex(ordered[0].Bytes);
                string hashB = Hotc233RuntimeDiagnostics.Sha256Hex(ordered[1].Bytes);
                if (string.Equals(hashA, hashB, StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }

                var first = loader.ReplaceHotUpdateAssembly(ordered[0]);
                int firstValue = InvokeHotfixProbeVersion(loader);
                var second = loader.ReplaceHotUpdateAssembly(ordered[1]);
                int secondValue = InvokeHotfixProbeVersion(loader);
                return first != null
                    && second != null
                    && string.Equals(first.GetName().Name, second.GetName().Name, StringComparison.OrdinalIgnoreCase)
                    && firstValue == 101
                    && secondValue == 202;
            }
            catch (Exception exception)
            {
                Hotc233RuntimeDiagnostics.Error("commercial.hotfix-different-content.failed", exception.Message);
                return false;
            }
        }

        private static int InvokeHotfixProbeVersion(HotUpdateBinaryLoader loader)
        {
            object value = loader.InvokeStatic("Hotc233Probe.HotfixEntry", "GetVersion");
            return value is int version ? version : -1;
        }

        private static bool VerifyHotReload(HotUpdateBinaryLoader loader, IReadOnlyList<NamedBinary> binaries)
        {
            var reloaded = loader.ReloadHotUpdateAssemblies(binaries);
            if (reloaded == null
                || reloaded.Count != binaries.Count
                || loader.TypeCacheCount != 0
                || loader.StaticMethodCacheCount != 0
                || loader.StaticDelegateCacheCount != 0)
            {
                return false;
            }

            return VerifyFeatureVerificationAfterLoaderChange(loader);
        }

        private static bool VerifyFeatureVerificationAfterLoaderChange(HotUpdateBinaryLoader loader)
        {
            try
            {
                string message = loader.InvokeStatic("UnityHotc.CodeHotUpdate.HotUpdateApp", "RunFeatureVerification") as string;
                if (string.IsNullOrEmpty(message))
                {
                    return false;
                }

                HotUpdateVerificationParser.ValidateFeatureVerification(message);
                return true;
            }
            catch (Exception exception)
            {
                Hotc233RuntimeDiagnostics.Error("commercial.feature-verify.failed", exception.Message);
                return false;
            }
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
