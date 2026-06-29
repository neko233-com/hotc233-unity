using dnlib.DotNet;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Hotc233.Editor.ABI
{
    public class MethodDesc : IEquatable<MethodDesc>
    {
        public string Sig { get; private set; }

        public MethodDef MethodDef { get; set; }

        public ReturnInfo ReturnInfo { get; set; }

        public List<ParamInfo> ParamInfos { get; set; }

        public bool Native2ManagedHiddenReturn { get; set; }

        public void Init()
        {
            for(int i = 0; i < ParamInfos.Count; i++)
            {
                ParamInfos[i].Index = i;
            }
            Sig = CreateCallSigName();
        }

        public void InitNative2Managed()
        {
            for (int i = 0; i < ParamInfos.Count; i++)
            {
                ParamInfos[i].Index = i;
            }
            Sig = CreateNative2ManagedSigName();
        }

        public MethodDesc Clone()
        {
            return new MethodDesc()
            {
                MethodDef = MethodDef,
                ReturnInfo = ReturnInfo,
                ParamInfos = ParamInfos.Select(p => new ParamInfo() { Index = p.Index, Type = p.Type }).ToList(),
                Native2ManagedHiddenReturn = Native2ManagedHiddenReturn,
            };
        }

        public void TransfromSigTypes(Func<TypeInfo, bool, TypeInfo> transformer)
        {
            ReturnInfo.Type = transformer(ReturnInfo.Type, true);
            foreach(var paramType in ParamInfos)
            {
                paramType.Type = transformer(paramType.Type, false);
            }
        }

        public string CreateCallSigName()
        {
            var n = new StringBuilder();
            if (Native2ManagedHiddenReturn)
            {
                n.Append('h');
            }
            n.Append(ReturnInfo.Type.CreateSigName());
            foreach(var param in ParamInfos)
            {
                n.Append(param.Type.CreateSigName());
            }
            return n.ToString();
        }

        public string CreateInvokeSigName()
        {
            var n = new StringBuilder();
            n.Append(ReturnInfo.Type.CreateSigName());
            foreach (var param in ParamInfos)
            {
                n.Append(param.Type.CreateSigName());
            }
            return n.ToString();
        }

        public string CreateNative2ManagedSigName()
        {
            string sigName = CreateInvokeSigName();
            return Native2ManagedHiddenReturn ? $"h{sigName}" : sigName;
        }

        public override bool Equals(object obj)
        {
            return Equals((MethodDesc)obj);
        }

        public bool Equals(MethodDesc other)
        {
            return Sig == other.Sig;
        }

        public override int GetHashCode()
        {
            return Sig.GetHashCode();
        }
    }
}
