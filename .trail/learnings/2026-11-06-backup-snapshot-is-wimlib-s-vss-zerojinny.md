---
title: "Backup snapshot is wimlib's VSS flag, and it alone unlocks the running Windows drive as a source"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 8, tokens: 72556 }
id: l-2da9eca5
scope: repo
status: active
src: agent
---

## Decided
- "Snapshot" on the backup tab means a VSS snapshot through wimlib itself: WIMLIB_ADD_FLAG_SNAPSHOT = 0x00008000 (wimlib.h, since 1.9.0) passed to wimlib_add_image. wimlib creates the shadow copy, captures from it and deletes it; no own VSS code. It needs administrator rights (the helper has them) and a 64-bit process on 64-bit Windows (anycpu build; the GUI refuses a 32-bit process).
- Only with the snapshot checked is the running Windows drive (and its Windows folder) a valid source; FillSources lists it then, marked as running Windows. The WIM file inside the source stays refused even with a snapshot (an existing WIM on C: would be captured into itself).
- The checkbox is kept in 7z_wimlib.ini as backup.snapshot.

## Watch out
- lang\*.ini are UTF-8 with BOM and CRLF. A Python regex like `^key = .*$` with re.M matches up to the \r, so replacing the line drops the \r and appending after group(1) doubles it: the files ended up with mixed CR / LF / CRLF. Read with newline='' and replace only text without the line end, or normalize to CRLF afterwards.

In the code: `src/WimLib.cs` (`AddSnapshot`, `CaptureProblem`, `Capture`), `src/BackupPanel.cs` (`chkSnapshot`, `FillSources`, `StartCapture`), `lang/en.ini`, `lang/ko.ini`.
