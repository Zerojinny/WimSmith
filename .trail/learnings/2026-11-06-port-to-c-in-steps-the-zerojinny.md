---
title: "Port to C++ in steps: the helper first, behind the same job.ini/status.txt contract"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 26, tokens: 330922 }
id: l-d5e54cf2
scope: repo
status: active
src: agent
touches:
  - backup/cs-winforms/src/Libs.cs
  - backup/cs-winforms/src/MainForm.cs
  - src/common.h
  - src/util.cpp
  - src/wim.h
  - src/wim.cpp
  - src/helper.h
  - src/disk.h
  - src/disk.cpp
  - src/helper.cpp
---

## Decided
- Target: C++ with plain Win32 API, static CRT (/MT), x64, so the exe runs without .NET, including WinPE (the real gap: .NET 4.8 is built into Win10/11, but not WinPE). Rust was second choice; .NET NativeAOT ruled out (no WinForms), Go ruled out (GUI libs unmaintained), egui-style GPU UIs risky in WinPE.
- Step 1 = the helper only (`--wim-helper <work>`), because job.ini / status.txt is a clean seam: the C# GUI keeps working and simply starts `7z_wimlib_native.exe` when it sits next to it. Step 2 = the window, then the C++ exe becomes 7z_wimlib.exe.
- wimlib stays a separate DLL loaded with LoadLibrary/GetProcAddress (no wimlib.h, no import lib in the repo): keeps the LGPL picture unchanged and lets the exe start without the DLL. Struct fields are read by offset, same offsets as the C# version.
- The C# version is frozen in backup/cs-winforms (builds in place with its own build_gui.bat).
- Switch done 2026-11-07: cpp/src -> src, cpp/build.bat -> build.bat (builds 7z_wimlib.exe; objects in obj/), the C# sources and build_gui.bat removed from the root, the C#-only texts (bk.log_native_helper, cpp.not_ported, bk.err_snapshot_32bit) dropped from lang/.

## Watch out
- C# helper bug kept out of the port: in disk mode WaitReady demanded NTFS, so a "data only" layout with exFAT/FAT32 would end with "not ready" (code 21). The C++ WaitReady takes the expected file system.
- diskpart.txt: the C# helper wrote it as ASCII (Korean labels became '?'); the C++ helper writes the OEM code page. Not verified elevated yet.
- Elevated modes (capture with privileges, apply to a volume, bcdboot, disk) were not run end to end: no admin in the session. Test with a small VHD. Disk 5 (H:, a 256 GB mounted VHD) was kept out of tests as my own caution because it may hold data; the user never forbade it and may use it if it is scratch.

In the code: src (helper.cpp, disk.cpp, wim.cpp, util.cpp), build.bat (vswhere + vcvars64); the C# helper switch was in BackupPanel.StartHelper (now backup/cs-winforms). Verified by running the same jobs through both helpers (capture/append/export/LZMS/split/join/verify/apply-to-folder + refusals: identical EXIT lines, logs and progress counts) and by comparing DiskPlan output (9 plans x 3 sizes: identical diskpart scripts).
