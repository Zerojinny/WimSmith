// ============================================================
//  7z_wimlib - DiskJob.cs
//  The disk tab: clear a whole disk, initialize it as GPT or MBR and
//  create the partitions of a layout (Windows, Windows + data, data).
//   - QueryDisks: the local disks (Get-Disk), read in the GUI process
//     without administrator rights.
//   - DiskProblem / PlanProblem: the checks, made by the GUI and again
//     by the helper.
//   - Disk (helper, mode = disk, elevated): Clear-Disk + Initialize-Disk
//     of the Storage module, then the partitions with diskpart (the
//     scripts of Microsoft's deployment documents: EFI / MSR / system
//     reserved / recovery types and attributes). The new Windows (or
//     data) partition gets a drive letter, reported as "LETTER <x>".
// ============================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;

namespace Dvt
{
    // a local disk (Get-Disk)
    public class DiskInfo
    {
        public int Number;
        public long Size;
        public string Name = "", Style = "", Bus = "", Letters = "";   // Letters: "E,F" ("" = none)
        public bool IsBoot, IsSystem, Offline, ReadOnly;
    }

    // what the disk tab creates
    //   Windows only : [EFI (+ MSR) | system reserved] + Windows [+ recovery]
    //   Windows+data : the same, Windows gets WindowsPercent of the free space, data the rest
    //   data only    : one partition on the whole disk
    public class DiskPlan
    {
        public const int MsrMb = 16;
        public const long Fat32Max = 32L * 1024 * 1024 * 1024;   // Windows formats FAT32 up to 32 GB
        public const long MbrMax = 2199023255552L;                // 2 TiB: the limit of an MBR disk
        public const long WindowsMin = 1073741824L;               // the Windows partition: at least 1 GB
        public const long DataMin = 16 * 1048576L;
        public const string RecoveryGuid = "de94bba4-06d1-4d40-a16a-bfd50179d6ac";

        public bool Gpt = true, Windows = true, Data, Msr = true, Recovery;
        public int SysMb = 260, RecoveryMb = 1024, WindowsPercent = 50;
        public string Label = "Windows", DataFs = "NTFS", DataLabel = "Data";

        public static int DefaultSysMb(bool gpt) { return gpt ? 260 : 500; }

        // "windows" | "windows_data" | "data"
        public string Layout { get { return !Windows ? "data" : Data ? "windows_data" : "windows"; } }
        public bool HasMsr { get { return Gpt && Windows && Msr; } }

        public void ToJob(Dictionary<string, string> job)
        {
            job["style"] = Gpt ? "GPT" : "MBR";
            job["layout"] = Layout;
            job["sys_mb"] = SysMb.ToString(CultureInfo.InvariantCulture);
            job["msr"] = Msr ? "1" : "0";
            job["recovery_mb"] = Recovery ? RecoveryMb.ToString(CultureInfo.InvariantCulture) : "0";
            job["win_pct"] = WindowsPercent.ToString(CultureInfo.InvariantCulture);
            job["label"] = Label;
            job["data_fs"] = DataFs;
            job["data_label"] = DataLabel;
        }

        public static DiskPlan FromJob(Dictionary<string, string> job)
        {
            DiskPlan p = new DiskPlan();
            string v;
            int n;
            p.Gpt = !job.TryGetValue("style", out v) || v != "MBR";
            string layout = job.TryGetValue("layout", out v) ? v : "windows";
            p.Windows = layout != "data";
            p.Data = layout != "windows";
            if (job.TryGetValue("sys_mb", out v) && int.TryParse(v, out n)) p.SysMb = n;
            p.Msr = !job.TryGetValue("msr", out v) || v != "0";
            if (job.TryGetValue("recovery_mb", out v) && int.TryParse(v, out n)) { p.RecoveryMb = n; p.Recovery = n > 0; }
            if (job.TryGetValue("win_pct", out v) && int.TryParse(v, out n)) p.WindowsPercent = n;
            if (job.TryGetValue("label", out v)) p.Label = v;
            if (job.TryGetValue("data_fs", out v)) p.DataFs = v;
            if (job.TryGetValue("data_label", out v)) p.DataLabel = v;
            return p;
        }

