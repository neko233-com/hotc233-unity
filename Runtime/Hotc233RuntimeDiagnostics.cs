using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using UnityEngine;
using UnityEngine.Scripting;

namespace Hotc233
{
    [Preserve]
    public static class Hotc233RuntimeDiagnostics
    {
        private const int MaxEvents = 256;
        private static readonly List<string> events = new List<string>();

        public static string SessionId { get; private set; } = "not-started";

        public static IReadOnlyList<string> Events => events;

        public static void BeginSession(string context)
        {
            SessionId = DateTime.UtcNow.ToString("yyyyMMddHHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            events.Clear();
            Info("session.begin", context);
            Info("environment", BuildEnvironmentLine());
        }

        public static void Info(string code, string message)
        {
            Add("INFO", code, message);
        }

        public static void Warning(string code, string message)
        {
            Add("WARN", code, message);
        }

        public static void Error(string code, string message)
        {
            Add("ERROR", code, message);
        }

        public static string DescribeBinary(string name, byte[] bytes)
        {
            if (bytes == null)
            {
                return $"{name} bytes=null";
            }

            return $"{name} bytes={bytes.Length} sha256={Sha256Short(bytes)}";
        }

        public static string DescribeException(Exception exception, string stage = null)
        {
            var sb = new StringBuilder();
            if (!string.IsNullOrEmpty(stage))
            {
                sb.Append("stage=").Append(stage).Append("; ");
            }

            sb.Append("session=").Append(SessionId).AppendLine();
            int depth = 0;
            for (var current = exception; current != null; current = current.InnerException)
            {
                sb.Append('[').Append(depth).Append("] ")
                    .Append(current.GetType().FullName)
                    .Append(": ")
                    .AppendLine(current.Message);
                sb.AppendLine(current.StackTrace);
                depth++;
            }

            if (events.Count > 0)
            {
                sb.AppendLine("recent-hotc233-events:");
                foreach (string item in events)
                {
                    sb.AppendLine(item);
                }
            }

            return sb.ToString();
        }

        public static string BuildEnvironmentLine()
        {
            return $"unity={Application.unityVersion}; platform={Application.platform}; device={SystemInfo.deviceModel}; os={SystemInfo.operatingSystem}; graphics={SystemInfo.graphicsDeviceName}; memoryMB={SystemInfo.systemMemorySize}; processor={SystemInfo.processorType}";
        }

        private static void Add(string level, string code, string message)
        {
            string line = $"[hotc233][{level}][{SessionId}][{code}] {message}";
            events.Add(line);
            if (events.Count > MaxEvents)
            {
                events.RemoveAt(0);
            }

            if (level == "ERROR")
            {
                Debug.LogError(line);
            }
            else if (level == "WARN")
            {
                Debug.LogWarning(line);
            }
            else
            {
                Debug.Log(line);
            }
        }

        private static string Sha256Short(byte[] bytes)
        {
            using (var sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(bytes);
                var sb = new StringBuilder(16);
                for (int i = 0; i < 8 && i < hash.Length; i++)
                {
                    sb.Append(hash[i].ToString("x2"));
                }

                return sb.ToString();
            }
        }
    }
}
