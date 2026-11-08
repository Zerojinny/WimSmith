# 7z_wimlib — notes for development

A small Windows tool to back up a volume or folder to a WIM image and restore it, using [wimlib](https://wimlib.net). This file describes the program and its source for whoever works on it.

The other documents are for the people who use the program and ship with the release; keep them in step when a feature changes:

| File | For |
|---|---|
| `README.md`, `README.ko.md` | the release: what it is, what to download, how to start, licenses |
| `MANUAL.md`, `MANUAL.ko.md` | the user manual: every tab, common tasks, settings, troubleshooting |

## Features

- **Backup**: saves a volume or a folder as a new WIM file, or as a new image of an existing one (LZX / XPRESS / LZMS, thread count). With **VSS snapshot** checked, the source is read from a Volume Shadow Copy (wimlib's snapshot option): files in use are saved as they were at that moment, and the running Windows drive can be saved too (the WIM file must then be on another drive). Without a snapshot the running Windows drive cannot be saved.
- **Restore to a volume**: formats the volume (NTFS) and applies one image to its root, after two confirmations. The Windows drive, the drive of 7z_wimlib and the drive of the WIM file are not offered.
- **Restore to a folder**: extracts an image into an empty folder, without formatting.
- **Disk (partitions)**: clears a whole disk, initializes it as GPT or MBR and creates the partitions, after two confirmations (the disk number has to be typed). *For Windows*: GPT = EFI system (FAT32, 260 MB) + MSR (16 MB, can be left out) + Windows (NTFS); MBR = system reserved (NTFS, 500 MB, active) + Windows; a recovery partition after Windows can be added. *Windows + data*: the same plus a data partition at the end (NTFS / exFAT / FAT32); Windows gets a share of the free space in %, the data partition the rest. *One data partition*: NTFS / exFAT / FAT32 on the whole disk. The disk of the running Windows and the disk of 7z_wimlib cannot be chosen. After a layout with Windows the restore tab gets the new partition as its target, with UEFI (GPT) or BIOS (MBR) boot files on the new system partition.
- Restore and export also take a split WIM (`.swm`): choose any part, the other parts are found in its folder (all of them, with their names unchanged).
- **Export**: copies one image or all images of a WIM / ESD file into a new or an existing WIM file (other compression, ESD -> WIM, several files into one; name and description can be changed). Keeps the boot image mark. No administrator rights needed.
- **Split / join**: splits a WIM file into `.swm` parts of a chosen size (e.g. 4000 MB for a FAT32 USB stick), and joins the parts into one WIM file again (the other parts are found from any one of them). WIM files with solid resources (ESD / LZMS solid) cannot be split: export them with LZX first. No administrator rights needed.
- **Info**: the header of a WIM file (images, compression, size, boot image, GUID, flags) and the details of each image (edition, architecture, version, languages, size, files, dates); copy to the clipboard, or take an image over to the restore / export tab.
- **Verify**: [Verify] on the info tab reads all the data of a WIM / ESD / SWM file and checks it against its SHA-1 hashes, the metadata of every image, and the integrity table when the file has one (`wimlib_verify_wim`). "Verify the WIM file after the backup" on the backup tab does the same right after a backup. Nothing is written.
- **Integrity (`--check`)**: an option on the backup, restore, export and split / join tabs, off by default and not saved. Job key `check = 1`: files are opened with `WIMLIB_OPEN_FLAG_CHECK_INTEGRITY` (checked against their integrity table when they have one; a restore checks before formatting) and written with `WIMLIB_WRITE_FLAG_CHECK_INTEGRITY` (backup, export, every split part, the joined file).
- **Browse**: the files of an image, read with wimlib (`wimlib_iterate_dir_tree`, in the GUI process, no elevation): folder tree and file list, extract the selected files / folders into a folder (`wimlib_extract_paths` without ACLs), open a file. Edit commands ([Edit] button, right-click menu, Del / F2 / F5): new folder, add files / a folder, rename, delete, through `wimlib_update_image` + `wimlib_overwrite` (in place, appended; added files without ACLs); not for split or read-only files. A file the user may not write (root of C:, Program Files) goes to the elevated helper (job mode `update`, keys c<n>_op / _a / _b / _flags; `wl::UpdateImage` builds the commands for both). The version that listed and extracted with `7z.exe` is kept in `backup\cpp-7zip-browse\` (git tag `before-wimlib-browse`). 7-Zip is not used any more (its [Open in 7-Zip] button and download were removed).
- **Boot files**: after a volume restore, `bcdboot` can write UEFI and/or BIOS boot files to a chosen system partition (with a boot manager language, default `ko-kr`). A partition without a drive letter gets one for that time.
- **When done**: nothing / shut down / restart, after a 60-second countdown that can be cancelled.
- Korean and English UI, dark or light theme (chosen in the footer, saved as `theme = dark | light` in `7z_wimlib.ini`); the backup tab shows a preview of the job below its options. The old light UI is kept in `backup\ui-classic\` (build it with the `build_gui.bat` there).

## Requirements

- Windows 8.1 / 10 / 11 x64. `7z_wimlib.exe` is a single native exe (C++, static C runtime) and needs no runtime such as the .NET Framework; it runs in an x64 WinPE as well (no Explorer needed: the file dialogs are its own).
- `lib\` next to `7z_wimlib.exe` holds the third-party DLL `libwim-15.dll` ([wimlib](https://wimlib.net), x64). When it is missing it is **downloaded automatically in its newest version** when it is first needed (the zip of wimlib.net, unpacked by `src\unzip.cpp`: no 7-Zip or tar.exe needed; nothing is installed). `lib\versions.ini` records the version; a `libwim-15.dll` in the old `pe_tools\` folder is moved into `lib\` at start. Without an internet connection, put the file there by hand. It is not part of this repository; its license texts are kept next to it in `lib\` (see [Third-party programs](#third-party-programs)).
- Administrator rights for backup, restore and the disk tab (Windows asks each time); export, split / join, info, verify and browse do not need them.

## Build

```
build.bat
```

Compiles `src\*.cpp` into `7z_wimlib.exe` (x64) in this folder with the Visual C++ of Visual Studio 2017 or later (Community or Build Tools, workload "Desktop development with C++"; found with vswhere). The object files go to `obj\`. The language files `lang\*.ini` are embedded; an edited `lang\` folder next to the exe is used first.

wimlib stays a separate DLL loaded at run time (`lib\libwim-15.dll`).

The former C# / .NET Framework version (up to the C++ port) is kept in `backup\cs-winforms\`, with the `build_gui.bat` that builds it.

## Files

| Path | Content |
|---|---|
| `src\main.cpp` | entry point: the window, or `--wim-helper` |
| `src\app.*`, `src\mainwin.*` | what the tabs share (volumes, confirmations, running a job), the main window, footer and log window |
| `src\tab_*.*` | the tabs: restore, disk, backup, export, split / join, info, browse |
| `src\helper.*` | the elevated helper (job.ini -> status.txt): backup, restore, export, split / join, verify, bcdboot |
| `src\disk.*`, `src\storage.*` | disk tab: disks, partitions and volumes (IOCTLs of Windows, no PowerShell), clearing / initializing a disk, formatting (FormatEx), drive letters, the diskpart script, the disk job |
| `src\wim.*`, `src\wimread.cpp` | wimlib (`lib\libwim-15.dll`, loaded at run time), WIM file and image information |
| `src\libs.*`, `src\unzip.*` | `lib\` folder: moving and downloading wimlib (WinHTTP), license texts; reading the zip (stored / Deflate, CRC-32) |
| `src\ui.*` | the window toolkit on Win32: dark controls, tabs, lists, dialogs, row layout |
| `src\lang.*`, `src\settings.*`, `src\util.cpp`, `src\common.h` | language files, `7z_wimlib.ini`, text / file / process helpers |
| `app.rc`, `app.manifest` | icon and embedded language files; manifest (DPI, Common Controls 6, Windows versions) |
| `lang\en.ini`, `lang\ko.ini` | texts (en.ini is the reference) |

The elevated helper works in `%TEMP%\7z_wimlib_job\<time>` (job.ini, status.txt, wimlib_errors.txt); the folder is deleted after a successful job and kept after a failed one.

## Working on it

- Source files use CRLF line ends; `sed -i` turns them into LF, so fix with `unix2dos` or edit with a script that keeps CRLF. The language files are UTF-8 with BOM.
- `lang\en.ini` is the reference: add every new text there and in `lang\ko.ini`. Hint texts under the start buttons were removed on purpose; explanations go into the manual.
- Popup menus go through `ui::PopupMenu` (owner-drawn items, menu background, the frame of the #32768 window): the menu of Windows stays light in the dark theme. Do not call `TrackPopupMenu` directly.
- No PowerShell at run time (it is missing in many WinPE images): disks, volumes and formatting go through `src\storage.*`.
- Elevated jobs cannot see the session's network or SUBST drives: paths given to the helper go through `SessionFreePath`.
- Small screens (WinPE without a video driver, 640 x 480): every tab page sits in a scrolling host (`ui::ScrollHost`, minimum 828 x 490, lower for browse / info with `SetScrollMin`); the main window starts maximized when 900 x 600 does not fit, windows and dialogs are kept on the screen (`FitToWorkArea`), the tab strip and the footer get narrower. A new tab or dialog must keep working at that size: lay dialogs out from their client size, not from fixed coordinates.
- When `7z_wimlib.exe` is running, `build.bat` cannot replace it: close it first, or build to another folder.
- Testing: use a copy of the exe in a scratch folder with its own `7z_wimlib.ini`; never edit the user's ini, never close the user's running instance, and do not start elevated jobs from a test (the UAC prompt appears on the user's desktop). The helper can be run directly (`7z_wimlib.exe --wim-helper <folder with job.ini>`) for jobs without elevation, such as a folder backup or verify.
- Learnings from past sessions are in `.trail/learnings/`.

## License

BSD 3-Clause, see [LICENSE](LICENSE). This applies to the code of this repository only.

### Third-party programs

7z_wimlib uses one separate program. Its code is not part of this repository, and it keeps its own license:

| Program | Files in `lib\` | License | License texts in `lib\` | Source |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | LGPL v2.1 or later for the library on Windows (or GPL v3 or later); includes libdivsufsort-lite (MIT) | `wimlib_COPYING.txt`, `wimlib_COPYING.LGPL.txt`, `wimlib_COPYING.GPLv3.txt`, `wimlib_COPYING.libdivsufsort-lite.txt` | https://wimlib.net/ |

How 7z_wimlib uses them:

- wimlib is loaded as a separate DLL (`libwim-15.dll`) and called through its public API. It is not modified, and it can be replaced by another version by replacing the DLL in `lib\`.
- The repository does not contain it. 7z_wimlib downloads it from wimlib.net on the user's PC, together with its license texts. When the DLL is there without its license texts (for example a `libwim-15.dll` moved from `pe_tools\`), 7z_wimlib downloads only the texts in the background at start.

When you pass on 7z_wimlib **together with** `lib\` (for example in a zip file):

- keep the license texts listed above in `lib\`;
- say that this file is wimlib under its own license, with the links to their sources above (the version of wimlib is in `lib\versions.ini` when 7z_wimlib downloaded it);
- do not change the DLL; if you do, the changed source has to be published under the same license.