        // NTFS: 32 characters; FAT32 and exFAT: 11
        static string Clean(string label, string fs, string fallback)
        {
            string l = label.Trim().Length == 0 ? fallback : WimLib.CleanLabel(label);
            int max = fs == "NTFS" ? 32 : 11;
            return l.Length > max ? l.Substring(0, max).TrimEnd() : l;
        }
        public string WindowsLabel() { return Clean(Label, "NTFS", "Windows"); }
        public string DataLabelClean() { return Clean(DataLabel, DataFs, "Data"); }

        // the space left for the Windows and data partitions (about 1 MB at the
        // start for the alignment and, on GPT, 1 MB for the table copy at the end)
        public long FreeBytes(long diskSize)
        {
            long used = 2 * 1048576L;
            if (Windows)
            {
                used += SysMb * 1048576L;
                if (HasMsr) used += MsrMb * 1048576L;
                if (Recovery) used += RecoveryMb * 1048576L;
            }
            long usable = Gpt ? diskSize : Math.Min(diskSize, MbrMax);
            return usable - used;
        }

        // the Windows partition (0 = none); whole MB when a data partition follows
        public long WindowsBytes(long diskSize)
        {
            if (!Windows) return 0;
            long free = FreeBytes(diskSize);
            if (!Data) return free;
            return free / 100 * WindowsPercent / 1048576L * 1048576L;
        }

        // the data partition (0 = none): the rest
        public long DataBytes(long diskSize)
        {
            if (!Data) return 0;
            return FreeBytes(diskSize) - WindowsBytes(diskSize);
        }

        // diskpart script for the partitions; the disk is already empty and
        // initialized as GPT / MBR. The Windows partition (or the only data
        // partition) gets the letter; a data partition next to Windows gets
        // the next free one.
        public string Script(int disk, char letter, long diskSize)
        {
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("select disk " + disk);
            if (Windows)
            {
                if (Gpt)
                {
                    sb.AppendLine("create partition efi size=" + SysMb);
                    sb.AppendLine("format quick fs=fat32 label=\"System\"");
                    if (HasMsr) sb.AppendLine("create partition msr size=" + MsrMb);
                }
                else
                {
                    sb.AppendLine("create partition primary size=" + SysMb);
                    sb.AppendLine("format quick fs=ntfs label=\"System Reserved\"");
                    sb.AppendLine("active");
                }
                if (Data) sb.AppendLine("create partition primary size=" + WindowsBytes(diskSize) / 1048576L);
                else
                {
                    sb.AppendLine("create partition primary");
                    if (Recovery) sb.AppendLine("shrink minimum=" + RecoveryMb);
                }
                sb.AppendLine("format quick fs=ntfs label=\"" + WindowsLabel() + "\"");
                sb.AppendLine("assign letter=" + letter);
                if (Recovery)
                {
                    sb.AppendLine(Data ? "create partition primary size=" + RecoveryMb : "create partition primary");
                    sb.AppendLine("format quick fs=ntfs label=\"Recovery\"");
                    if (Gpt)
                    {
                        sb.AppendLine("set id=\"" + RecoveryGuid + "\"");
                        sb.AppendLine("gpt attributes=0x8000000000000001");
                    }
                    else sb.AppendLine("set id=27");
                }
            }
            if (Data)
            {
                sb.AppendLine("create partition primary");
                sb.AppendLine("format quick fs=" + DataFs.ToLowerInvariant() + " label=\"" + DataLabelClean() + "\"");
                sb.AppendLine(Windows ? "assign" : "assign letter=" + letter);
            }
            sb.AppendLine("exit");
            return sb.ToString();
        }
    }

    public static partial class WimLib
    {
        // ---------- disks (GUI and helper) ----------

