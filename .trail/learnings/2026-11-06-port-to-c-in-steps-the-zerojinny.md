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
  - src/Libs.cs
  - src/MainForm.cs
  - cpp/src/common.h
  - cpp/src/util.cpp
  - cpp/src/wim.h
  - cpp/src/wim.cpp
  - cpp/src/helper.h
  - cpp/src/disk.h
  - cpp/src/disk.cpp
  - cpp/src/helper.cpp
---

## Decided
- Target: C++ with plain Win32 API, static CRT (/MT), x64, so the exe runs without .NET, including WinPE (the real gap: .NET 4.8 is built into Win10/11, but not WinPE). Rust was second choice; .NET NativeAOT ruled out (no WinForms), Go ruled out (GUI libs unmaintained), egui-style GPU UIs risky in WinPE.
- Step 1 = the helper only (`--wim-helper <work>`), because job.ini / status.txt is a clean seam: the C# GUI keeps working and simply starts `7z_wimlib_native.exe` when it sits next to it. Step 2 = the window, then the C++ exe becomes 7z_wimlib.exe.
- wimlib stays a separate DLL loaded with LoadLibrary/GetProcAddress (no wimlib.h, no import lib in the repo): keeps the LGPL picture unchanged and lets the exe start without the DLL. Struct fields are read by offset, same offsets as the C# version.
- The C# version is frozen in backup/cs-winforms (builds in place with its own build_gui.bat).

## Watch out
- C# helper bug kept out of the port: in disk mode WaitReady demanded NTFS, so a "data only" layout with exFAT/FAT32 would end with "not ready" (code 21). The C++ WaitReady takes the expected file system.
- diskpart.txt: the C# helper wrote it as ASCII (Korean labels became '?'); the C++ helper writes the OEM code page. Not verified elevated yet.
- Elevated modes (capture with privileges, apply to a volume, bcdboot, disk) were not run end to end: no admin in the session. Test with a VHD, never the user's disk 5 (H:).

In the code: cpp/src (helper.cpp, disk.cpp, wim.cpp, util.cpp), cpp/build.bat (vswhere + vcvars64), C# switch in BackupPanel.StartHelper. Verified by running the same jobs through both helpers (capture/append/export/LZMS/split/join/verify/apply-to-folder + refusals: identical EXIT lines, logs and progress counts) and by comparing DiskPlan output (9 plans x 3 sizes: identical diskpart scripts).
