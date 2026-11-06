// ============================================================
//  7z_wimlib (C++) - disk.cpp
//  The disk tab: clear a whole disk, initialize it as GPT or MBR and
//  create the partitions of a layout (Windows, Windows + data, data).
//   - QueryDisks / QueryPartitions: Get-Disk / Get-Partition (no
//     administrator rights needed).
//   - Disk (helper, mode = disk, elevated): Clear-Disk + Initialize-Disk
//     of the Storage module, then the partitions with diskpart (the
//     scripts of Microsoft's deployment documents). The new Windows (or
//     data) partition gets a drive letter, reported as "LETTER <x>".
// ============================================================
#include "disk.h"
#include "helper.h"
#include <climits>

namespace dvt
{
    static const wchar_t* const EspGuid = L"{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}";
    static const wchar_t* const MsrGuid = L"{e3c9e316-0b5c-4db8-817d-f92df00215ae}";
    static const wchar_t* const RecoveryGuid = L"de94bba4-06d1-4d40-a16a-bfd50179d6ac";

    bool DiskPartition::IsEsp() const { return EqualsI(GptType, EspGuid) || MbrType == 0xEF; }
    bool DiskPartition::IsMsr() const { return EqualsI(GptType, MsrGuid); }

    static bool IsTrue(const wstring& s) { return EqualsI(s, L"True"); }

    static bool TryInt(const Job& job, const wchar_t* key, int& v)
    {
        wstring s;
        long long n = 0;
        if (!TryGet(job, key, s) || !TryParseLong(s, n) || n < INT_MIN || n > INT_MAX) return false;
        v = (int)n;
        return true;
    }

    // ---------- the plan ----------

    DiskPlan DiskPlan::FromJob(const Job& job)
    {
        DiskPlan p;
        wstring v;
        int n;
        p.Gpt = !TryGet(job, L"style", v) || v != L"MBR";
        wstring layout = TryGet(job, L"layout", v) ? v : L"windows";
        p.Windows = layout != L"data";
        p.Data = layout != L"windows";
        if (TryInt(job, L"sys_mb", n)) p.SysMb = n;
        p.Msr = !TryGet(job, L"msr", v) || v != L"0";
        if (TryInt(job, L"recovery_mb", n)) { p.RecoveryMb = n; p.Recovery = n > 0; }
        if (TryInt(job, L"win_pct", n)) p.WindowsPercent = n;
        if (TryGet(job, L"label", v)) p.Label = v;
        if (TryGet(job, L"data_fs", v)) p.DataFs = v;
        if (TryGet(job, L"data_label", v)) p.DataLabel = v;
        return p;
    }

    // NTFS: 32 characters; FAT32 and exFAT: 11
    static wstring CleanFor(const wstring& label, const wstring& fs, const wchar_t* fallback)
    {
        wstring l = Trim(label).empty() ? wstring(fallback) : CleanLabel(label);
        size_t max = fs == L"NTFS" ? 32 : 11;
        return l.size() > max ? TrimEnd(l.substr(0, max)) : l;
    }

    wstring DiskPlan::WindowsLabel() const { return CleanFor(Label, L"NTFS", L"Windows"); }
    wstring DiskPlan::DataLabelClean() const { return CleanFor(DataLabel, DataFs, L"Data"); }

    // the space left for the Windows and data partitions (about 1 MB at the
    // start for the alignment and, on GPT, 1 MB for the table copy at the end)
    long long DiskPlan::FreeBytes(long long diskSize) const
    {
        long long used = 2 * MiB;
        if (Windows)
        {
            used += SysMb * MiB;
            if (HasMsr()) used += MsrMb * MiB;
            if (Recovery) used += RecoveryMb * MiB;
        }
        long long usable = Gpt ? diskSize : (diskSize < MbrMax ? diskSize : MbrMax);
        return usable - used;
    }

    // the Windows partition (0 = none); whole MB when a data partition follows
    long long DiskPlan::WindowsBytes(long long diskSize) const
    {
        if (!Windows) return 0;
        long long free = FreeBytes(diskSize);
        if (!Data) return free;
        return free / 100 * WindowsPercent / MiB * MiB;
    }

    long long DiskPlan::DataBytes(long long diskSize) const
    {
        if (!Data) return 0;
        return FreeBytes(diskSize) - WindowsBytes(diskSize);
    }

