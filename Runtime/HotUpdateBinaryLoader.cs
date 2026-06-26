using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using UnityEngine.Scripting;

namespace Hotc233
{
    public enum HotUpdatePerformanceProfile
    {
        Stable = 0,
        RuntimeFast = 1,
        RuntimeOptions = RuntimeFast,
        PreJit = 2,
        Aggressive = 3,
        Compatibility = 4,
    }

    [Preserve]
    public sealed class HotUpdateBinaryLoader
    {
        private readonly List<Assembly> assemblies = new List<Assembly>();
        private readonly Dictionary<string, Type> typeCache = new Dictionary<string, Type>(StringComparer.Ordinal);
        private readonly Dictionary<string, MethodInfo> staticMethodCache = new Dictionary<string, MethodInfo>(StringComparer.Ordinal);
        private readonly Dictionary<string, Delegate> staticDelegateCache = new Dictionary<string, Delegate>(StringComparer.Ordinal);
        private bool performanceDefaultsApplied;
        private const string UnsafePreJitEnvironmentVariable = "HOTC233_UNSAFE_PREJIT";

        public HotUpdatePerformanceProfile PerformanceProfile { get; private set; } = HotUpdatePerformanceProfile.RuntimeFast;

        public IReadOnlyList<Assembly> Assemblies => assemblies;

        public int TypeCacheCount => typeCache.Count;

        public int StaticMethodCacheCount => staticMethodCache.Count;

        public int StaticDelegateCacheCount => staticDelegateCache.Count;

        public bool ApplyPerformanceDefaultsOnLoad { get; set; } = true;

        public bool PreJitHotUpdateTypesOnLoad { get; set; } = false;

        public Hotc233LoadPolicy LoadPolicy { get; set; } = Hotc233LoadPolicy.None;

        public bool AllowUnsafePreJitHotUpdateTypes { get; set; } =
            string.Equals(Environment.GetEnvironmentVariable(UnsafePreJitEnvironmentVariable), "1", StringComparison.OrdinalIgnoreCase)
            || string.Equals(Environment.GetEnvironmentVariable(UnsafePreJitEnvironmentVariable), "true", StringComparison.OrdinalIgnoreCase)
            || string.Equals(Environment.GetEnvironmentVariable(UnsafePreJitEnvironmentVariable), "yes", StringComparison.OrdinalIgnoreCase);

        public int PreJitSucceededCount { get; private set; }

        public int PreJitSkippedCount { get; private set; }

        public int PreJitExceptionCount { get; private set; }

        public int MinMethodBodyCacheSize { get; set; } = 65536;

        public int MinMethodInlineDepth { get; set; } = 8;

        public int MinInlineableMethodBodySize { get; set; } = 96;

        public HotUpdateBinaryLoader UsePerformanceProfile(HotUpdatePerformanceProfile profile)
        {
            PerformanceProfile = profile;
            ApplyPerformanceDefaultsOnLoad = false;
            PreJitHotUpdateTypesOnLoad = false;

            switch (profile)
            {
                case HotUpdatePerformanceProfile.Stable:
                    ApplyPerformanceDefaultsOnLoad = true;
                    MinMethodBodyCacheSize = 65536;
                    MinMethodInlineDepth = 3;
                    MinInlineableMethodBodySize = 32;
                    break;
                case HotUpdatePerformanceProfile.RuntimeFast:
                    ApplyPerformanceDefaultsOnLoad = true;
                    MinMethodBodyCacheSize = 65536;
                    MinMethodInlineDepth = 8;
                    MinInlineableMethodBodySize = 96;
                    break;
                case HotUpdatePerformanceProfile.PreJit:
                    ApplyPerformanceDefaultsOnLoad = true;
                    MinMethodBodyCacheSize = 65536;
                    MinMethodInlineDepth = 3;
                    MinInlineableMethodBodySize = 32;
                    PreJitHotUpdateTypesOnLoad = true;
                    break;
                case HotUpdatePerformanceProfile.Aggressive:
                    ApplyPerformanceDefaultsOnLoad = true;
                    MinMethodBodyCacheSize = 65536;
                    MinMethodInlineDepth = 8;
                    MinInlineableMethodBodySize = 96;
                    PreJitHotUpdateTypesOnLoad = true;
                    break;
                case HotUpdatePerformanceProfile.Compatibility:
                    break;
            }

            return this;
        }

