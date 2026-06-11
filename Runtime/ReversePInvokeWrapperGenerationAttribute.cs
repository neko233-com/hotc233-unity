using System;

namespace Hotc233
{
    [AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
    public class ReversePInvokeWrapperGenerationAttribute : Attribute
    {
        public int ReserveWrapperCount { get; }

        public ReversePInvokeWrapperGenerationAttribute(int reserveWrapperCount)
        {
            ReserveWrapperCount = reserveWrapperCount;
        }
    }
}
