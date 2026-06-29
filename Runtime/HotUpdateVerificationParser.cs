using System;

namespace Hotc233
{
    /// <summary>
    /// Parses hot update verification output into structured probe results.
    /// Lives in Hotc233.Runtime so main/editor/player code can validate without
    /// compile-time references to hot update assemblies.
    /// </summary>
    public static class HotUpdateVerificationParser
    {
        public static bool IsSelfTestPassed(string message)
        {
            return !string.IsNullOrEmpty(message)
                && message.Contains("HotUpdateLogic loaded")
                && message.Contains("CSharpUsageProbe passed");
        }

        public static bool IsFullVerificationPassed(string message)
        {
            return IsSelfTestPassed(message)
                && message.Contains("FeatureCompatibilityProbe passed")
                && message.Contains("CSharpHybridClrParityProbe passed")
                && message.Contains("ReflectionComprehensiveProbe passed")
                && message.Contains("UnityAssetProbe passed")
                && message.Contains("LinqAggregateProbe passed")
                && message.Contains("CommercialCapabilityProbe passed")
                && message.Contains("TimelineCustomTrackProbe passed")
                && message.Contains("SceneManagementProbe passed")
                && message.Contains("PlatformDeviceProbe passed")
                && message.Contains("PerformanceProbe:");
        }

        public static CSharpUsageFlags ParseCSharpUsage(string message)
        {
            return new CSharpUsageFlags
            {
                passed = message.Contains("CSharpUsageProbe passed"),
                lambda = message.Contains("lambda"),
                genericMethod = message.Contains("generic-method"),
                genericType = message.Contains("generic-type"),
                iteratorAndLinq = message.Contains("iterator-linq"),
                delegateClosure = message.Contains("delegate-closure"),
                exceptionFilter = message.Contains("exception-filter"),
            };
        }

        public static FeatureCompatFlags ParseFeatureCompat(string message)
        {
            return new FeatureCompatFlags
            {
                passed = message.Contains("FeatureCompatibilityProbe passed"),
                interfaceDispatch = message.Contains("interface-dispatch"),
                structByRef = message.Contains("struct-byref"),
                nullableTypes = message.Contains("nullable-types"),
                enumOperations = message.Contains("enum-operations"),
                multidimArray = message.Contains("multidim-array"),
                eventPattern = message.Contains("event-pattern"),
                reflectionInvoke = message.Contains("reflection-invoke"),
                nestedGenerics = message.Contains("nested-generics"),
                staticGenericClass = message.Contains("static-generic-class"),
                valueTuple = message.Contains("value-tuple"),
                patternMatching = message.Contains("pattern-matching"),
                spanLikeOps = message.Contains("span-like-ops"),
                staticExtensionMethods = message.Contains("static-extension-methods"),
                genericExtensionMethods = message.Contains("generic-extension-methods"),
                extensionInterfaceDispatch = message.Contains("extension-interface-dispatch"),
            };
        }

        public static bool IsFeatureVerificationPassed(string message)
        {
            return IsSelfTestPassed(message)
                && message.Contains("FeatureCompatibilityProbe passed")
                && message.Contains("CSharpHybridClrParityProbe passed")
                && message.Contains("ReflectionComprehensiveProbe passed")
                && message.Contains("UnityAssetProbe passed")
                && message.Contains("LinqAggregateProbe passed")
                && message.Contains("CommercialCapabilityProbe passed")
                && message.Contains("TimelineCustomTrackProbe passed")
                && message.Contains("SceneManagementProbe passed")
                && message.Contains("PlatformDeviceProbe passed");
        }

        public static void ValidateFeatureVerification(string message)
        {
            ValidateFeatureProbes(message, requirePerformanceProbe: false);
        }

        public static void ValidateFullVerification(string message)
        {
            ValidateFeatureProbes(message, requirePerformanceProbe: true);
        }

