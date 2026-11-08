// ============================================================
//  WimSmith - disk.cpp
//  The disk tab: clear a whole disk, initialize it as GPT or MBR and
//  create the partitions of a layout (Windows, Windows + data, data).
//   - QueryDisks / QueryPartitions: the IOCTLs of storage.cpp (no
//     administrator rights, no PowerShell).
//   - Disk (helper, mode = disk, elevated): the disk is cleared by
//     storage::ClearAndInitialize (as diskpart "clean"; GPT: also
//     initialized), then diskpart makes the partitions ("convert mbr"
//     first on MBR: it writes the MBR boot code BIOS needs; the scripts
//     of Microsoft's deployment documents). The new Windows (or data)
//     partition gets a drive letter, reported as "LETTER <x>".
// ============================================================
#include "disk.h"
#include "helper.h"
#include "storage.h"
#include <algorithm>
#include <climits>

namespace wimsmith
{
    static const wchar_t* const EspGuid = L"{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}";
    static const wchar_t* const MsrGuid = L"{e3c9e316-0b5c-4db8-817d-f92df00215ae}";
    static const wchar_t* const RecoveryGuid = L"de94bba4-06d1-4d40-a16a-bfd50179d6ac";

    bool DiskPartition::IsEsp() const { return EqualsI(GptType, EspGuid) || MbrType == 0xEF; }
    bool DiskPartition::IsMsr() const { return EqualsI(GptType, MsrGuid); }

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
        p.Reserved = !TryGet(job, L"reserved", v) || v != L"0";
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
            if (HasSys()) used += SysMb * MiB;
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

    // diskpart script; the disk is already empty (GPT: initialized, MBR: RAW).
    // The Windows partition (or the only data partition) gets the letter; a
    // data partition next to Windows gets the next free one.
    wstring DiskPlan::Script(int disk, wchar_t letter, long long diskSize) const
    {
        wstring s;
        auto line = [&s](const wstring& l) { s += l + L"\r\n"; };
        wstring let(1, letter);
        // MBR: the disk was left RAW (cleared just before: rescan first);
        // "convert mbr" initializes it and writes the MBR boot code BIOS needs
        if (!Gpt) line(L"rescan");
        line(L"select disk " + Num(disk));
        if (!Gpt) line(L"convert mbr");
        if (Windows)
        {
            if (Gpt)
            {
                line(L"create partition efi size=" + Num(SysMb));
                line(L"format quick fs=fat32 label=\"System\"");
                if (HasMsr()) line(L"create partition msr size=" + Num(MsrMb));
            }
            else if (Reserved)
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
            // MBR without system reserved: BIOS starts the Windows partition
            if (!Gpt && !Reserved) line(L"active");
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
    // (storage.cpp: IOCTLs without access rights, no PowerShell)

    // the system partition of the running Windows, and the disks of its Windows volume
    static void RunningWindows(storage::Extent& sys, bool& hasSys, std::vector<int>& winDisks)
    {
        hasSys = storage::SystemPartition(sys);
        winDisks = storage::WindowsDisks();
    }

    bool QueryDisks(std::vector<DiskInfo>& list, wstring& error)
    {
        list.clear();
        storage::Extent sys;
        bool hasSys;
        std::vector<int> winDisks;
        RunningWindows(sys, hasSys, winDisks);
        std::vector<storage::Volume> vols = storage::Volumes();
        for (const storage::Disk& s : storage::Disks())
        {
            if (s.Size <= 0) continue;
            DiskInfo d;
            d.Number = s.Number;
            d.Size = s.Size;
            d.Style = s.Style;
            d.Bus = s.Bus;
            d.Name = s.Name;
            d.Offline = s.Offline;
            d.ReadOnly = s.ReadOnly;
            d.IsBoot = std::find(winDisks.begin(), winDisks.end(), s.Number) != winDisks.end();
            d.IsSystem = hasSys && sys.Disk == s.Number;
            // the drive letters of the volumes on the disk, in the order of the disk
            std::vector<std::pair<long long, wchar_t>> letters;
            for (const storage::Volume& v : vols)
            {
                if (!v.Letter) continue;
                for (const storage::Extent& e : v.Extents)
                    if (e.Disk == s.Number) { letters.push_back({ e.Offset, v.Letter }); break; }
            }
            std::sort(letters.begin(), letters.end());
            for (const auto& l : letters) d.Letters += (d.Letters.empty() ? L"" : L",") + wstring(1, l.second);
            list.push_back(d);
        }
        if (list.empty()) { error = L"No disk was found."; return false; }
        error.clear();
        return true;
    }

    bool QueryPartitions(std::vector<DiskPartition>& list, wstring& error)
    {
        list.clear();
        storage::Extent sys;
        bool hasSys;
        std::vector<int> winDisks;
        RunningWindows(sys, hasSys, winDisks);
        std::vector<storage::Volume> vols = storage::Volumes();
        for (const storage::Disk& s : storage::Disks())
            for (const storage::Partition& x : s.Partitions)
            {
                DiskPartition p;
                p.Disk = s.Number;
                p.Number = x.Number;
                p.GptType = x.GptType;
                p.MbrType = x.MbrType;
                p.Active = x.Active;
                p.IsSystem = hasSys && sys.Disk == s.Number && sys.Offset == x.Offset;
                p.Size = x.Length;
                p.Offset = x.Offset;
                p.Style = s.Style;
                for (const storage::Volume& v : vols)
                    for (const storage::Extent& e : v.Extents)
                        if (e.Disk == s.Number && e.Offset == x.Offset)
                        {
                            p.Letter = v.Letter;
                            p.FileSystem = v.FileSystem;
                        }
                list.push_back(p);
            }
        if (list.empty()) { error = L"No partition was found."; return false; }
        error.clear();
        return true;
    }

    // a disk that must not be cleared: the disk of the running Windows,
    // of WimSmith, a read-only or offline disk
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
        if (p.HasSys() && (p.SysMb < (p.Gpt ? 100 : 50) || p.SysMb > 4096)) return L"dk.err_sys_size";
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
    // same number is refused), style, layout, sys_mb, msr, reserved, recovery_mb,
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
        if (!QueryDisks(disks, err)) return Finish(1, L"The disks were not read: " + err);
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
        // the disk was checked above (not the boot / system disk, nor that of WimSmith)
        wstring step;
        DWORD ce = storage::ClearAndInitialize(number, plan.Gpt, step);
        if (ce != 0) return Finish((int)ce, L"Clearing / initializing disk " + n + L" failed (" + step + L"): " + ErrorMessage(ce));
        int rc = 0;

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
