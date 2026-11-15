---
title: Folders selected together in Explorer are gathered into one Make-a-WIM window by the first process
author: OWNER
date: 2026-11-15
cost: { minutes: 16, tokens: 211459 }
id: l-0150da20
scope: repo
status: active
src: agent
touches:
  - src/assoc.cpp
  - src/makewim.h
  - src/makewim.cpp
  - src/main.cpp
  - src/helper.cpp
---

## Decided
- The folder menu is a static verb (Directory\shell\WimSmith, SubCommands, MultiSelectModel = Player). Explorer starts the command once per selected folder, so the first process takes a named mutex and opens the window; the others send their folder by WM_COPYDATA to its message-only window and end. No shell extension DLL (COM) was written: WimSmith stays a single exe that also runs in WinPE.
- The window runs the elevated helper itself (like the removed boot-once window did) with one capture job for all folders: one UAC prompt, not one per folder.
- The capture job grew source_count / s<k> / w<k> / n<k>; folders with the same w<k> are written into one WIM in one write. The old single-source keys still work unchanged for the backup tab.

## Watch out
- While the window asks (confirmation, UAC) or runs, it refuses new folders; the other processes retry for about 10 s and then open a window of their own once the mutex is released.
- An image name already in the file gets " (2)" in the helper (UniqueName) instead of failing.

In the code: src/makewim.cpp (MakeWimForward, RunMakeWim), src/assoc.cpp RegisterFolderMenu, src/helper.cpp CaptureItems / CaptureFile / Capture, App::PhaseText.