        private static void ValidateFeatureProbes(string message, bool requirePerformanceProbe)
        {
            if (!IsSelfTestPassed(message))
            {
                throw new InvalidOperationException("Self test failed: " + message);
            }

            var csharp = ParseCSharpUsage(message);
            if (!csharp.passed || !csharp.lambda || !csharp.genericMethod || !csharp.genericType)
            {
                throw new InvalidOperationException("C# usage probe failed: " + message);
            }

            var feature = ParseFeatureCompat(message);
            if (!feature.passed || !feature.AllPassed())
            {
                throw new InvalidOperationException("Feature compatibility probe failed: " + message);
            }

            var csharpParity = ParseCSharpHybridClrParity(message);
            if (!csharpParity.passed || !csharpParity.AllPassed())
            {
                throw new InvalidOperationException("C# HybridCLR parity probe failed: " + message);
            }

            var reflection = ParseReflectionComprehensive(message);
            if (!reflection.passed || !reflection.AllPassed())
            {
                throw new InvalidOperationException("Reflection comprehensive probe failed: " + message);
            }

            if (requirePerformanceProbe && !message.Contains("PerformanceProbe:"))
            {
                throw new InvalidOperationException("Performance probe missing from verification output: " + message);
            }

            var unity = ParseUnityAssetFlags(message);
            if (!unity.passed || !unity.AllPassed())
            {
                throw new InvalidOperationException("Unity asset probe failed: " + message);
            }

            var linq = ParseLinqAggregate(message);
            if (!linq.passed || !linq.AllPassed())
            {
                throw new InvalidOperationException("LINQ aggregate probe failed: " + message);
            }

            var commercial = ParseCommercialCapability(message);
            if (!commercial.passed || !commercial.HotUpdateSurfacePassed())
            {
                throw new InvalidOperationException("Commercial capability probe failed: " + message);
            }

            if (message.Contains("CommercialCapabilityLoaderProbe"))
            {
                if (!commercial.PreInterpreterPassed())
                {
                    throw new InvalidOperationException("Commercial loader business capabilities failed: " + message);
                }
            }

            var timeline = ParseTimelineCustomTrack(message);
            if (!timeline.passed || !timeline.AllPassed())
            {
                throw new InvalidOperationException("Timeline custom track probe failed: " + message);
            }

            var scene = ParseSceneManagement(message);
            if (!scene.passed || !scene.AllPassed())
            {
                throw new InvalidOperationException("Scene management probe failed: " + message);
            }

            var platform = ParsePlatformDevice(message);
            if (!platform.passed || !platform.AllPassed())
            {
                throw new InvalidOperationException("Platform device probe failed: " + message);
            }
        }

        public static UnityAssetFlags ParseUnityAssetFlags(string message)
        {
            return new UnityAssetFlags
            {
                passed = message.Contains("UnityAssetProbe passed"),
                scriptableObject = message.Contains("scriptable-object"),
                unityCallback = message.Contains("unity-callback"),
                coroutineIterator = message.Contains("coroutine-iterator"),
                uguiApi = message.Contains("ugui-api"),
                sprite2DApi = message.Contains("sprite-2d-api"),
                mesh3DApi = message.Contains("mesh-3d-api"),
                timelineApi = message.Contains("timeline-api"),
                asyncTask = message.Contains("async-task"),
            };
        }

        public static ReflectionComprehensiveFlags ParseReflectionComprehensive(string message)
        {
            return new ReflectionComprehensiveFlags
            {
                passed = message.Contains("ReflectionComprehensiveProbe passed"),
                assemblyModuleType = message.Contains("refl-assembly-module-type"),
                constructorsActivator = message.Contains("refl-constructors-activator"),
                fieldProperty = message.Contains("refl-field-property"),
                methodOverload = message.Contains("refl-method-overload"),
                privateMembers = message.Contains("refl-private-members"),
                eventDelegate = message.Contains("refl-event-delegate"),
                attributes = message.Contains("refl-attributes"),
                genericMethodType = message.Contains("refl-generic-method-type"),
                arrayEnumNullable = message.Contains("refl-array-enum-nullable"),
                interfaceNested = message.Contains("refl-interface-nested"),
                optionalParameters = message.Contains("refl-optional-parameters"),
                targetInvocationException = message.Contains("refl-target-invocation-exception"),
            };
        }