        public void LoadRuntimeMetadata(IEnumerable<NamedBinary> binaries, HomologousImageMode mode)
        {
            if (binaries == null)
            {
                throw new ArgumentNullException(nameof(binaries));
            }

            int count = 0;
            foreach (var binary in binaries)
            {
                var prepared = PrepareBinary(binary);
                if (prepared.Bytes == null || prepared.Bytes.Length == 0)
                {
                    throw new ArgumentException($"Runtime metadata binary is empty: {binary.Name}");
                }

                Hotc233RuntimeDiagnostics.Info("metadata.load.begin", Hotc233RuntimeDiagnostics.DescribeBinary(prepared.Name, prepared.Bytes));
                var result = RuntimeApi.LoadMetadataForAOTAssembly(prepared.Bytes, mode);
                if (result != LoadImageErrorCode.OK)
                {
                    Hotc233RuntimeDiagnostics.Error("metadata.load.failed", $"{prepared.Name} result={result}");
                    throw new InvalidOperationException($"Runtime metadata load failed: {prepared.Name}, result:{result}");
                }

                count++;
                Hotc233RuntimeDiagnostics.Info("metadata.load.ok", $"{prepared.Name} mode={mode}");
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
                var prepared = PrepareBinary(binary);
                if (prepared.Bytes == null || prepared.Bytes.Length == 0)
                {
                    throw new ArgumentException($"Hot update binary is empty: {binary.Name}");
                }

                Hotc233RuntimeDiagnostics.Info("assembly.load.begin", Hotc233RuntimeDiagnostics.DescribeBinary(prepared.Name, prepared.Bytes));
                var assembly = Assembly.Load(prepared.Bytes);
                assemblies.Add(assembly);
                typeCache.Clear();
                staticMethodCache.Clear();
                staticDelegateCache.Clear();
                Hotc233RuntimeDiagnostics.Info("assembly.load.ok", $"{assembly.GetName().Name} from {prepared.Name}");
            }

            Hotc233RuntimeDiagnostics.Info(
                "assembly.performance.profile",
                $"profile={GetPerformanceProfileName(PerformanceProfile)}; runtimeOptions={ApplyPerformanceDefaultsOnLoad}; preJitRequested={PreJitHotUpdateTypesOnLoad}; preJitEnabled={PreJitHotUpdateTypesOnLoad && AllowUnsafePreJitHotUpdateTypes}");
            ApplyPerformanceDefaultsIfNeeded();
            PreJitLoadedAssembliesIfNeeded();
            Hotc233RuntimeDiagnostics.Info("assembly.load.complete", $"count={assemblies.Count}; names={string.Join(",", assemblies.Select(assembly => assembly.GetName().Name))}");
            return assemblies;
        }

        public IReadOnlyList<Assembly> ReloadHotUpdateAssemblies(IEnumerable<NamedBinary> binaries)
        {
            assemblies.Clear();
            typeCache.Clear();
            staticMethodCache.Clear();
            staticDelegateCache.Clear();
            Hotc233RuntimeDiagnostics.Info("assembly.reload.begin", "cleared loader assembly resolution caches");
            return LoadHotUpdateAssemblies(binaries);
        }

        public Assembly ReplaceHotUpdateAssembly(NamedBinary binary)
        {
            var prepared = PrepareBinary(binary);
            if (prepared.Bytes == null || prepared.Bytes.Length == 0)
            {
                throw new ArgumentException($"Hotfix binary is empty: {prepared.Name}");
            }

            var assembly = Assembly.Load(prepared.Bytes);
            string assemblyName = assembly.GetName().Name;
            assemblies.RemoveAll(item => string.Equals(item.GetName().Name, assemblyName, StringComparison.Ordinal));
            assemblies.Insert(0, assembly);
            typeCache.Clear();
            staticMethodCache.Clear();
            staticDelegateCache.Clear();
            Hotc233RuntimeDiagnostics.Info("assembly.hotfix.replace", $"{assemblyName} from {prepared.Name}; old Assembly objects remain loaded by CLR but hotc233 resolution now prefers the replacement");
            return assembly;
        }

        public void ApplyPerformanceDefaultsIfNeeded()
        {
#if UNITY_EDITOR
            return;
#else
            if (!ApplyPerformanceDefaultsOnLoad || performanceDefaultsApplied)
            {
                return;
            }

            SetRuntimeOptionAtLeast(RuntimeOptionId.MaxMethodBodyCacheSize, MinMethodBodyCacheSize);
            SetRuntimeOptionAtLeast(RuntimeOptionId.MaxMethodInlineDepth, MinMethodInlineDepth);
            SetRuntimeOptionAtLeast(RuntimeOptionId.MaxInlineableMethodBodySize, MinInlineableMethodBodySize);
            performanceDefaultsApplied = true;
#endif
        }

