using UnityEngine;
using UnityEngine.Playables;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    internal static class Hotc233UnityAotGenericPreserves
    {
        private static bool s_neverExecute;

        [Preserve]
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void PreserveUnityGenericMethods()
        {
            if (!s_neverExecute)
            {
                return;
            }

            ScriptPlayableOutput output = default;
            _ = PlayableOutputExtensions.IsOutputValid<ScriptPlayableOutput>(output);
        }
    }
}
