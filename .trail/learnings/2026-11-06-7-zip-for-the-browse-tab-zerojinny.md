---
title: "7-Zip for the browse tab comes from the official installer unpacked with 7zr, and lists one WIM image at a time"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 16, tokens: 157833 }
id: l-d4787451
scope: repo
status: active
src: agent
touches:
  - src/Libs.cs
  - src/WimBrowser.cs
  - README.ko.md
---

## Decided
- Third-party programs live in lib\ next to the exe and are downloaded (newest version) when first needed; a libwim-15.dll in the old pe_tools\ is moved into lib\ at start. lib/ is git-ignored.
- 7-Zip: download 7zr.exe plus the x64 installer (7z<ver>-x64.exe) and run "7zr x installer 7z.exe 7z.dll 7zFM.exe License.txt". The installer is a 7z archive, so nothing is installed or run from it. The 7-zip.org download page links to GitHub releases; the GitHub API (ip7z/7zip) is the fallback.
- wimlib: newest wimlib-<ver>-windows-x86_64-bin.zip linked on https://wimlib.net/ (relative href "downloads/..."); libwim-15.dll sits at the zip root.
- Listing uses "7z l -ba -sccUTF-8" (fixed columns: date 19, attr 5, size 12, packed 12, two spaces, name), not -slt, which is ~10 MB for one WinPE image.
- A multi-image WIM appears in 7-Zip as folders 1, 2, ... plus [1].xml; a single-image WIM shows its content at the root. The browse tab lists one image with the filter "<n>\*" (fast) instead of the whole file.
- Extracting a selection: 7z keeps the in-archive path, so it extracts into a hidden work folder inside the target and then moves the items up.

## Tried and ruled out
- 7za.exe from 7z<ver>-extra.7z: the standalone build does not read WIM.

## Watch out
- Testing 7z list files from bash printf: "\n" in a path like System32\notepad.exe becomes a newline and silently extracts the whole folder. The C# list file is fine.
- Windows PowerShell 5.1 misreads Korean in a .ps1 without a BOM; and UI Automation sees only anonymous panes in this app, so drive it with SendKeys.

In the code: `src/Libs.cs` (Migrate, EnsureWimlib, Ensure7zip, Download7zip), `src/WimBrowser.cs` (ListImage, AddLine, ExtractSelected, MoveMerge), `build_gui.bat` (/r:System.IO.Compression*.dll).