        public void PreJitLoadedAssembliesIfNeeded()
        {
#if UNITY_EDITOR
            return;
#else
            if (!PreJitHotUpdateTypesOnLoad)
            {
                return;
            }

            if (!AllowUnsafePreJitHotUpdateTypes)
            {
                Hotc233RuntimeDiagnostics.Warning(
                    "assembly.prejit.skipped",
                    $"{GetPerformanceProfileName(PerformanceProfile)} requested PreJIT, but it is disabled by default. Set {UnsafePreJitEnvironmentVariable}=1 only for experimental profile validation.");
                return;
            }

            int succeeded = 0;
            int skipped = 0;
            int exceptions = 0;
            foreach (var assembly in assemblies)
            {
                foreach (var type in GetLoadableTypes(assembly))
                {
                    if (type == null || type.IsGenericTypeDefinition)
                    {
                        continue;
                    }

                    try
                    {
                        if (RuntimeApi.PreJitClass(type))
                        {
                            succeeded++;
                        }
                        else
                        {
                            skipped++;
                        }
                    }
                    catch (Exception exception)
                    {
                        exceptions++;
                        Hotc233RuntimeDiagnostics.Warning("assembly.prejit.type.failed", $"{type.FullName}: {exception.GetType().Name}: {exception.Message}");
                    }
                }
            }

            PreJitSucceededCount = succeeded;
            PreJitSkippedCount = skipped;
            PreJitExceptionCount = exceptions;
            Hotc233RuntimeDiagnostics.Info("assembly.prejit.complete", $"succeeded={succeeded}; skipped={skipped}; exceptions={exceptions}");
#endif
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

            var type = ResolveType(typeName);
            if (type == null)
            {
                Hotc233RuntimeDiagnostics.Error("entry.type.missing", $"{typeName}; loaded={string.Join(",", assemblies.Select(assembly => assembly.GetName().Name))}");
                throw new MissingMemberException($"Type not found in loaded hot update assemblies: {typeName}");
            }

            var method = ResolveStaticMethod(type, methodName);
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

        public TDelegate CreateStaticDelegate<TDelegate>(string typeName, string methodName)
            where TDelegate : class
        {
            if (string.IsNullOrWhiteSpace(typeName))
            {
                throw new ArgumentException("Type name is required.", nameof(typeName));
            }

            if (string.IsNullOrWhiteSpace(methodName))
            {
                throw new ArgumentException("Method name is required.", nameof(methodName));
            }

            var delegateType = typeof(TDelegate);
            if (!typeof(Delegate).IsAssignableFrom(delegateType))
            {
                throw new ArgumentException($"{delegateType.FullName} is not a delegate type.", nameof(TDelegate));
            }

            var type = ResolveType(typeName);
            if (type == null)
            {
                Hotc233RuntimeDiagnostics.Error("entry.type.missing", $"{typeName}; loaded={string.Join(",", assemblies.Select(assembly => assembly.GetName().Name))}");
                throw new MissingMemberException($"Type not found in loaded hot update assemblies: {typeName}");
            }

            string cacheKey = type.FullName + "::" + methodName + "::" + delegateType.AssemblyQualifiedName;
            if (staticDelegateCache.TryGetValue(cacheKey, out var cachedDelegate))
            {
                return (TDelegate)(object)cachedDelegate;
            }

            var method = ResolveStaticMethod(type, methodName);
            if (method == null)
            {
                Hotc233RuntimeDiagnostics.Error("entry.method.missing", $"{type.FullName}.{methodName}");
                throw new MissingMethodException(type.FullName, methodName);
            }

            try
            {
                var createdDelegate = Delegate.CreateDelegate(delegateType, method);
                staticDelegateCache[cacheKey] = createdDelegate;
                Hotc233RuntimeDiagnostics.Info("entry.delegate.created", $"{type.FullName}.{methodName} as {delegateType.FullName}");
                return (TDelegate)(object)createdDelegate;
            }
            catch (ArgumentException exception)
            {
                Hotc233RuntimeDiagnostics.Error("entry.delegate.failed", Hotc233RuntimeDiagnostics.DescribeException(exception, $"{type.FullName}.{methodName} as {delegateType.FullName}"));
                throw;
            }
        }

        private Type ResolveType(string typeName)
        {
            if (typeCache.TryGetValue(typeName, out var cachedType))
            {
                return cachedType;
            }

            var type = assemblies
                .Select(assembly => assembly.GetType(typeName, false))
                .FirstOrDefault(candidate => candidate != null);
            if (type != null)
            {
                typeCache[typeName] = type;
            }

            return type;
        }

        private NamedBinary PrepareBinary(NamedBinary binary)
        {
            return (LoadPolicy ?? Hotc233LoadPolicy.None).Apply(binary);
        }

        private static void SetRuntimeOptionAtLeast(RuntimeOptionId optionId, int minValue)
        {
            int currentValue = RuntimeApi.GetRuntimeOption(optionId);
            if (currentValue < minValue)
            {
                RuntimeApi.SetRuntimeOption(optionId, minValue);
                Hotc233RuntimeDiagnostics.Info("runtime.option.set", $"{optionId}={minValue} (was {currentValue})");
            }
        }

        private static IEnumerable<Type> GetLoadableTypes(Assembly assembly)
        {
            try
            {
                return assembly.GetTypes();
            }
            catch (ReflectionTypeLoadException exception)
            {
                return exception.Types.Where(type => type != null);
            }
        }

        private static string GetPerformanceProfileName(HotUpdatePerformanceProfile profile)
        {
            return (int)profile == (int)HotUpdatePerformanceProfile.RuntimeFast
                ? "RuntimeFast"
                : profile.ToString();
        }

        private MethodInfo ResolveStaticMethod(Type type, string methodName)
        {
            string cacheKey = type.FullName + "::" + methodName;
            if (staticMethodCache.TryGetValue(cacheKey, out var cachedMethod))
            {
                return cachedMethod;
            }

            var method = type.GetMethod(methodName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
            if (method != null)
            {
                staticMethodCache[cacheKey] = method;
            }

            return method;
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
