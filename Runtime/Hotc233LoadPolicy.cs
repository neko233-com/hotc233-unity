using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public interface IHotc233PayloadProtector
    {
        byte[] Protect(NamedBinary binary);

        byte[] Unprotect(NamedBinary binary);
    }

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

        public IHotc233PayloadProtector PayloadProtector { get; set; }

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

        public static Hotc233LoadPolicy CreateAesCbcHmacProtected(byte[] encryptionKey, byte[] authenticationKey)
        {
            return new Hotc233LoadPolicy
            {
                PayloadProtector = new AesCbcHmacPayloadProtector(encryptionKey, authenticationKey),
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

        public NamedBinary Protect(NamedBinary binary)
        {
            if (binary.Bytes == null || binary.Bytes.Length == 0)
            {
                return binary;
            }

            byte[] bytes;
            if (PayloadProtector != null)
            {
                bytes = PayloadProtector.Protect(binary);
            }
            else if (EnableXorPayloadProtection)
            {
                bytes = Xor(binary.Bytes, XorKey);
            }
            else
            {
                bytes = (byte[])binary.Bytes.Clone();
            }

            return new NamedBinary(binary.Name, bytes);
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
            if (PayloadProtector != null)
            {
                bytes = PayloadProtector.Unprotect(binary);
            }
            else if (DecryptBinary != null)
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

        [Preserve]
        public sealed class AesCbcHmacPayloadProtector : IHotc233PayloadProtector
        {
            private const int MagicLength = 8;
            private const int VersionOffset = MagicLength;
            private const int IvLengthOffset = VersionOffset + 1;
            private const int CipherLengthOffset = IvLengthOffset + 1;
            private const int HeaderLength = CipherLengthOffset + 4;
            private const int TagLength = 32;
            private static readonly byte[] Magic = Encoding.ASCII.GetBytes("H233P001");
            private readonly byte[] encryptionKey;
            private readonly byte[] authenticationKey;

            public AesCbcHmacPayloadProtector(byte[] encryptionKey, byte[] authenticationKey)
            {
                ValidateAesKey(encryptionKey, nameof(encryptionKey));
                if (authenticationKey == null || authenticationKey.Length < TagLength)
                {
                    throw new ArgumentException("HMAC-SHA256 authentication key must be at least 32 bytes.", nameof(authenticationKey));
                }

                this.encryptionKey = (byte[])encryptionKey.Clone();
                this.authenticationKey = (byte[])authenticationKey.Clone();
            }

            public byte[] Protect(NamedBinary binary)
            {
                if (binary.Bytes == null)
                {
                    return null;
                }

                byte[] iv = new byte[16];
                using (var rng = RandomNumberGenerator.Create())
                {
                    rng.GetBytes(iv);
                }

                byte[] cipherText;
                using (var aes = Aes.Create())
                {
                    aes.Mode = CipherMode.CBC;
                    aes.Padding = PaddingMode.PKCS7;
                    aes.Key = encryptionKey;
                    aes.IV = iv;
                    using (var encryptor = aes.CreateEncryptor())
                    {
                        cipherText = encryptor.TransformFinalBlock(binary.Bytes, 0, binary.Bytes.Length);
                    }
                }

                byte[] envelopeWithoutTag = BuildEnvelopeWithoutTag(iv, cipherText);
                byte[] tag = ComputeTag(binary.Name, envelopeWithoutTag);
                byte[] envelope = new byte[envelopeWithoutTag.Length + tag.Length];
                Buffer.BlockCopy(envelopeWithoutTag, 0, envelope, 0, envelopeWithoutTag.Length);
                Buffer.BlockCopy(tag, 0, envelope, envelopeWithoutTag.Length, tag.Length);
                return envelope;
            }

            public byte[] Unprotect(NamedBinary binary)
            {
                byte[] envelope = binary.Bytes;
                if (envelope == null)
                {
                    return null;
                }

                if (envelope.Length < HeaderLength + TagLength || !HasMagic(envelope))
                {
                    throw new InvalidOperationException("hotc233 protected payload has an invalid header: " + binary.Name);
                }

                byte version = envelope[VersionOffset];
                int ivLength = envelope[IvLengthOffset];
                int cipherLength = ReadInt32BigEndian(envelope, CipherLengthOffset);
                if (version != 1 || ivLength != 16 || cipherLength <= 0)
                {
                    throw new InvalidOperationException("hotc233 protected payload has unsupported metadata: " + binary.Name);
                }

                int expectedLength = HeaderLength + ivLength + cipherLength + TagLength;
                if (envelope.Length != expectedLength)
                {
                    throw new InvalidOperationException("hotc233 protected payload length mismatch: " + binary.Name);
                }

                int tagOffset = envelope.Length - TagLength;
                byte[] envelopeWithoutTag = new byte[tagOffset];
                Buffer.BlockCopy(envelope, 0, envelopeWithoutTag, 0, tagOffset);
                byte[] expectedTag = ComputeTag(binary.Name, envelopeWithoutTag);
                if (!FixedTimeEquals(envelope, tagOffset, expectedTag))
                {
                    Hotc233RuntimeDiagnostics.Error("load.protection.auth.failed", binary.Name);
                    throw new InvalidOperationException("hotc233 protected payload authentication failed: " + binary.Name);
                }

                byte[] iv = new byte[ivLength];
                Buffer.BlockCopy(envelope, HeaderLength, iv, 0, ivLength);
                byte[] cipherText = new byte[cipherLength];
                Buffer.BlockCopy(envelope, HeaderLength + ivLength, cipherText, 0, cipherLength);

                using (var aes = Aes.Create())
                {
                    aes.Mode = CipherMode.CBC;
                    aes.Padding = PaddingMode.PKCS7;
                    aes.Key = encryptionKey;
                    aes.IV = iv;
                    using (var decryptor = aes.CreateDecryptor())
                    {
                        return decryptor.TransformFinalBlock(cipherText, 0, cipherText.Length);
                    }
                }
            }

            private static void ValidateAesKey(byte[] key, string parameterName)
            {
                if (key == null || (key.Length != 16 && key.Length != 24 && key.Length != 32))
                {
                    throw new ArgumentException("AES key must be 16, 24, or 32 bytes.", parameterName);
                }
            }

            private byte[] BuildEnvelopeWithoutTag(byte[] iv, byte[] cipherText)
            {
                byte[] envelope = new byte[HeaderLength + iv.Length + cipherText.Length];
                Buffer.BlockCopy(Magic, 0, envelope, 0, MagicLength);
                envelope[VersionOffset] = 1;
                envelope[IvLengthOffset] = (byte)iv.Length;
                WriteInt32BigEndian(envelope, CipherLengthOffset, cipherText.Length);
                Buffer.BlockCopy(iv, 0, envelope, HeaderLength, iv.Length);
                Buffer.BlockCopy(cipherText, 0, envelope, HeaderLength + iv.Length, cipherText.Length);
                return envelope;
            }

            private byte[] ComputeTag(string binaryName, byte[] envelopeWithoutTag)
            {
                using (var hmac = new HMACSHA256(authenticationKey))
                using (var stream = new MemoryStream())
                {
                    byte[] nameBytes = Encoding.UTF8.GetBytes(binaryName ?? string.Empty);
                    stream.Write(nameBytes, 0, nameBytes.Length);
                    stream.WriteByte(0);
                    stream.Write(envelopeWithoutTag, 0, envelopeWithoutTag.Length);
                    return hmac.ComputeHash(stream.ToArray());
                }
            }

            private static bool HasMagic(byte[] bytes)
            {
                for (int i = 0; i < Magic.Length; i++)
                {
                    if (bytes[i] != Magic[i])
                    {
                        return false;
                    }
                }

                return true;
            }

            private static bool FixedTimeEquals(byte[] left, int leftOffset, byte[] right)
            {
                if (left == null || right == null || leftOffset < 0 || left.Length - leftOffset < right.Length)
                {
                    return false;
                }

                int diff = 0;
                for (int i = 0; i < right.Length; i++)
                {
                    diff |= left[leftOffset + i] ^ right[i];
                }

                return diff == 0;
            }

            private static int ReadInt32BigEndian(byte[] bytes, int offset)
            {
                return (bytes[offset] << 24)
                    | (bytes[offset + 1] << 16)
                    | (bytes[offset + 2] << 8)
                    | bytes[offset + 3];
            }

            private static void WriteInt32BigEndian(byte[] bytes, int offset, int value)
            {
                bytes[offset] = (byte)((value >> 24) & 0xff);
                bytes[offset + 1] = (byte)((value >> 16) & 0xff);
                bytes[offset + 2] = (byte)((value >> 8) & 0xff);
                bytes[offset + 3] = (byte)(value & 0xff);
            }
        }
    }
}
