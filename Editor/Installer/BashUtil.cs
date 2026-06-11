using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;

namespace Hotc233.Editor.Installer
{
    public static class BashUtil
    {
        public static int RunCommand(string workingDir, string program, string[] args, bool log = true)
        {
            using (Process p = new Process())
            {
                p.StartInfo.WorkingDirectory = workingDir;
                p.StartInfo.FileName = program;
                p.StartInfo.UseShellExecute = false;
                p.StartInfo.CreateNoWindow = true;
                string argsStr = string.Join(" ", args.Select(arg => "\"" + arg + "\""));
                p.StartInfo.Arguments = argsStr;
                if (log)
                {
                    UnityEngine.Debug.Log($"[BashUtil] run => {program} {argsStr}");
                }
                p.Start();
                p.WaitForExit();
                return p.ExitCode;
            }
        }

        public static (int ExitCode, string StdOut, string StdErr) RunCommand2(string workingDir, string program, string[] args, bool log = true)
        {
            using (Process p = new Process())
            {
                p.StartInfo.WorkingDirectory = workingDir;
                p.StartInfo.FileName = program;
                p.StartInfo.UseShellExecute = false;
                p.StartInfo.CreateNoWindow = true;
                p.StartInfo.RedirectStandardOutput = true;
                p.StartInfo.RedirectStandardError = true;
                string argsStr = string.Join(" ", args);
                p.StartInfo.Arguments = argsStr;
                if (log)
                {
                    UnityEngine.Debug.Log($"[BashUtil] run => {program} {argsStr}");
                }
                p.Start();
                p.WaitForExit();

                string stdOut = p.StandardOutput.ReadToEnd();
                string stdErr = p.StandardError.ReadToEnd();
                return (p.ExitCode, stdOut, stdErr);
            }
        }

        public static void RemoveDir(string dir, bool log = false)
        {
            if (log)
            {
                UnityEngine.Debug.Log($"[BashUtil] RemoveDir dir:{dir}");
            }

            int maxTryCount = 5;
            for (int i = 0; i < maxTryCount; ++i)
            {
                try
                {
                    if (!Directory.Exists(dir))
                    {
                        return;
                    }
                    foreach (var file in Directory.GetFiles(dir))
                    {
                        File.SetAttributes(file, FileAttributes.Normal);
                        File.Delete(file);
                    }
                    foreach (var subDir in Directory.GetDirectories(dir))
                    {
                        RemoveDir(subDir);
                    }
                    Directory.Delete(dir, true);
                    break;
                }
                catch (Exception e)
                {
                    UnityEngine.Debug.LogError($"[BashUtil] RemoveDir:{dir} with exception:{e}. try count:{i}");
                    Thread.Sleep(100);
                }
            }
        }

        public static void RecreateDir(string dir)
        {
            if (Directory.Exists(dir))
            {
                RemoveDir(dir, true);
            }
            Directory.CreateDirectory(dir);
        }

        public static void CopyDir(string src, string dst, bool log = false)
        {
            if (log)
            {
                UnityEngine.Debug.Log($"[BashUtil] CopyDir {src} => {dst}");
            }
            if (Directory.Exists(dst))
            {
                RemoveDir(dst);
            }
            else
            {
                string parentDir = Path.GetDirectoryName(Path.GetFullPath(dst));
                Directory.CreateDirectory(parentDir);
            }

            UnityEditor.FileUtil.CopyFileOrDirectory(src, dst);
        }

        public static bool SyncDirIncremental(string src, string dst, bool log = false)
        {
            if (!Directory.Exists(src))
            {
                throw new DirectoryNotFoundException($"source directory not found: {src}");
            }

            if (log)
            {
                UnityEngine.Debug.Log($"[BashUtil] SyncDirIncremental {src} => {dst}");
            }

            bool changed = false;
            Directory.CreateDirectory(dst);
            RemoveDeletedEntries(src, dst, ref changed, log);
            CopyChangedEntries(src, dst, ref changed, log);
            return changed;
        }

        private static void RemoveDeletedEntries(string src, string dst, ref bool changed, bool log)
        {
            foreach (string dstFile in Directory.GetFiles(dst))
            {
                string fileName = Path.GetFileName(dstFile);
                string srcFile = Path.Combine(src, fileName);
                if (File.Exists(srcFile))
                {
                    continue;
                }

                File.SetAttributes(dstFile, FileAttributes.Normal);
                File.Delete(dstFile);
                changed = true;
                if (log)
                {
                    UnityEngine.Debug.Log($"[BashUtil] delete file {dstFile}");
                }
            }

            foreach (string dstDir in Directory.GetDirectories(dst))
            {
                string dirName = Path.GetFileName(dstDir);
                string srcDir = Path.Combine(src, dirName);
                if (Directory.Exists(srcDir))
                {
                    RemoveDeletedEntries(srcDir, dstDir, ref changed, log);
                    continue;
                }

                RemoveDir(dstDir, log);
                changed = true;
            }
        }

        private static void CopyChangedEntries(string src, string dst, ref bool changed, bool log)
        {
            foreach (string srcDir in Directory.GetDirectories(src))
            {
                string dirName = Path.GetFileName(srcDir);
                string dstDir = Path.Combine(dst, dirName);
                if (!Directory.Exists(dstDir))
                {
                    Directory.CreateDirectory(dstDir);
                    changed = true;
                    if (log)
                    {
                        UnityEngine.Debug.Log($"[BashUtil] create dir {dstDir}");
                    }
                }

                CopyChangedEntries(srcDir, dstDir, ref changed, log);
            }

            foreach (string srcFile in Directory.GetFiles(src))
            {
                string fileName = Path.GetFileName(srcFile);
                string dstFile = Path.Combine(dst, fileName);
                if (!NeedCopy(srcFile, dstFile))
                {
                    continue;
                }

                File.SetAttributes(srcFile, FileAttributes.Normal);
                if (File.Exists(dstFile))
                {
                    File.SetAttributes(dstFile, FileAttributes.Normal);
                }
                File.Copy(srcFile, dstFile, true);
                File.SetLastWriteTimeUtc(dstFile, File.GetLastWriteTimeUtc(srcFile));
                changed = true;
                if (log)
                {
                    UnityEngine.Debug.Log($"[BashUtil] copy file {srcFile} => {dstFile}");
                }
            }
        }

        private static bool NeedCopy(string srcFile, string dstFile)
        {
            if (!File.Exists(dstFile))
            {
                return true;
            }

            var srcInfo = new FileInfo(srcFile);
            var dstInfo = new FileInfo(dstFile);
            if (srcInfo.Length != dstInfo.Length)
            {
                return true;
            }

            return srcInfo.LastWriteTimeUtc != dstInfo.LastWriteTimeUtc;
        }
    }
}