        public static List<DiskInfo> QueryDisks(out string error)
        {
            const string script =
                "$ErrorActionPreference='SilentlyContinue'\n" +
                "[Console]::OutputEncoding=[Text.Encoding]::UTF8\n" +
                "foreach($d in Get-Disk){ $l=@(Get-Partition -DiskNumber $d.Number | %{ ([string]$_.DriveLetter).Trim([char]0) } | ?{ $_ }) -join ','\n" +
                " '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}' -f $d.Number,$d.Size,$d.PartitionStyle,$d.BusType,$d.IsBoot,$d.IsSystem," +
                "$d.IsOffline,$d.IsReadOnly,$l,$d.FriendlyName }";
            string output;
            try { RunPs(script, out output, out error); }
            catch (Exception ex) { error = ex.Message; return null; }
            List<DiskInfo> list = new List<DiskInfo>();
            foreach (string line in output.Replace("\r\n", "\n").Split('\n'))
            {
                string[] f = line.Trim().Split(new[] { '|' }, 10);
                if (f.Length < 10) continue;
                DiskInfo d = new DiskInfo();
                d.Number = (int)ParseLong(f[0], -1);
                d.Size = ParseLong(f[1], 0);
                d.Style = f[2].ToUpperInvariant();
                d.Bus = f[3];
                d.IsBoot = IsTrue(f[4]);
                d.IsSystem = IsTrue(f[5]);
                d.Offline = IsTrue(f[6]);
                d.ReadOnly = IsTrue(f[7]);
                d.Letters = f[8].ToUpperInvariant();
                d.Name = f[9].Trim();
                if (d.Number >= 0 && d.Size > 0) list.Add(d);
            }
            if (list.Count == 0) { if (error.Length == 0) error = "Get-Disk returned nothing."; return null; }
            error = null;
            return list;
        }

        static bool IsTrue(string s) { return string.Equals(s, "True", StringComparison.OrdinalIgnoreCase); }

        // a disk that must not be cleared: the disk of the running Windows,
        // of 7z_wimlib, a read-only or offline disk
        public static string DiskProblem(DiskInfo d, string buildRoot)
        {
            if (d.IsBoot || d.IsSystem) return "dk.err_system";
            foreach (string l in d.Letters.Split(','))
            {
                if (l.Length == 0) continue;
                if (IsWindowsDrive(l[0])) return "dk.err_system";
                if (Root(buildRoot) == l.Substring(0, 1) + ":\\") return "dk.err_build";
            }
            if (d.ReadOnly) return "dk.err_readonly";
            if (d.Offline) return "dk.err_offline";
            return null;
        }

        public static string PlanProblem(DiskPlan p, long diskSize)
        {
            if (!p.Gpt && diskSize > DiskPlan.MbrMax) return "dk.err_mbr_2tb";
            if (p.Windows && (p.SysMb < (p.Gpt ? 100 : 50) || p.SysMb > 4096)) return "dk.err_sys_size";
            if (p.Recovery && (p.RecoveryMb < 300 || p.RecoveryMb > 65536)) return "dk.err_recovery_size";
            if (p.Data && p.Windows && (p.WindowsPercent < 5 || p.WindowsPercent > 95)) return "dk.err_percent";
            if (p.Windows && p.WindowsBytes(diskSize) < DiskPlan.WindowsMin) return "dk.err_too_small";
            if (p.Data && p.DataBytes(diskSize) < DiskPlan.DataMin) return "dk.err_data_small";
            if (p.Data && p.DataFs != "NTFS" && p.DataFs != "exFAT" && p.DataFs != "FAT32") return "dk.err_fs";
            if (p.Data && p.DataFs == "FAT32" && p.DataBytes(diskSize) > DiskPlan.Fat32Max) return "dk.err_fat32_size";
            return null;
        }

        // ---------- helper: mode = disk ----------
        // disk, disk_size (the size the GUI saw: a different disk under the
        // same number is refused), style, layout, sys_mb, recovery_mb, fs, label

