---
title: "Disks, partitions and volumes are read and changed with Windows IOCTLs instead of PowerShell, so the tool works in WinPE"
author: Zerojinny
date: 2026-11-06
branch: master
cost: { minutes: 17, tokens: 225285 }
id: l-aa2e6732
scope: repo
status: active
src: agent
touches:
  - README.ko.md
  - README.md
  - app.rc
  - backup/cs-winforms/lang/en.ini
  - backup/cs-winforms/lang/ko.ini
  - backup/cs-winforms/src/BackupPanel.cs
  - build.bat
  - lang/en.ini
  - lang/ko.ini
---

## Decided
- No PowerShell anywhere (WinPE has it only as an optional component). storage.cpp does what the Storage cmdlets did: disks via \.\PhysicalDriveN opened with access 0 (GEOMETRY_EX, DRIVE_LAYOUT_EX, GET_DISK_ATTRIBUTES, IS_WRITABLE, STORAGE_QUERY_PROPERTY; all FILE_ANY_ACCESS, so the GUI needs no admin), volumes via FindFirstVolume + IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS matched to partitions by (disk, starting offset).
- Boot/system disk: IsBoot = a disk under the Windows volume's extents; IsSystem = the disk of HKLM\SYSTEM\Setup\SystemPartition (\Device\HarddiskVolumeN opened as \?\GLOBALROOT...).
- Format-Volume -> FormatEx of fmifs.dll (what format.com uses), retried once after FSCTL_DISMOUNT_VOLUME when it reports "can't lock" (= -Force). Add/Remove-PartitionAccessPath -> SetVolumeMountPoint / DeleteVolumeMountPoint on the volume GUID path. Set-Partition -IsActive -> DRIVE_LAYOUT_EX with BootIndicator + RewritePartition.
- diskpart stays for creating the partitions (it is in WinPE, and "create partition efi" / "set id" / "gpt attributes" are its job).

## Watch out
- Compared with the PowerShell output on this PC: identical disks/partitions, except the list is now sorted by disk number and the ESP's file system reads FAT32 (Get-Volume gave "" without admin). Reading took 16 ms instead of about 1.2 s.
- Disk name: vendor + product of STORAGE_DEVICE_DESCRIPTOR, vendor dropped when it is "ATA"/"NVMe" or repeated in the product; matched Get-Disk FriendlyName on 5 disks (SATA, NVMe) and gives "Msft Virtual Disk" for a VHD.
- The elevated paths (clear/initialize, FormatEx, mount points, set active) were written without admin in the session: test them on a VHD.

In the code: src/storage.h, src/storage.cpp; QueryDisks/QueryPartitions and Disk in src/disk.cpp; WriteBootFiles and Apply in src/helper.cpp; build.bat links advapi32.lib (RegGetValueW).
