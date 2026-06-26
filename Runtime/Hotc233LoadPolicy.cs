using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public sealed class Hotc233LoadPolicy
    {
        public static Hotc233LoadPolicy None => new Hotc233LoadPolicy();

        public bool EnableAccessControl { get; set; }

        public bool EnableIntegrityCheck { get; set; }

        public bool EnableXorPayloadProtection { get; set; }

        public string XorKey { get; set; }

        public Func<NamedBinary, bool> AccessValidator { get; set; }

        public Func<NamedBinary, byte[]> DecryptBinary { get; set; }

        public Dictionary<string, string> ExpectedSha256ByName { get; } = new Dictionary<string, string>(StringComparer.Ordinal);

        public HashSet<string> AllowedBinaryNames { get; } = new HashSet<string>(StringComparer.Ordinal);

        public static Hotc233LoadPolicy CreateXorProtected(string key)
        {
            return new Hotc233LoadPolicy
            {
                EnableXorPayloadProtection = true,
                XorKey = key,
            };
        }

        public static Hotc233LoadPolicy AllowOnly(params string[] binaryNames)
        {
            var policy = new Hotc233LoadPolicy
            {
                EnableAccessControl = true,
            };
            if (binaryNames != null)
            {
                foreach (string name in binaryNames)
                {
                    if (!string.IsNullOrEmpty(name))
                    {
                        policy.AllowedBinaryNames.Add(name);
                    }
                }
            }

            return policy;
        }

        public Hotc233LoadPolicy RequireSha256(string binaryName, string sha256Hex)
        {
            EnableIntegrityCheck = true;
            ExpectedSha256ByName[binaryName] = sha256Hex;
            return this;
        }

        public NamedBinary Apply(NamedBinary binary)
        {
            if (binary.Bytes == null || binary.Bytes.Length == 0)
            {
                return binary;
            }

            if (EnableAccessControl)
            {
                bool allowedByName = AllowedBinaryNames.Count == 0 || AllowedBinaryNames.Contains(binary.Name);
                bool allowedByCallback = AccessValidator == null || AccessValidator(binary);
                if (!allowedByName || !allowedByCallback)
                {
                    Hotc233RuntimeDiagnostics.Error("load.access.denied", binary.Name);
                    throw new UnauthorizedAccessException("hotc233 binary access denied: " + binary.Name);
                }
            }

            byte[] bytes = binary.Bytes;
            if (DecryptBinary != null)
            {
                bytes = DecryptBinary(binary);
            }
            else if (EnableXorPayloadProtection)
            {
                bytes = Xor(bytes, XorKey);
            }

            if (EnableIntegrityCheck && ExpectedSha256ByName.TryGetValue(binary.Name, out string expected))
            {
                string actual = Hotc233RuntimeDiagnostics.Sha256Hex(bytes);
                if (!string.Equals(actual, expected, StringComparison.OrdinalIgnoreCase))
                {
                    Hotc233RuntimeDiagnostics.Error("load.integrity.failed", $"{binary.Name} expected={expected} actual={actual}");
                    throw new InvalidOperationException("hotc233 binary integrity check failed: " + binary.Name);
                }
            }

            return new NamedBinary(binary.Name, bytes);
        }

        public static byte[] Xor(byte[] bytes, string key)
        {
            if (bytes == null)
            {
                return null;
            }

            if (string.IsNullOrEmpty(key))
            {
                throw new ArgumentException("XOR key is required.", nameof(key));
            }

            byte[] keyBytes = Encoding.UTF8.GetBytes(key);
            byte[] output = new byte[bytes.Length];
            for (int i = 0; i < bytes.Length; i++)
            {
                output[i] = (byte)(bytes[i] ^ keyBytes[i % keyBytes.Length]);
            }

            return output;
        }
    }
}
