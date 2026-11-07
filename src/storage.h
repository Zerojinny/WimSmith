// ============================================================
//  7z_wimlib - storage.h
//  Disks, partitions and volumes with the Windows API only (no
//  PowerShell, so it works in a WinPE without it) - storage.cpp:
//   - reading (no administrator rights needed): \\.\PhysicalDriveN
//     opened without access rights (IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
//     _GET_DRIVE_LAYOUT_EX, _GET_DISK_ATTRIBUTES, STORAGE_QUERY_PROPERTY);
//     the volumes (FindFirstVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
//     their drive letters and file systems);
//   - changing (elevated helper): clearing and initializing a disk
//     (as diskpart "clean" + IOCTL_DISK_CREATE_DISK), formatting a
//     volume (FormatEx of fmifs.dll, as format.com), adding / removing a
//     drive letter (SetVolumeMountPoint), making an MBR partition active.
// ============================================================
#pragma once
#include "common.h"

namespace sevenwim
{
    namespace storage
    {
        struct Extent
        {
            int Disk = -1;
            long long Offset = 0, Length = 0;
        };

        struct Volume
        {
            wstring Path;                 // \\?\Volume{guid}\ .
            wchar_t Letter = 0;           // 0 = none (the first, when several)
            wstring FileSystem;           // "" = unknown / not formatted / no access
            std::vector<Extent> Extents;
        };

        struct Partition
        {
            int Number = 0;
            long long Offset = 0, Length = 0;
            wstring GptType;              // "{c12a7328-...}" (lower case), "" on MBR
            int MbrType = 0;
            bool Active = false;
        };

        struct Disk
        {
            int Number = -1;
            long long Size = 0;
            wstring Style;                // GPT | MBR | RAW
            wstring Bus, Name;
            bool Offline = false, ReadOnly = false;
            std::vector<Partition> Partitions;
        };

        std::vector<Volume> Volumes();
        // every \\.\PhysicalDriveN that answers, by number
        std::vector<Disk> Disks();
        // the system partition the running Windows started from (registry
        // SYSTEM\Setup\SystemPartition), and the disks of the Windows volume
        bool SystemPartition(Extent& e);
        std::vector<int> WindowsDisks();

        // ---- elevated ----
        // removes every partition and the partition tables; gpt: then writes an
        // empty GPT table. An MBR disk is left uninitialized (RAW): diskpart
        // "convert mbr" initializes it, because IOCTL_DISK_CREATE_DISK writes no
        // MBR boot code and BIOS could not start from the disk.
        // 0 = done, else a Win32 error (step = what failed)
        DWORD ClearAndInitialize(int disk, bool gpt, wstring& step);
        // the first sector of the disk has boot code (bytes 0..439 not all zero)
        DWORD HasMbrBootCode(int disk, bool& has);
        // quick format; 0 = done, else a Win32 error (reason = what FormatEx said)
        DWORD Format(wchar_t letter, const wstring& fileSystem, const wstring& label, wstring& reason);
        // drive letter of the partition at (disk, offset)
        DWORD AddLetter(int disk, long long offset, wchar_t letter);
        DWORD RemoveLetter(wchar_t letter);
        // MBR: this partition becomes the active one, the others not
        DWORD SetActive(int disk, int partition);
    }
}