        static int Disk(Dictionary<string, string> job)
        {
            int number = (int)ParseLong(Get(job, "disk"), -1);
            long size = ParseLong(Get(job, "disk_size"), -1);
            DiskPlan plan = DiskPlan.FromJob(job);
            if (number < 0) return Finish(87, "No disk.");

            Progress(-1, "disk", true);
            string err;
            List<DiskInfo> disks = QueryDisks(out err);
            if (disks == null) return Finish(1, "Get-Disk failed: " + err);
            DiskInfo d = null;
            foreach (DiskInfo x in disks) if (x.Number == number) d = x;
            if (d == null) return Finish(2, "Disk " + number + " was not found.");
            if (d.Size != size) return Finish(87, "Refused: disk " + number + " is not the chosen disk any more (its size changed). Refresh the list.");
            string why = DiskProblem(d, Get(job, "root"));
            if (why == null) why = PlanProblem(plan, d.Size);
            if (why != null) return Finish(87, "Refused: " + why + " (disk " + number + ")");
            if (File.Exists(cancelFile)) return Finish(ErrAborted, "Cancelled before the disk was changed.");

            string style = plan.Gpt ? "GPT" : "MBR";
            Status("Disk " + number + ": " + d.Name + ", " + (d.Size / MiB) + " MB, " + d.Style
                + (d.Letters.Length > 0 ? ", volumes " + d.Letters : ""));
            Status("Clearing disk " + number + " and initializing it as " + style);
            string ps =
                "$ErrorActionPreference='Stop'\n" +
                "$d=Get-Disk -Number " + number + "\n" +
                "if($d.IsBoot -or $d.IsSystem){ throw 'Disk " + number + " is the boot or system disk.' }\n" +
                "if($d.PartitionStyle -ne 'RAW'){ Clear-Disk -Number " + number + " -RemoveData -RemoveOEM -Confirm:$false }\n" +
                "Initialize-Disk -Number " + number + " -PartitionStyle " + style + "\n" +
                // Initialize-Disk may add an MSR of its own: the layout creates its own partitions
                "@(Get-Partition -DiskNumber " + number + " -ErrorAction SilentlyContinue) | Remove-Partition -Confirm:$false\n";
            int rc = RunPs(ps, true);
            if (rc != 0) return Finish(rc, "Clearing / initializing disk " + number + " failed (code " + rc + ").");

            char letter = !DriveExists('W') ? 'W' : FreeLetter();
            if (letter == '\0') return Finish(87, "No free drive letter for the new partition.");
            string scriptFile = Path.Combine(Path.GetDirectoryName(statusFile), "diskpart.txt");
            string script = plan.Script(number, letter, d.Size);
            File.WriteAllText(scriptFile, script, Encoding.ASCII);
            Status("Creating the partitions (diskpart):");
            foreach (string l in script.Replace("\r\n", "\n").Split('\n'))
                if (l.Trim().Length > 0) Status("  > " + l.Trim());
            rc = RunDiskpart(scriptFile);
            if (rc != 0) return Finish(rc, "diskpart failed (code " + rc + "); disk " + number + " may be partly partitioned.");

            if (WaitReady(letter) == null) return Finish(21, letter + ": is not ready after diskpart.");
            Status("LETTER " + letter);
            return Finish(0, "OK disk " + number + " (" + style + ", " + plan.Layout + "), " + letter + ":");
        }

        static bool DriveExists(char letter)
        {
            foreach (DriveInfo d in DriveInfo.GetDrives())
                if (char.ToUpperInvariant(d.Name[0]) == letter) return true;
            return false;
        }

        // diskpart /s; its output (OEM code page) goes to status.txt
        static int RunDiskpart(string scriptFile)
        {
            ProcessStartInfo psi = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "diskpart.exe"), "/s \"" + scriptFile + "\"");
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardError = true;
            psi.RedirectStandardOutput = true;
            Encoding oem;
            try { oem = Encoding.GetEncoding(CultureInfo.CurrentCulture.TextInfo.OEMCodePage); }
            catch (Exception) { oem = Encoding.Default; }
            psi.StandardOutputEncoding = psi.StandardErrorEncoding = oem;
            using (Process p = Process.Start(psi))
            {
                System.Threading.Tasks.Task<string> err = p.StandardError.ReadToEndAsync();
                string output = p.StandardOutput.ReadToEnd();
                p.WaitForExit();
                foreach (string l in (output + err.Result).Replace("\r\n", "\n").Split('\n'))
                    if (l.Trim().Length > 0) Status("  " + l.TrimEnd());
                return p.ExitCode;
            }
        }
    }
}
