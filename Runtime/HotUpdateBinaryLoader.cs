using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public sealed class HotUpdateBinaryLoader
    {
        private readonly List<Assembly> assemblies = new List<Assembly>();

        public IReadOnlyList<Assembly> Assemblies => assemblies;

        public void LoadRuntimeMetadata(IEnumerable<NamedBinary> binaries, HomologousImageMode mode)
        {
            if (binaries == null)
            {
                throw new ArgumentNullException(nameof(binaries));
            }

            int count = 0;
            foreach (var binary in binaries)
            {
                if (binary.Bytes == null || binary.Bytes.Length == 0)
                {
                    throw new ArgumentException($"Runtime metadata binary is empty: {binary.Name}");
                }

                Hotc233RuntimeDiagnostics.Info("metadata.load.begin", Hotc233RuntimeDiagnostics.DescribeBinary(binary.Name, binary.Bytes));
                var result = RuntimeApi.LoadMetadataForAOTAssembly(binary.Bytes, mode);
                if (result != LoadImageErrorCode.OK)
                {
                    Hotc233RuntimeDiagnostics.Error("metadata.load.failed", $"{binary.Name} result={result}");
                    throw new InvalidOperationException($"Runtime metadata load failed: {binary.Name}, result:{result}");
                }

                count++;
                Hotc233RuntimeDiagnostics.Info("metadata.load.ok", $"{binary.Name} mode={mode}");
            }

            Hotc233RuntimeDiagnostics.Info("metadata.load.complete", $"count={count}");
        }

        public IReadOnlyList<Assembly> LoadHotUpdateAssemblies(IEnumerable<NamedBinary> binaries)
        {
            if (binaries == null)
            {
                throw new ArgumentNullException(nameof(binaries));
            }

            foreach (var binary in binaries)
            {
                if (binary.Bytes == null || binary.Bytes.Length == 0)
                {
                    throw new ArgumentException($"Hot update binary is empty: {binary.Name}");
                }

                Hotc233RuntimeDiagnostics.Info("assembly.load.begin", Hotc233RuntimeDiagnostics.DescribeBinary(binary.Name, binary.Bytes));
                var assembly = Assembly.Load(binary.Bytes);
                assemblies.Add(assembly);
                Hotc233RuntimeDiagnostics.Info("assembly.load.ok", $"{assembly.GetName().Name} from {binary.Name}");
            }

            Hotc233RuntimeDiagnostics.Info("assembly.load.complete", $"count={assemblies.Count}; names={string.Join(",", assemblies.Select(assembly => assembly.GetName().Name))}");
            return assemblies;
        }

        public object InvokeStatic(string typeName, string methodName, params object[] args)
        {
            if (string.IsNullOrWhiteSpace(typeName))
            {
                throw new ArgumentException("Type name is required.", nameof(typeName));
            }

            if (string.IsNullOrWhiteSpace(methodName))
            {
                throw new ArgumentException("Method name is required.", nameof(methodName));
            }

            var type = assemblies
                .Select(assembly => assembly.GetType(typeName, false))
                .FirstOrDefault(candidate => candidate != null);
            if (type == null)
            {
                Hotc233RuntimeDiagnostics.Error("entry.type.missing", $"{typeName}; loaded={string.Join(",", assemblies.Select(assembly => assembly.GetName().Name))}");
                throw new MissingMemberException($"Type not found in loaded hot update assemblies: {typeName}");
            }

            var method = type.GetMethod(methodName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
            if (method == null)
            {
                Hotc233RuntimeDiagnostics.Error("entry.method.missing", $"{type.FullName}.{methodName}");
                throw new MissingMethodException(type.FullName, methodName);
            }

            Hotc233RuntimeDiagnostics.Info("entry.invoke.begin", $"{type.FullName}.{methodName}");
            try
            {
                object result = method.Invoke(null, args);
                Hotc233RuntimeDiagnostics.Info("entry.invoke.ok", $"{type.FullName}.{methodName}");
                return result;
            }
            catch (TargetInvocationException exception)
            {
                Hotc233RuntimeDiagnostics.Error("entry.invoke.failed", Hotc233RuntimeDiagnostics.DescribeException(exception.InnerException ?? exception, $"{type.FullName}.{methodName}"));
                throw;
            }
        }
    }

    [Preserve]
    public readonly struct NamedBinary
    {
        public NamedBinary(string name, byte[] bytes)
        {
            Name = name;
            Bytes = bytes;
        }

        public string Name { get; }

        public byte[] Bytes { get; }
    }
}
