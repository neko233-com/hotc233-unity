using System;
using System.Collections.Generic;

namespace Hotc233
{
    /// <summary>
    /// YooAsset address catalog for automated hot-update resource verification.
    /// Address = asset file name without extension (AddressByFileName rule).
    /// </summary>
    public static class HotUpdateResourceCatalog
    {
        public static readonly ResourceProbeEntry[] Probes =
        {
            new ResourceProbeEntry("HotUpdatePrefabProbe", "UnityHotc.CodeHotUpdate.HotUpdatePrefabProbe", ResourceKind.Prefab, "RuntimeReadyScore", 4, 5.6f),
            new ResourceProbeEntry("HotUpdateUIPrefab", "UnityHotc.CodeHotUpdate.HotUpdateUIController", ResourceKind.Prefab, "ClickCount", 1, 0.5f, invokeMethod: "OnButtonClick"),
            new ResourceProbeEntry("HotUpdate2DPrefab", "UnityHotc.CodeHotUpdate.HotUpdateSpriteProbe", ResourceKind.Prefab, "TickCount", 1, 1.6f),
            new ResourceProbeEntry("HotUpdate3DPrefab", "UnityHotc.CodeHotUpdate.HotUpdateMeshProbe", ResourceKind.Prefab, "TickCount", 1, 1.6f),
            new ResourceProbeEntry("HotUpdateConfig", "UnityHotc.CodeHotUpdate.HotUpdateConfig", ResourceKind.ScriptableObject, "Version", 233, 0f),
        };

        public enum ResourceKind
        {
            Prefab,
            ScriptableObject,
        }

        public sealed class ResourceProbeEntry
        {
            public ResourceProbeEntry(
                string address,
                string componentOrAssetType,
                ResourceKind kind,
                string metricProperty,
                int expectedMinimum,
                float waitSeconds,
                string invokeMethod = null)
            {
                Address = address;
                ComponentOrAssetType = componentOrAssetType;
                Kind = kind;
                MetricProperty = metricProperty;
                ExpectedMinimum = expectedMinimum;
                WaitSeconds = waitSeconds;
                InvokeMethod = invokeMethod;
            }

            public string Address { get; }
            public string ComponentOrAssetType { get; }
            public ResourceKind Kind { get; }
            public string MetricProperty { get; }
            public int ExpectedMinimum { get; }
            public float WaitSeconds { get; }
            public string InvokeMethod { get; }
        }
    }
}
