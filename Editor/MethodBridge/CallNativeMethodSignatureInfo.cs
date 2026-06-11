using dnlib.DotNet;

namespace Hotc233.Editor.MethodBridge
{
    public class CallNativeMethodSignatureInfo
    {
        public MethodSig MethodSig { get; set; }

        public CallingConvention? Callvention { get; set; }
    }
}