    // diskpart script; the disk is already empty and initialized as GPT / MBR.
    // The Windows partition (or the only data partition) gets the letter; a
    // data partition next to Windows gets the next free one.
    wstring DiskPlan::Script(int disk, wchar_t letter, long long diskSize) const
    {
        wstring s;
        auto line = [&s](const wstring& l) { s += l + L"\r\n"; };
        wstring let(1, letter);
        line(L"select disk " + Num(disk));
        if (Windows)
        {
            if (Gpt)
            {
                line(L"create partition efi size=" + Num(SysMb));
                line(L"format quick fs=fat32 label=\"System\"");
                if (HasMsr()) line(L"create partition msr size=" + Num(MsrMb));
            }
            else
            {
                line(L"create partition primary size=" + Num(SysMb));
                line(L"format quick fs=ntfs label=\"System Reserved\"");
                line(L"active");
            }
            if (Data) line(L"create partition primary size=" + Num(WindowsBytes(diskSize) / MiB));
            else
            {
                line(L"create partition primary");
                if (Recovery) line(L"shrink minimum=" + Num(RecoveryMb));
            }
            line(L"format quick fs=ntfs label=\"" + WindowsLabel() + L"\"");
            line(L"assign letter=" + let);
            if (Recovery)
            {
                line(Data ? L"create partition primary size=" + Num(RecoveryMb) : wstring(L"create partition primary"));
                line(L"format quick fs=ntfs label=\"Recovery\"");
                if (Gpt)
                {
                    line(L"set id=\"" + wstring(RecoveryGuid) + L"\"");
                    line(L"gpt attributes=0x8000000000000001");
                }
                else line(L"set id=27");
            }
        }
        if (Data)
        {
            line(L"create partition primary");
            line(L"format quick fs=" + Lower(DataFs) + L" label=\"" + DataLabelClean() + L"\"");
            line(Windows ? wstring(L"assign") : L"assign letter=" + let);
        }
        line(L"exit");
        return s;
    }

    // ---------- disks and partitions ----------

    bool QueryDisks(std::vector<DiskInfo>& list, wstring& error)
    {
        const wchar_t* script =
            L"$ErrorActionPreference='SilentlyContinue'\n"
            L"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
            L"foreach($d in Get-Disk){ $l=@(Get-Partition -DiskNumber $d.Number | %{ ([string]$_.DriveLetter).Trim([char]0) } | ?{ $_ }) -join ','\n"
            L" '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}' -f $d.Number,$d.Size,$d.PartitionStyle,$d.BusType,$d.IsBoot,$d.IsSystem,"
            L"$d.IsOffline,$d.IsReadOnly,$l,$d.FriendlyName }";
        list.clear();
        wstring output;
        try { RunPs(script, output, error); }
        catch (const HelperError& e) { error = e.msg; return false; }
        for (const wstring& line : SplitLines(output))
        {
            std::vector<wstring> f = Split(Trim(line), L'|', 10);
            if (f.size() < 10) continue;
            DiskInfo d;
            d.Number = (int)ParseLong(f[0], -1);
            d.Size = ParseLong(f[1], 0);
            d.Style = Upper(f[2]);
            d.Bus = f[3];
            d.IsBoot = IsTrue(f[4]);
            d.IsSystem = IsTrue(f[5]);
            d.Offline = IsTrue(f[6]);
            d.ReadOnly = IsTrue(f[7]);
            d.Letters = Upper(f[8]);
            d.Name = Trim(f[9]);
            if (d.Number >= 0 && d.Size > 0) list.push_back(d);
        }
        if (list.empty()) { if (error.empty()) error = L"Get-Disk returned nothing."; return false; }
        error.clear();
        return true;
    }