        public static LinqAggregateFlags ParseLinqAggregate(string message)
        {
            return new LinqAggregateFlags
            {
                passed = message.Contains("LinqAggregateProbe passed"),
                sum = message.Contains("linq-sum,") || message.Contains("linq-sum;"),
                sumSelector = message.Contains("linq-sum-selector"),
                average = message.Contains("linq-average"),
                count = message.Contains("linq-count,") || message.Contains("linq-count;"),
                longCount = message.Contains("linq-longcount"),
                min = message.Contains("linq-min,") || message.Contains("linq-min;"),
                max = message.Contains("linq-max,") || message.Contains("linq-max;"),
                aggregate = message.Contains("linq-aggregate"),
                distinct = message.Contains("linq-distinct"),
                union = message.Contains("linq-union"),
                intersect = message.Contains("linq-intersect"),
                except = message.Contains("linq-except"),
                groupBy = message.Contains("linq-groupby"),
                join = message.Contains("linq-join"),
                zip = message.Contains("linq-zip"),
                orderBy = message.Contains("linq-orderby"),
                thenBy = message.Contains("linq-thenby"),
                selectMany = message.Contains("linq-selectmany"),
                all = message.Contains("linq-all,") || message.Contains("linq-all;"),
                any = message.Contains("linq-any,") || message.Contains("linq-any;"),
                contains = message.Contains("linq-contains"),
                single = message.Contains("linq-single,") || message.Contains("linq-single;"),
                singleOrDefault = message.Contains("linq-singleordefault"),
                elementAt = message.Contains("linq-elementat"),
                take = message.Contains("linq-take,") || message.Contains("linq-take;"),
                skip = message.Contains("linq-skip,") || message.Contains("linq-skip;"),
                takeWhile = message.Contains("linq-takewhile"),
                skipWhile = message.Contains("linq-skipwhile"),
                concat = message.Contains("linq-concat"),
                sequenceEqual = message.Contains("linq-sequenceequal"),
                toDictionary = message.Contains("linq-todictionary"),
                toLookup = message.Contains("linq-tolookup"),
            };
        }

        public static CommercialCapabilityFlags ParseCommercialCapability(string message)
        {
            return new CommercialCapabilityFlags
            {
                passed = message.Contains("CommercialCapabilityProbe passed"),
                fullGenericSharing = message.Contains("full-generic-sharing")
                    && message.Contains("full-generic-sharing-matrix")
                    && message.Contains("full-generic-sharing-virtual")
                    && message.Contains("full-generic-sharing-delegate"),
                crashLog = message.Contains("crash-log"),
                metadataOptimization = message.Contains("metadata-optimization")
                    && (!message.Contains("CommercialCapabilityLoaderProbe") || message.Contains("metadata-saving-percent=")),
                hotfix = message.Contains("hotfix"),
                hotReload = message.Contains("hot-reload"),
                codeProtection = message.Contains("code-protection"),
                accessControl = message.Contains("access-control"),
                assemblyLoadOptimization = message.Contains("assembly-load-optimization"),
                standardInterpreterOptimization = message.Contains("standard-interpreter-optimization"),
                offlineInstructionOptimization = message.Contains("offline-instruction-optimization"),
            };
        }

        public static TimelineCustomTrackFlags ParseTimelineCustomTrack(string message)
        {
            return new TimelineCustomTrackFlags
            {
                passed = message.Contains("TimelineCustomTrackProbe passed"),
                graphCreate = message.Contains("tl-graph-create"),
                customBehaviour = message.Contains("tl-custom-behaviour"),
                clipInstance = message.Contains("tl-clip-instance"),
                outputBinding = message.Contains("tl-output-binding"),
                timelineAssetPlayback = message.Contains("tl-timeline-asset-playback"),
            };
        }

