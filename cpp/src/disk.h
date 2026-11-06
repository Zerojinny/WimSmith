// ============================================================
//  7z_wimlib (C++) - disk.h
//  Disks and partitions (Get-Disk / Get-Partition of the Storage
//  module) and the layouts of the disk tab (disk.cpp).
// ============================================================
#pragma once
#include "common.h"

namespace dvt
{
    // a local disk (Get-Disk)
    struct DiskInfo
    {
        int Number = -1;
        long long Size = 0;
        wstring Name, Style, Bus, Letters;   // Letters: "E,F" ("" = none)
        bool IsBoot = false, IsSystem = false, Offline = false, ReadOnly = false;
    };

    // a partition of a local disk (Get-Partition), for the boot files
    struct DiskPartition
    {
        int Disk = -1, Number = -1;
        wchar_t Letter = 0;                  // 0 = no drive letter
        wstring GptType, FileSystem, Style;
        int MbrType = 0;
        bool Active = false, IsSystem = false;
        long long Size = 0, Offset = -1;

        bool Gpt() const { return Style == L"GPT"; }
        bool IsEsp() const;
        bool IsMsr() const;
    };

    // what the disk tab creates
    //   Windows only : [EFI (+ MSR) | system reserved] + Windows [+ recovery]
    //   Windows+data : the same, Windows gets WindowsPercent of the free space, data the rest
    //   data only    : one partition on the whole disk
    struct DiskPlan
    {
        static const int MsrMb = 16;
        static const long long Fat32Max = 32LL * 1024 * 1024 * 1024;
        static const long long MbrMax = 2199023255552LL;      // 2 TiB
        static const long long WindowsMin = 1073741824LL;     // 1 GB
        static const long long DataMin = 16LL * 1048576;

        bool Gpt = true, Windows = true, Data = false, Msr = true, Recovery = false;
        int SysMb = 260, RecoveryMb = 1024, WindowsPercent = 50;
        wstring Label = L"Windows", DataFs = L"NTFS", DataLabel = L"Data";

        static int DefaultSysMb(bool gpt) { return gpt ? 260 : 500; }
        wstring Layout() const { return !Windows ? L"data" : Data ? L"windows_data" : L"windows"; }
        bool HasMsr() const { return Gpt && Windows && Msr; }

        static DiskPlan FromJob(const Job& job);
        wstring WindowsLabel() const;
        wstring DataLabelClean() const;
        long long FreeBytes(long long diskSize) const;
        long long WindowsBytes(long long diskSize) const;
        long long DataBytes(long long diskSize) const;
        wstring Script(int disk, wchar_t letter, long long diskSize) const;
    };

    bool QueryDisks(std::vector<DiskInfo>& list, wstring& error);
    bool QueryPartitions(std::vector<DiskPartition>& list, wstring& error);
    wstring DiskProblem(const DiskInfo& d, const wstring& buildRoot);
    wstring PlanProblem(const DiskPlan& p, long long diskSize);
    wstring SystemPartitionProblem(const DiskPartition& p, const wstring& fw);
}