    bool QueryPartitions(std::vector<DiskPartition>& list, wstring& error)
    {
        const wchar_t* script =
            L"$ErrorActionPreference='SilentlyContinue'\n"
            L"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
            L"foreach($d in Get-Disk){ foreach($p in @(Get-Partition -DiskNumber $d.Number)){ $v=Get-Volume -Partition $p\n"
            L" '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}|{10}' -f $p.DiskNumber,$p.PartitionNumber,([string]$p.DriveLetter).Trim([char]0),"
            L"$p.GptType,$p.MbrType,$p.IsActive,$p.IsSystem,$p.Size,$p.Offset,$v.FileSystem,$d.PartitionStyle } }";
        list.clear();
        wstring output;
        try { RunPs(script, output, error); }
        catch (const HelperError& e) { error = e.msg; return false; }
        for (const wstring& line : SplitLines(output))
        {
            std::vector<wstring> f = Split(Trim(line), L'|');
            if (f.size() < 11) continue;
            DiskPartition p;
            p.Disk = (int)ParseLong(f[0], -1);
            p.Number = (int)ParseLong(f[1], -1);
            p.Letter = !f[2].empty() && IsCharAlphaW(f[2][0]) ? UpperChar(f[2][0]) : 0;
            p.GptType = f[3];
            p.MbrType = (int)ParseLong(f[4], 0);
            p.Active = IsTrue(f[5]);
            p.IsSystem = IsTrue(f[6]);
            p.Size = ParseLong(f[7], 0);
            p.Offset = ParseLong(f[8], -1);
            p.FileSystem = f[9];
            p.Style = Upper(f[10]);
            if (p.Disk >= 0 && p.Number >= 0) list.push_back(p);
        }
        if (list.empty()) { if (error.empty()) error = L"Get-Partition returned nothing."; return false; }
        error.clear();
        return true;
    }

    // a disk that must not be cleared: the disk of the running Windows,
    // of 7z_wimlib, a read-only or offline disk
    wstring DiskProblem(const DiskInfo& d, const wstring& buildRoot)
    {
        if (d.IsBoot || d.IsSystem) return L"dk.err_system";
        for (const wstring& l : Split(d.Letters, L','))
        {
            if (l.empty()) continue;
            if (IsWindowsDrive(l[0])) return L"dk.err_system";
            if (Root(buildRoot) == l.substr(0, 1) + L":\\") return L"dk.err_build";
        }
        if (d.ReadOnly) return L"dk.err_readonly";
        if (d.Offline) return L"dk.err_offline";
        return wstring();
    }

    wstring PlanProblem(const DiskPlan& p, long long diskSize)
    {
        if (!p.Gpt && diskSize > DiskPlan::MbrMax) return L"dk.err_mbr_2tb";
        if (p.Windows && (p.SysMb < (p.Gpt ? 100 : 50) || p.SysMb > 4096)) return L"dk.err_sys_size";
        if (p.Recovery && (p.RecoveryMb < 300 || p.RecoveryMb > 65536)) return L"dk.err_recovery_size";
        if (p.Data && p.Windows && (p.WindowsPercent < 5 || p.WindowsPercent > 95)) return L"dk.err_percent";
        if (p.Windows && p.WindowsBytes(diskSize) < DiskPlan::WindowsMin) return L"dk.err_too_small";
        if (p.Data && p.DataBytes(diskSize) < DiskPlan::DataMin) return L"dk.err_data_small";
        if (p.Data && p.DataFs != L"NTFS" && p.DataFs != L"exFAT" && p.DataFs != L"FAT32") return L"dk.err_fs";
        if (p.Data && p.DataFs == L"FAT32" && p.DataBytes(diskSize) > DiskPlan::Fat32Max) return L"dk.err_fat32_size";
        return wstring();
    }

    // null, or the key of the reason in lang\*.ini
    wstring SystemPartitionProblem(const DiskPartition& p, const wstring& fw)
    {
        if (p.IsSystem) return L"bk.err_sys_running";
        if (p.IsMsr()) return L"bk.err_sys_msr";
        if (fw != L"BIOS" && !EqualsI(p.FileSystem, L"FAT32")) return L"bk.err_sys_fat32";
        if (fw == L"BIOS" && p.FileSystem.empty()) return L"bk.err_sys_no_fs";
        return wstring();
    }

    // ---------- helper: mode = disk ----------
    // disk, disk_size (the size the GUI saw: a different disk under the
    // same number is refused), style, layout, sys_mb, msr, recovery_mb,
    // win_pct, label, data_fs, data_label

