---
title: Exporting a WIM image drops its boot mark unless the boot flag is passed; export runs without admin rights
author: Zerojinny
date: 2026-11-06
cost: { minutes: 1, tokens: 1403 }
id: l-5349ad30
scope: repo
status: active
src: agent
---

## Decided
- The export tab runs in the same helper as backup/restore, but started without "runas": wimlib needs no privileges to copy images, so the user gets no UAC prompt. The helper skips acquiring privileges for mode = export.
- The boot image mark is kept: when the exported image is the source's boot image (or all images are exported), the export passes the boot flag, unless the target file being appended to already has its own boot image.
- The info tab only reads (in the GUI process); editing name/description was left out on purpose so the GUI does not write to user WIM files.

## Tried and ruled out
- Exporting with flags 0: a WinPE boot.wim (boot index 1) came out with boot index 0, which breaks booting from it.

## Watch out
- wimlib_export_image: the source WIM must stay open until the destination is written and freed (nested try/finally in Export).
- For "all images", name and description must be null.

In the code: `src/WimLib.cs` (Export, BootIndex, ReadInfo; ExportBoot = 0x1), `src/BackupPanel.cs` (StartHelper: elevate = jobMode != "export").
