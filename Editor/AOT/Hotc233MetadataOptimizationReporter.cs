using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Hotc233.Editor.Settings;
using UnityEditor;
using UnityEngine;

namespace Hotc233.Editor.AOT
{
    [Serializable]
    public sealed class Hotc233MetadataOptimizationAssemblyRow
    {
        public string name;
        public long baselineBytes;
        public long optimizedBytes;
        public double savingPercent;
    }

    [Serializable]
    public sealed class Hotc233MetadataOptimizationReport
    {
        public bool success;
        public long baselineTotalBytes;
        public long optimizedTotalBytes;
        public double savingPercent;
        public double loadElapsedMs;
        public long peakHeapDeltaBytes;
        public string generatedAtUtc;
        public string target;
        public Hotc233MetadataOptimizationAssemblyRow[] assemblies;
        public string message;
    }

    public static class Hotc233MetadataOptimizationReporter
    {
        public const double MinSavingPercent = 10.0;
        public const double MaxSavingPercent = 25.0;

        public static Hotc233MetadataOptimizationReport OptimizeDirectory(string sourceDir, string destinationDir, BuildTarget target)
        {
            if (!Hotc233Settings.Instance.enableMetadataOptimization)
            {
                Directory.CreateDirectory(destinationDir);
                foreach (string sourcePath in Directory.GetFiles(sourceDir, "*.dll"))
                {
                    string fileName = Path.GetFileName(sourcePath);
                    File.Copy(sourcePath, Path.Combine(destinationDir, fileName), true);
                }

                return BuildReport(Array.Empty<Hotc233MetadataOptimizationAssemblyRow>(), target, 0, 0, "metadata optimization disabled");
            }

            Directory.CreateDirectory(destinationDir);
            var rows = new List<Hotc233MetadataOptimizationAssemblyRow>();
            long baselineTotal = 0;
            long optimizedTotal = 0;

            foreach (string sourcePath in Directory.GetFiles(sourceDir, "*.dll"))
            {
                string fileName = Path.GetFileName(sourcePath);
                byte[] baselineBytes = File.ReadAllBytes(sourcePath);
                byte[] optimizedBytes = AOTAssemblyMetadataStripper.Strip(baselineBytes);
                File.WriteAllBytes(Path.Combine(destinationDir, fileName), optimizedBytes);

                long baseline = baselineBytes.LongLength;
                long optimized = optimizedBytes.LongLength;
                baselineTotal += baseline;
                optimizedTotal += optimized;
                rows.Add(new Hotc233MetadataOptimizationAssemblyRow
                {
                    name = Path.GetFileNameWithoutExtension(fileName),
                    baselineBytes = baseline,
                    optimizedBytes = optimized,
                    savingPercent = ComputeSavingPercent(baseline, optimized),
                });
            }

            return BuildReport(rows.ToArray(), target, baselineTotal, optimizedTotal, null);
        }

        public static Hotc233MetadataOptimizationReport BuildReport(
            Hotc233MetadataOptimizationAssemblyRow[] rows,
            BuildTarget target,
            long baselineTotalBytes,
            long optimizedTotalBytes,
            string messageOverride)
        {
            double savingPercent = ComputeSavingPercent(baselineTotalBytes, optimizedTotalBytes);
            bool success = rows.Length > 0
                && baselineTotalBytes > 0
                && optimizedTotalBytes > 0
                && optimizedTotalBytes < baselineTotalBytes
                && savingPercent + 0.001 >= MinSavingPercent;

            string message = messageOverride;
            if (string.IsNullOrEmpty(message))
            {
                message = success
                    ? $"metadata optimization saved {savingPercent:F1}% ({baselineTotalBytes} -> {optimizedTotalBytes} bytes)"
                    : $"metadata optimization below P0 threshold {MinSavingPercent:F1}%: {savingPercent:F1}%";
            }

            return new Hotc233MetadataOptimizationReport
            {
                success = success,
                baselineTotalBytes = baselineTotalBytes,
                optimizedTotalBytes = optimizedTotalBytes,
                savingPercent = savingPercent,
                generatedAtUtc = DateTime.UtcNow.ToString("O"),
                target = target.ToString(),
                assemblies = rows ?? Array.Empty<Hotc233MetadataOptimizationAssemblyRow>(),
                message = message,
            };
        }

        public static void WriteReport(string outputPath, Hotc233MetadataOptimizationReport report)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(outputPath) ?? ".");
            File.WriteAllText(outputPath, JsonUtility.ToJson(report, true), new UTF8Encoding(false));
        }

        public static Hotc233MetadataOptimizationReport AttachRuntimeMetrics(
            Hotc233MetadataOptimizationReport report,
            double loadElapsedMs,
            long peakHeapDeltaBytes)
        {
            report.loadElapsedMs = loadElapsedMs;
            report.peakHeapDeltaBytes = peakHeapDeltaBytes;
            if (report.success)
            {
                report.message += $"; load={loadElapsedMs:F2}ms; peakHeapDelta={peakHeapDeltaBytes}";
            }

            return report;
        }

        public static double ComputeSavingPercent(long baselineBytes, long optimizedBytes)
        {
            if (baselineBytes <= 0)
            {
                return 0;
            }

            return (baselineBytes - optimizedBytes) * 100.0 / baselineBytes;
        }
    }
}