    // diskpart /s; its output (OEM code page) goes to status.txt
    static int RunDiskpart(const wstring& scriptFile)
    {
        std::string out, err;
        int rc = RunProcess(Combine(SystemDir(), L"diskpart.exe"), L"/s \"" + scriptFile + L"\"", out, err);
        for (const wstring& l : SplitLines(FromCodePage(out + err, CP_OEMCP)))
            if (!Trim(l).empty()) Status(L"  " + TrimEnd(l));
        return rc;
    }

    int Disk(const Job& job)
    {
        int number = (int)ParseLong(Get(job, L"disk"), -1);
        long long size = ParseLong(Get(job, L"disk_size"), -1);
        DiskPlan plan = DiskPlan::FromJob(job);
        if (number < 0) return Finish(87, L"No disk.");

        Progress(-1, L"disk", true);
        std::vector<DiskInfo> disks;
        wstring err;
        if (!QueryDisks(disks, err)) return Finish(1, L"Get-Disk failed: " + err);
        const DiskInfo* d = nullptr;
        for (const DiskInfo& x : disks) if (x.Number == number) d = &x;
        if (!d) return Finish(2, L"Disk " + Num(number) + L" was not found.");
        if (d->Size != size)
            return Finish(87, L"Refused: disk " + Num(number) + L" is not the chosen disk any more (its size changed). Refresh the list.");
        wstring why = DiskProblem(*d, Get(job, L"root"));
        if (why.empty()) why = PlanProblem(plan, d->Size);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (disk " + Num(number) + L")");
        if (FileExists(cancelFile)) return Finish(76, L"Cancelled before the disk was changed.");

        wstring style = plan.Gpt ? L"GPT" : L"MBR";
        wstring n = Num(number);
        Status(L"Disk " + n + L": " + d->Name + L", " + Num(d->Size / MiB) + L" MB, " + d->Style
            + (d->Letters.empty() ? wstring() : L", volumes " + d->Letters));
        Status(L"Clearing disk " + n + L" and initializing it as " + style);
        wstring ps =
            L"$ErrorActionPreference='Stop'\n"
            L"$d=Get-Disk -Number " + n + L"\n"
            L"if($d.IsBoot -or $d.IsSystem){ throw 'Disk " + n + L" is the boot or system disk.' }\n"
            L"if($d.PartitionStyle -ne 'RAW'){ Clear-Disk -Number " + n + L" -RemoveData -RemoveOEM -Confirm:$false }\n"
            L"Initialize-Disk -Number " + n + L" -PartitionStyle " + style + L"\n"
            // Initialize-Disk may add an MSR of its own: the layout creates its own partitions
            L"@(Get-Partition -DiskNumber " + n + L" -ErrorAction SilentlyContinue) | Remove-Partition -Confirm:$false\n";
        int rc = RunPs(ps, true);
        if (rc != 0) return Finish(rc, L"Clearing / initializing disk " + n + L" failed (code " + Num(rc) + L").");

        wchar_t letter = !DriveExists(L'W') ? L'W' : FreeLetter();
        if (letter == 0) return Finish(87, L"No free drive letter for the new partition.");
        wstring scriptFile = Combine(DirName(statusFile), L"diskpart.txt");
        wstring script = plan.Script(number, letter, d->Size);
        // the labels may hold letters of the system language: OEM code page, as diskpart reads it
        if (!WriteBytes(scriptFile, ToCodePage(script, CP_OEMCP)))
            return Finish(1, L"Write " + scriptFile + L": " + ErrorMessage(GetLastError()));
        Status(L"Creating the partitions (diskpart):");
        for (const wstring& l : SplitLines(script))
            if (!Trim(l).empty()) Status(L"  > " + Trim(l));
        rc = RunDiskpart(scriptFile);
        if (rc != 0) return Finish(rc, L"diskpart failed (code " + Num(rc) + L"); disk " + n + L" may be partly partitioned.");

        // the lettered partition: Windows (NTFS), or the only data partition (its own file system)
        wstring fs = plan.Windows ? wstring(L"NTFS") : plan.DataFs;
        if (!WaitReady(letter, fs)) return Finish(21, wstring(1, letter) + L": is not ready after diskpart.");
        Status(L"LETTER " + wstring(1, letter));
        return Finish(0, L"OK disk " + n + L" (" + style + L", " + plan.Layout() + L"), " + wstring(1, letter) + L":");
    }
}
