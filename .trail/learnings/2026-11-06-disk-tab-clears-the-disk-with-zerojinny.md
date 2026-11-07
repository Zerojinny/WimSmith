---
title: "Disk tab clears the disk with Storage cmdlets but creates partitions with diskpart, and never preselects a disk"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 10, tokens: 158176 }
id: l-28eb1e5d
scope: repo
status: active
src: agent
touches:
  - backup/icon/make_icon.ps1
  - src/DiskJob.cs
  - src/BackupPanel.Disk.cs
---

## Decided
- Clearing and initializing (since 2026-11-07, no PowerShell; it is missing in WinPE): storage::ClearAndInitialize locks / dismounts the volumes of the disk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, zeroes the first and last MB (as diskpart "clean": MBR, both GPT headers), then IOCTL_DISK_CREATE_DISK GPT/MBR. The IOCTL adds no MSR, so the table starts empty (the PowerShell version had to remove the MSR Initialize-Disk added). Before: Clear-Disk -RemoveData -RemoveOEM + Initialize-Disk.
- Partitions are created by a diskpart script that follows Microsoft's deployment scripts: "create partition efi" formats the ESP as FAT32 without access problems, and "set id" / "gpt attributes=0x8000000000000001" give the recovery partition its type, which New-Partition / Set-Partition cannot do for every case (MBR type 0x27, GPT attributes).
- The helper checks the disk again (boot/system disk, the disk of 7z_wimlib, read-only, offline) and refuses when the disk's size differs from the size the GUI saw: disk numbers can change when a USB disk is replugged.
- The disk list never picks a disk on its own; the first entry is "(choose the disk to clear)". Unusable disks are listed with the reason instead of being hidden.
- After a Windows layout, only the restore target is set (new letter, W: when free); the existing UpdateBoot / DefaultSysPart logic then picks UEFI + ESP on GPT and BIOS + the active system reserved partition on MBR.

- MBR: the system reserved partition can be left out (2026-11-07, user request; box "Create" in the system partition row, setting disk.reserved, job key reserved, missing key = on). Then diskpart makes the Windows partition active, and the restore tab picks the target itself for BIOS boot files unless the disk has a separate small (<= 4096 MB) active partition; the helper makes the chosen partition active and the confirmation says so.

## Watch out
- Windows formats FAT32 only up to 32 GB and MBR uses only 2 TiB: both are refused before the job starts.
- The end-to-end run (elevated diskpart) was not tested in the session that added it; test on a VHD first.

In the code: src/DiskJob.cs (DiskPlan.Script, QueryDisks, DiskProblem, PlanProblem, WimLib.Disk, RunDiskpart), src/BackupPanel.Disk.cs (BuildDiskTab, StartDisk, DiskDone), src/BackupPanel.cs (ConfirmTyped titleKey, LETTER line in ReadHelperLines, elevate for mode disk).
