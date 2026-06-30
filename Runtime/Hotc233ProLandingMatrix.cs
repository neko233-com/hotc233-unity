using System;
using System.Collections.Generic;
using UnityEngine.Scripting;

namespace Hotc233
{
    /// <summary>
    /// Canonical HybridCLR Pro landing matrix for reports and tooling.
    /// Source of truth JSON: docs/pro-landing-matrix.json
    /// </summary>
    [Preserve]
    public static class Hotc233ProLandingMatrix
    {
        public enum LandingStatus
        {
            NotStarted,
            Partial,
            InProgress,
            Landed,
        }

        [Preserve]
        [Serializable]
        public sealed class CapabilityRow
        {
            public string id;
            public string title;
            public string hybridClrDoc;
            public string phase;
            public LandingStatus status;
            public string[] probes;
            public string acceptance;
        }

        public static readonly CapabilityRow[] Capabilities =
        {
            Row("full-generic-sharing", "完全泛型共享", "https://www.hybridclr.cn/docs/business/fullgenericsharing", "P0", LandingStatus.Partial,
                new[] { "CommercialCapabilityProbe.full-generic-sharing", "enableFullGenericSharing" },
                "值类型/引用类型/嵌套/接口泛型与 MakeGenericType WebGL 探针全绿。"),
            Row("metadata-optimization", "元数据优化", "https://www.hybridclr.cn/docs/business/metadataoptimization", "P0", LandingStatus.Partial,
                new[] { "metadata-optimization", "Hotc233LoadPolicy" },
                "metadata bytes / peak heap / load time 相对基线节省 10%-25%。"),
            Row("standard-interpreter-optimization", "标准解释优化", "https://www.hybridclr.cn/docs/business/basicoptimization", "P1", LandingStatus.InProgress,
                new[] { "performance-webgl-hotc-vs-hybridclr-base.json", "RuntimeFast" },
                "全部 HybridCLR 官方 base 行达到 Pro 架构目标。"),
            Row("offline-instruction-optimization", "离线指令优化", "https://www.hybridclr.cn/docs/business/basicoptimization", "P3", LandingStatus.Partial,
                new[] { "webgl-hotc233-opcode-profile.json", "offline-instruction-optimization" },
                "fusion 仅来自 typed IR lowering；禁止通用 linear trace。"),
            Row("hotfix", "Hotfix 动态热修复", "https://www.hybridclr.cn/docs/business/hotfix", "P0", LandingStatus.Partial,
                new[] { "hotfix" }, "ReplaceHotUpdateAssembly 可验证。"),
            Row("hot-reload", "热重载工作流", "https://www.hybridclr.cn/docs/business/hotfix", "P0", LandingStatus.Partial,
                new[] { "hot-reload" }, "ReloadHotUpdateAssemblies 可验证。"),
            Row("code-protection", "代码加密 / 加固", "https://www.hybridclr.cn/docs/business/codeprotection", "P0", LandingStatus.Landed,
                new[] { "code-protection" }, "AES-CBC + HMAC-SHA256 认证 payload、随机 IV、篡改拒绝、名称绑定拒绝、SHA256 明文完整性校验；XOR 仅 legacy/dev。"),
            Row("access-control", "访问控制策略", "https://www.hybridclr.cn/docs/business/accesscontrol", "P0", LandingStatus.Partial,
                new[] { "access-control" }, "AllowOnly 白名单。"),
            Row("assembly-load-optimization", "Assembly.Load 加载优化", "https://www.hybridclr.cn/docs/business/assemblyload", "P0", LandingStatus.Partial,
                new[] { "assembly-load-optimization" }, "Type/Method/Delegate 缓存命中。"),
            Row("interpreter-crash-log", "解释器栈崩溃日志", "https://www.hybridclr.cn/docs/business/interpreterstacktrace", "P0", LandingStatus.Landed,
                new[] { "crash-log", "GetInterpreterStackTraceJson" }, "frameTracking=true。"),
        };

        public static string BuildReportSummary()
        {
            int landed = 0;
            int partial = 0;
            var pending = new List<string>();
            foreach (CapabilityRow row in Capabilities)
            {
                switch (row.status)
                {
                    case LandingStatus.Landed:
                        landed++;
                        break;
                    case LandingStatus.Partial:
                    case LandingStatus.InProgress:
                        partial++;
                        pending.Add(row.id);
                        break;
                }
            }

            return "ProLandingMatrix landed=" + landed
                + " partialOrInProgress=" + partial
                + " pending=" + string.Join(",", pending);
        }

        private static CapabilityRow Row(
            string id,
            string title,
            string doc,
            string phase,
            LandingStatus status,
            string[] probes,
            string acceptance)
        {
            return new CapabilityRow
            {
                id = id,
                title = title,
                hybridClrDoc = doc,
                phase = phase,
                status = status,
                probes = probes,
                acceptance = acceptance,
            };
        }
    }
}