        public static SceneManagementFlags ParseSceneManagement(string message)
        {
            return new SceneManagementFlags
            {
                passed = message.Contains("SceneManagementProbe passed"),
                apiAccess = message.Contains("scene-api-access"),
                typeAvail = message.Contains("scene-type-avail"),
                loadByName = message.Contains("scene-load-byname"),
            };
        }

        public static PlatformDeviceFlags ParsePlatformDevice(string message)
        {
            return new PlatformDeviceFlags
            {
                passed = message.Contains("PlatformDeviceProbe passed"),
                platformDetect = message.Contains("platform-detect"),
                deviceInfo = message.Contains("device-info"),
                screenProps = message.Contains("screen-props"),
                systemInfo = message.Contains("system-info"),
                inputBasic = message.Contains("input-basic"),
            };
        }

        [Serializable]
        public sealed class CSharpUsageFlags
        {
            public bool passed;
            public bool lambda;
            public bool genericMethod;
            public bool genericType;
            public bool iteratorAndLinq;
            public bool delegateClosure;
            public bool exceptionFilter;
        }

        [Serializable]
        public sealed class FeatureCompatFlags
        {
            public bool passed;
            public bool interfaceDispatch;
            public bool structByRef;
            public bool nullableTypes;
            public bool enumOperations;
            public bool multidimArray;
            public bool eventPattern;
            public bool reflectionInvoke;
            public bool nestedGenerics;
            public bool staticGenericClass;
            public bool valueTuple;
            public bool patternMatching;
            public bool spanLikeOps;
            public bool staticExtensionMethods;
            public bool genericExtensionMethods;
            public bool extensionInterfaceDispatch;

            public bool AllPassed()
            {
                return interfaceDispatch
                    && structByRef
                    && nullableTypes
                    && enumOperations
                    && multidimArray
                    && eventPattern
                    && reflectionInvoke
                    && nestedGenerics
                    && staticGenericClass
                    && valueTuple
                    && patternMatching
                    && spanLikeOps
                    && staticExtensionMethods
                    && genericExtensionMethods
                    && extensionInterfaceDispatch;
            }
        }

        public static CSharpHybridClrParityFlags ParseCSharpHybridClrParity(string message)
        {
            return new CSharpHybridClrParityFlags
            {
                passed = message.Contains("CSharpHybridClrParityProbe passed"),
                actionFuncDelegate = message.Contains("cs-action-func-delegate"),
                multicastDelegateOrder = message.Contains("cs-multicast-delegate-order"),
                eventAddRemove = message.Contains("cs-event-add-remove"),
                inOutRefParams = message.Contains("cs-in-out-ref-params"),
                paramsArray = message.Contains("cs-params-array"),
                delegateReturnNested = message.Contains("cs-delegate-return-nested"),
                genericDelegateVariance = message.Contains("cs-generic-delegate-variance"),
                asyncAwaitChain = message.Contains("cs-async-await-chain"),
                asyncExceptionFlow = message.Contains("cs-async-exception-flow"),
            };
        }

        [Serializable]
        public sealed class UnityAssetFlags
        {
            public bool passed;
            public bool scriptableObject;
            public bool unityCallback;
            public bool coroutineIterator;
            public bool uguiApi;
            public bool sprite2DApi;
            public bool mesh3DApi;
            public bool timelineApi;
            public bool asyncTask;

            public bool AllPassed()
            {
                return scriptableObject
                    && unityCallback
                    && coroutineIterator
                    && uguiApi
                    && sprite2DApi
                    && mesh3DApi
                    && timelineApi
                    && asyncTask;
            }
        }

        [Serializable]
        public sealed class CSharpHybridClrParityFlags
        {
            public bool passed;
            public bool actionFuncDelegate;
            public bool multicastDelegateOrder;
            public bool eventAddRemove;
            public bool inOutRefParams;
            public bool paramsArray;
            public bool delegateReturnNested;
            public bool genericDelegateVariance;
            public bool asyncAwaitChain;
            public bool asyncExceptionFlow;

