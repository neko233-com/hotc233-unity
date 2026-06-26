using System;
using System.Collections.Generic;

namespace Hotc233
{
    /// <summary>
    /// Canonical feature matrix for markdown reports.
    /// </summary>
    public static class Hotc233FeatureCatalog
    {
        public enum SupportStatus
        {
            Passed,
            Failed,
            Unsupported,
            NotRun,
        }

        public enum FeatureCategory
        {
            CSharpLanguage,
            UnityRuntime,
            UnityResource,
            YooAssetResource,
            CommercialCompatibility,
            Workflow,
        }

        public sealed class FeatureDefinition
        {
            public string id;
            public string title;
            public FeatureCategory category;
            public string messageToken;
            public bool hybridClrBaseline;
            public string notes;
        }

        public static readonly FeatureDefinition[] All =
        {
            // C# language (6 base)
            Def("lambda-closure", "Lambda / 闭包", FeatureCategory.CSharpLanguage, "lambda", true),
            Def("generic-method", "泛型方法", FeatureCategory.CSharpLanguage, "generic-method", true),
            Def("generic-type", "泛型类型", FeatureCategory.CSharpLanguage, "generic-type", true),
            Def("iterator-linq", "迭代器 + LINQ Where", FeatureCategory.CSharpLanguage, "iterator-linq", true),
            Def("delegate-closure", "委托闭包", FeatureCategory.CSharpLanguage, "delegate-closure", true),
            Def("exception-filter", "异常过滤器 when", FeatureCategory.CSharpLanguage, "exception-filter", true),
            // C# extended (12)
            Def("interface-dispatch", "接口多态分派", FeatureCategory.CSharpLanguage, "interface-dispatch", true),
            Def("struct-byref", "结构体 ref 传参", FeatureCategory.CSharpLanguage, "struct-byref", true),
            Def("nullable-types", "Nullable 可空类型", FeatureCategory.CSharpLanguage, "nullable-types", true),
            Def("enum-operations", "枚举 Flags/Parse", FeatureCategory.CSharpLanguage, "enum-operations", true),
            Def("multidim-array", "多维 / 锯齿数组", FeatureCategory.CSharpLanguage, "multidim-array", true),
            Def("event-pattern", "event 模式", FeatureCategory.CSharpLanguage, "event-pattern", true),
            Def("reflection-invoke", "反射调用", FeatureCategory.CSharpLanguage, "reflection-invoke", true),
            Def("nested-generics", "嵌套泛型", FeatureCategory.CSharpLanguage, "nested-generics", true),
            Def("static-generic-class", "静态泛型类", FeatureCategory.CSharpLanguage, "static-generic-class", true),
            Def("value-tuple", "ValueTuple", FeatureCategory.CSharpLanguage, "value-tuple", true),
            Def("pattern-matching", "模式匹配 switch/is", FeatureCategory.CSharpLanguage, "pattern-matching", true),
            Def("span-like-ops", "ArraySegment / Memory", FeatureCategory.CSharpLanguage, "span-like-ops", true),
            // Reflection comprehensive coverage
            Def("refl-assembly-module-type", "反射 Assembly/Module/Type 查询", FeatureCategory.CSharpLanguage, "refl-assembly-module-type", true),
            Def("refl-constructors-activator", "反射构造函数 / Activator", FeatureCategory.CSharpLanguage, "refl-constructors-activator", true),
            Def("refl-field-property", "反射字段 / 属性读写", FeatureCategory.CSharpLanguage, "refl-field-property", true),
            Def("refl-method-overload", "反射方法重载调用", FeatureCategory.CSharpLanguage, "refl-method-overload", true),
            Def("refl-private-members", "反射私有成员", FeatureCategory.CSharpLanguage, "refl-private-members", true),
            Def("refl-event-delegate", "反射事件 / Delegate.CreateDelegate", FeatureCategory.CSharpLanguage, "refl-event-delegate", true),
            Def("refl-attributes", "反射 Attribute / 参数 Attribute", FeatureCategory.CSharpLanguage, "refl-attributes", true),
            Def("refl-generic-method-type", "反射泛型方法 / 泛型类型", FeatureCategory.CSharpLanguage, "refl-generic-method-type", true),
            Def("refl-array-enum-nullable", "反射数组 / Enum / Nullable", FeatureCategory.CSharpLanguage, "refl-array-enum-nullable", true),
            Def("refl-interface-nested", "反射 InterfaceMap / 嵌套类型", FeatureCategory.CSharpLanguage, "refl-interface-nested", true),
            Def("refl-optional-parameters", "反射可选参数 Type.Missing", FeatureCategory.CSharpLanguage, "refl-optional-parameters", true),
            Def("refl-target-invocation-exception", "反射异常 TargetInvocationException", FeatureCategory.CSharpLanguage, "refl-target-invocation-exception", true),
            // Unity runtime API (8)
            Def("scriptable-object", "ScriptableObject 热更", FeatureCategory.UnityRuntime, "scriptable-object", true),
            Def("unity-callback", "UnityEvent / Action 回调", FeatureCategory.UnityRuntime, "unity-callback", true),
            Def("coroutine-iterator", "协程 IEnumerator", FeatureCategory.UnityRuntime, "coroutine-iterator", true),
            Def("ugui-api", "UGUI Canvas/Button/Text", FeatureCategory.UnityRuntime, "ugui-api", true),
            Def("sprite-2d-api", "2D SpriteRenderer", FeatureCategory.UnityRuntime, "sprite-2d-api", true),
            Def("mesh-3d-api", "3D Mesh / Transform", FeatureCategory.UnityRuntime, "mesh-3d-api", true),
            Def("timeline-api", "Timeline Playable 类型", FeatureCategory.UnityRuntime, "timeline-api", true),
            Def("async-task", "async/await Task", FeatureCategory.UnityRuntime, "async-task", true),
            // YooAsset resources (5)
            Def("res-prefab-probe", "Prefab 生命周期 (HotUpdatePrefabProbe)", FeatureCategory.YooAssetResource, null, true, "YooAsset 地址 HotUpdatePrefabProbe"),
            Def("res-ui-prefab", "UGUI Prefab (HotUpdateUIPrefab)", FeatureCategory.YooAssetResource, null, true, "YooAsset 地址 HotUpdateUIPrefab"),
            Def("res-2d-prefab", "2D Prefab (HotUpdate2DPrefab)", FeatureCategory.YooAssetResource, null, true, "YooAsset 地址 HotUpdate2DPrefab"),
            Def("res-3d-prefab", "3D Prefab (HotUpdate3DPrefab)", FeatureCategory.YooAssetResource, null, true, "YooAsset 地址 HotUpdate3DPrefab"),
            Def("res-scriptable-object", "SO 资源 (HotUpdateConfig)", FeatureCategory.YooAssetResource, null, true, "YooAsset 地址 HotUpdateConfig"),
            // Workflow
            Def("multi-asmdef-load", "多 asmdef 拓扑加载", FeatureCategory.Workflow, "HotUpdateLogic loaded", true),
            Def("ab-dll-bytes", "AssetBundle .dll.bytes 读回", FeatureCategory.Workflow, null, true, "runtimeProbe.source=AssetBundle.LoadFromFile"),
            Def("business-full-generic-sharing", "完全泛型共享", FeatureCategory.CommercialCompatibility, "full-generic-sharing", true,
                "Unity 2022+ il2cpp 泛型元数据与 hotc233 GenericSharing/GenericMetadata 路径"),
            Def("business-metadata-optimization", "元数据优化", FeatureCategory.CommercialCompatibility, "metadata-optimization", true,
                "支持元数据裁剪、完整性校验和 slim/minigame 安全配置"),
            Def("business-standard-interpreter-optimization", "标准解释优化", FeatureCategory.CommercialCompatibility, "standard-interpreter-optimization", true,
                "RuntimeFast、离线 IR 和解释器快路径"),
            Def("business-hotfix", "Hotfix 动态热修复", FeatureCategory.CommercialCompatibility, "hotfix", true,
                "HotUpdateBinaryLoader.ReplaceHotUpdateAssembly"),
            Def("business-hot-reload", "热重载版工作流", FeatureCategory.CommercialCompatibility, "hot-reload", true,
                "HotUpdateBinaryLoader.ReloadHotUpdateAssemblies"),
            Def("business-code-protection", "代码加密 / 加固", FeatureCategory.CommercialCompatibility, "code-protection", true,
                "Hotc233LoadPolicy 解密钩子、XOR 示例和 SHA256 完整性校验"),
            Def("business-access-control", "访问控制策略", FeatureCategory.CommercialCompatibility, "access-control", true,
                "Hotc233LoadPolicy.AllowOnly 与自定义 AccessValidator"),
            Def("business-assembly-load-optimization", "Assembly.Load 加载优化", FeatureCategory.CommercialCompatibility, "assembly-load-optimization", true,
                "本地 payload manifest、StreamingAssets 原始 .dll.bytes 和 loader 缓存"),
            Def("business-crash-log", "解释器栈崩溃日志", FeatureCategory.CommercialCompatibility, "crash-log", true,
                "Hotc233RuntimeDiagnostics session/event/exception 上下文"),
            Def("business-offline-instruction-optimization", "离线指令优化", FeatureCategory.CommercialCompatibility, "offline-instruction-optimization", true,
                "hotc transform 阶段合成 IR 快路径，Player 报告保留 opcode profile"),
            // LINQ aggregate APIs (previously unsupported)
            Def("linq-sum", "LINQ Enumerable.Sum", FeatureCategory.CSharpLanguage, "linq-sum", true,
                "AOT 元数据补充后支持"),
            Def("linq-sum-selector", "LINQ Sum + Selector", FeatureCategory.CSharpLanguage, "linq-sum-selector", true),
            Def("linq-average", "LINQ Average", FeatureCategory.CSharpLanguage, "linq-average", true),
            Def("linq-count", "LINQ Count", FeatureCategory.CSharpLanguage, "linq-count", true),
            Def("linq-longcount", "LINQ LongCount", FeatureCategory.CSharpLanguage, "linq-longcount", true),
            Def("linq-min", "LINQ Min", FeatureCategory.CSharpLanguage, "linq-min", true),
            Def("linq-max", "LINQ Max", FeatureCategory.CSharpLanguage, "linq-max", true),
            Def("linq-aggregate", "LINQ Aggregate", FeatureCategory.CSharpLanguage, "linq-aggregate", true),
            Def("linq-distinct", "LINQ Distinct", FeatureCategory.CSharpLanguage, "linq-distinct", true),
            Def("linq-union", "LINQ Union", FeatureCategory.CSharpLanguage, "linq-union", true),
            Def("linq-intersect", "LINQ Intersect", FeatureCategory.CSharpLanguage, "linq-intersect", true),
            Def("linq-except", "LINQ Except", FeatureCategory.CSharpLanguage, "linq-except", true),
            Def("linq-groupby", "LINQ GroupBy", FeatureCategory.CSharpLanguage, "linq-groupby", true),
            Def("linq-join", "LINQ Join", FeatureCategory.CSharpLanguage, "linq-join", true),
            Def("linq-zip", "LINQ Zip", FeatureCategory.CSharpLanguage, "linq-zip", true),
            Def("linq-orderby", "LINQ OrderBy/OrderByDescending", FeatureCategory.CSharpLanguage, "linq-orderby", true),
            Def("linq-thenby", "LINQ ThenBy", FeatureCategory.CSharpLanguage, "linq-thenby", true),
            Def("linq-selectmany", "LINQ SelectMany", FeatureCategory.CSharpLanguage, "linq-selectmany", true),
            Def("linq-all", "LINQ All", FeatureCategory.CSharpLanguage, "linq-all", true),
            Def("linq-any", "LINQ Any", FeatureCategory.CSharpLanguage, "linq-any", true),
            Def("linq-contains", "LINQ Contains", FeatureCategory.CSharpLanguage, "linq-contains", true),
            Def("linq-single", "LINQ Single", FeatureCategory.CSharpLanguage, "linq-single", true),
            Def("linq-singleordefault", "LINQ SingleOrDefault", FeatureCategory.CSharpLanguage, "linq-singleordefault", true),
            Def("linq-elementat", "LINQ ElementAt", FeatureCategory.CSharpLanguage, "linq-elementat", true),
            Def("linq-take", "LINQ Take", FeatureCategory.CSharpLanguage, "linq-take", true),
            Def("linq-skip", "LINQ Skip", FeatureCategory.CSharpLanguage, "linq-skip", true),
            Def("linq-takewhile", "LINQ TakeWhile", FeatureCategory.CSharpLanguage, "linq-takewhile", true),
            Def("linq-skipwhile", "LINQ SkipWhile", FeatureCategory.CSharpLanguage, "linq-skipwhile", true),
            Def("linq-concat", "LINQ Concat", FeatureCategory.CSharpLanguage, "linq-concat", true),
            Def("linq-sequenceequal", "LINQ SequenceEqual", FeatureCategory.CSharpLanguage, "linq-sequenceequal", true),
            Def("linq-todictionary", "LINQ ToDictionary", FeatureCategory.CSharpLanguage, "linq-todictionary", true),
            Def("linq-tolookup", "LINQ ToLookup", FeatureCategory.CSharpLanguage, "linq-tolookup", true),
            // Timeline custom track E2E (previously unsupported)
            Def("tl-graph-create", "PlayableGraph 创建", FeatureCategory.UnityRuntime, "tl-graph-create", true),
            Def("tl-custom-behaviour", "自定义 PlayableBehaviour", FeatureCategory.UnityRuntime, "tl-custom-behaviour", true),
            Def("tl-clip-instance", "TimelineClip 实例化", FeatureCategory.UnityRuntime, "tl-clip-instance", true),
            Def("tl-output-binding", "PlayableOutput 绑定", FeatureCategory.UnityRuntime, "tl-output-binding", true),
            Def("tl-timeline-asset-playback", "TimelineAsset 自定义片段播放", FeatureCategory.UnityRuntime, "tl-timeline-asset-playback", true),
            // Scene management (previously unsupported)
            Def("scene-api-access", "SceneManagement API 访问", FeatureCategory.UnityResource, "scene-api-access", true),
            Def("scene-type-avail", "Scene 类型可用性", FeatureCategory.UnityResource, "scene-type-avail", true),
            Def("scene-load-byname", "按名称加载场景", FeatureCategory.UnityResource, "scene-load-byname", true),
            // Platform device probe (previously unsupported)
            Def("platform-detect", "平台检测", FeatureCategory.Workflow, "platform-detect", true),
            Def("device-info", "设备信息", FeatureCategory.Workflow, "device-info", true),
            Def("screen-props", "屏幕属性", FeatureCategory.Workflow, "screen-props", true),
            Def("system-info", "系统信息", FeatureCategory.Workflow, "system-info", true),
            Def("input-basic", "输入系统基础", FeatureCategory.Workflow, "input-basic", true),
        };

        private static FeatureDefinition Def(
            string id,
            string title,
            FeatureCategory category,
            string messageToken,
            bool hybridClrBaseline,
            string notes = null,
            bool unsupported = false)
        {
            return new FeatureDefinition
            {
                id = id,
                title = title,
                category = category,
                messageToken = messageToken,
                hybridClrBaseline = hybridClrBaseline,
                notes = notes ?? string.Empty,
            };
        }

        public static SupportStatus ResolveTokenStatus(string verificationMessage, FeatureDefinition feature)
        {
            if (string.IsNullOrEmpty(feature.messageToken))
            {
                return SupportStatus.NotRun;
            }

            if (verificationMessage.Contains(feature.messageToken))
            {
                return SupportStatus.Passed;
            }

            return SupportStatus.Failed;
        }
    }
}
