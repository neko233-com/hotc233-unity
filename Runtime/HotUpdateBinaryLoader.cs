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

            foreach (var binary in binaries)
            {
                if (binary.Bytes == null || binary.Bytes.Length == 0)
                {
                    throw new ArgumentException($"Runtime metadata binary is empty: {binary.Name}");
                }

                var result = RuntimeApi.LoadMetadataForAOTAssembly(binary.Bytes, mode);
                if (result != LoadImageErrorCode.OK)
                {
                    throw new InvalidOperationException($"Runtime metadata load failed: {binary.Name}, result:{result}");
                }
            }
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

                assemblies.Add(Assembly.Load(binary.Bytes));
            }

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
                throw new MissingMemberException($"Type not found in loaded hot update assemblies: {typeName}");
            }

            var method = type.GetMethod(methodName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
            if (method == null)
            {
                throw new MissingMethodException(type.FullName, methodName);
            }

            return method.Invoke(null, args);
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