            public bool AllPassed()
            {
                return actionFuncDelegate
                    && multicastDelegateOrder
                    && eventAddRemove
                    && inOutRefParams
                    && paramsArray
                    && delegateReturnNested
                    && genericDelegateVariance
                    && asyncAwaitChain
                    && asyncExceptionFlow;
            }
        }

        [Serializable]
        public sealed class ReflectionComprehensiveFlags
        {
            public bool passed;
            public bool assemblyModuleType;
            public bool constructorsActivator;
            public bool fieldProperty;
            public bool methodOverload;
            public bool privateMembers;
            public bool eventDelegate;
            public bool attributes;
            public bool genericMethodType;
            public bool arrayEnumNullable;
            public bool interfaceNested;
            public bool optionalParameters;
            public bool targetInvocationException;

            public bool AllPassed()
            {
                return assemblyModuleType
                    && constructorsActivator
                    && fieldProperty
                    && methodOverload
                    && privateMembers
                    && eventDelegate
                    && attributes
                    && genericMethodType
                    && arrayEnumNullable
                    && interfaceNested
                    && optionalParameters
                    && targetInvocationException;
            }
        }

        [Serializable]
        public sealed class LinqAggregateFlags
        {
            public bool passed;
            public bool sum;
            public bool sumSelector;
            public bool average;
            public bool count;
            public bool longCount;
            public bool min;
            public bool max;
            public bool aggregate;
            public bool distinct;
            public bool union;
            public bool intersect;
            public bool except;
            public bool groupBy;
            public bool join;
            public bool zip;
            public bool orderBy;
            public bool thenBy;
            public bool selectMany;
            public bool all;
            public bool any;
            public bool contains;
            public bool single;
            public bool singleOrDefault;
            public bool elementAt;
            public bool take;
            public bool skip;
            public bool takeWhile;
            public bool skipWhile;
            public bool concat;
            public bool sequenceEqual;
            public bool toDictionary;
            public bool toLookup;

            public bool AllPassed()
            {
                return sum && sumSelector && average && count && longCount && min && max
                    && aggregate && distinct && union && intersect && except
                    && groupBy && join && zip && orderBy && thenBy && selectMany
                    && all && any && contains && single && singleOrDefault && elementAt
                    && take && skip && takeWhile && skipWhile && concat
                    && sequenceEqual && toDictionary && toLookup;
            }
        }

        [Serializable]
        public sealed class CommercialCapabilityFlags
        {
            public bool passed;
            public bool fullGenericSharing;
            public bool metadataOptimization;
            public bool standardInterpreterOptimization;
            public bool hotfix;
            public bool hotReload;
            public bool codeProtection;
            public bool accessControl;
            public bool assemblyLoadOptimization;
            public bool crashLog;
            public bool offlineInstructionOptimization;

            public bool HotUpdateSurfacePassed()
            {
                return fullGenericSharing && metadataOptimization && crashLog;
            }

            public bool BusinessCapabilitiesPassed()
            {
                return HotUpdateSurfacePassed() && PreInterpreterPassed();
            }

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

        [Serializable]
        public sealed class TimelineCustomTrackFlags
        {
            public bool passed;
            public bool graphCreate;
            public bool customBehaviour;
            public bool clipInstance;
            public bool outputBinding;
            public bool timelineAssetPlayback;

            public bool AllPassed()
            {
                return graphCreate && customBehaviour && clipInstance && outputBinding && timelineAssetPlayback;
            }
        }

        [Serializable]
        public sealed class SceneManagementFlags
        {
            public bool passed;
            public bool apiAccess;
            public bool typeAvail;
            public bool loadByName;

            public bool AllPassed()
            {
                return apiAccess && typeAvail && loadByName;
            }
        }

        [Serializable]
        public sealed class PlatformDeviceFlags
        {
            public bool passed;
            public bool platformDetect;
            public bool deviceInfo;
            public bool screenProps;
            public bool systemInfo;
            public bool inputBasic;

            public bool AllPassed()
            {
                return platformDetect && deviceInfo && screenProps && systemInfo && inputBasic;
            }
        }
    }
}
