---
title: "Info tab changes go through a helper mode, exclusions replace the default capture configuration, and GUI tests on a locked screen use PrintWindow"
author: Zerojinny
date: 2026-11-08
branch: master
cost: { minutes: 28, tokens: 265098 }
id: l-67f8cfcb
scope: repo
status: active
src: agent
touches:
  - backup/cpp-before-wimlib-extras/7z_wimlib.ico
  - backup/cpp-before-wimlib-extras/app.manifest
  - backup/cpp-before-wimlib-extras/app.rc
  - backup/cpp-before-wimlib-extras/build.bat
  - backup/cpp-before-wimlib-extras/lang/en.ini
  - backup/cpp-before-wimlib-extras/lang/ko.ini
  - backup/cpp-before-wimlib-extras/src/app.cpp
  - backup/cpp-before-wimlib-extras/src/app.h
  - backup/cpp-before-wimlib-extras/src/common.h
  - backup/cpp-before-wimlib-extras/src/disk.cpp
---

## Decided
- The user asked for five wimlib extras at once: delete an image and optimize the file, fast append (update-of), an exclusion list, editing image info / the boot image, and Compact OS restore. All writes of the info tab run in the helper (mode "modify") like the other jobs: progress, cancel and the failure message come for free, and it is elevated only when the user may not write the file (job key admin = 1, from NeedsAdminToWrite, now shared with the browse tab).
- "Fast" (update-of) is on by default and uses the last image of the file as the template: it only skips reading files whose size and times match, so a changed file is always read again.

## Watch out
- A capture configuration file replaces WIMLIB_ADD_FLAG_WINCONFIG completely: wimlib's defaults (pagefile.sys, hiberfil.sys, swapfile.sys, System Volume Information, $Recycle.Bin, \Windows\CSC) must be written into the file too, or they get captured. Wildcards work in the middle of a path (\Users\*\AppData\Local\Temp\* was tested).
- wimlib_overwrite after wimlib_delete_image rebuilds the whole file by itself (a temp file beside it), so it needs free space of the file's size; the helper checks this first.
- Owner-drawn menu items: Windows does NOT add a check-mark width to itemWidth here; subtracting SM_CXMENUCHECK cut long items off.
- Testing the GUI while the user's screen is locked: mouse_event and CopyFromScreen fail. PrintWindow(hwnd, dc, 2) captures windows, PostMessage WM_LBUTTONDOWN/UP to the deepest child (ChildWindowFromPointEx) clicks, and a popup menu (#32768) takes WM_KEYDOWN VK_DOWN / VK_RETURN (posted clicks just close it).

In the code: Modify, CaptureConfig, CompactFlags in src/helper.cpp; InfoTab::ShowManageMenu / EditImage / StartModify; BackupTab::EditExclusions; RestoreTab::Compact; MenuOwnerProc in src/ui.cpp.
