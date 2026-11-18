# WimSmith — notes for development

[한국어](CLAUDE.md) (the main version; this is its English translation)

A small Windows tool to back up a volume or folder to a WIM image and restore it, using [wimlib](https://wimlib.net). This file describes the program and its source for whoever works on it.

The other documents are for the people who use the program and ship with the release; keep them in step when a feature changes:

| File | For |
|---|---|
| `README.md`, `README.en.md` | the release: what it is, what to download, how to start, licenses |
| `MANUAL.md`, `MANUAL.en.md` | the user manual: every tab, common tasks, settings, troubleshooting |

Every document is in Korean as `<name>.md` (the main version, shown first on GitHub) and in English as `<name>.en.md`; each links to the other on its first lines. Change both together. `LICENSE` stays in English only (the MIT text is the legal one).

## Features

- **Backup**: saves a volume or a folder as a new WIM file, or as a new image of an existing one (LZX / XPRESS / LZMS, thread count). With **VSS snapshot** checked, the source is read from a Volume Shadow Copy (wimlib's snapshot option): files in use are saved as they were at that moment, and the running Windows drive can be saved too (the WIM file must then be on another drive). Without a snapshot the running Windows drive cannot be saved. **Fast** (with "add a new image", job key `update_of = 1`): `wimlib_reference_template_image` against the last image, so files of the same size and times are not read again (wimcapture `--update-of`). **Exclude** (keys `exclude_count`, `x1`..`xn`): the helper writes a capture configuration file (UTF-16 with BOM, wimlib's Windows defaults plus the user's paths) into its work folder and passes it instead of `WIMLIB_ADD_FLAG_WINCONFIG`; the list is `backup.exclude` in the ini (`|`-separated), the recommended one is in `tab_backup.cpp`.
- **Extract from the right-click menu** (items `1_extract` / `2_extract_each` of the `.wim` / `.esd` / `.swm` menu, `assoc::MenuItem::args` = `--extract one|each`, `MultiSelectModel = Player` on the menu and these items; `UpdateMenuLanguage` compares the first item with `xw.menu_one`): `src\extractwim.cpp`, run from main.cpp before the main window, without elevation, in this process (`wl::Load`, a thread, `wimlib_extract_image` with `ExtractNoAcls | ExtractReplaceInvalidNames`, progress and [Cancel] in a small window). The files of one click are gathered (`gather::Forward("Extract.<mode>")`, see below) and it starts once none came in for a second. `one`: a new folder `<dir of the first file>\<stem>` (`(2)`, `(3)`, … when it exists) for all files; `each`: one per file. One image: into the folder; several: a subfolder `<index> <name>` each. A `.swm` part becomes the first part of its set (`SwmParts`), once.
- **Make a WIM from folders** (right-click menu of folders, [Settings] "st.menu_folders": `assoc::RegisterFolderMenu`, HKCU `Software\Classes\Directory\shell\WimSmith` with `SubCommands`, items `1_one` / `2_each` / `3_open` (texts `mw.menu_one` / `_each` / `_open`, `FolderMenu()` in mainwin.cpp), `MultiSelectModel = Player`; command `--make-wim <one|each|open> "%1"`; made again at start when the language or the number of items differs, `UpdateMenuLanguage`): `src\makewim.cpp`, run from main.cpp before the main window. Explorer starts one process per selected folder: `MakeWimForward` (`src\gather.*`, shared with extract) makes the named mutex `Local\WimSmith.MakeWim.<mode>` (first process) or sends the folder by `WM_COPYDATA` to the message-only window `WimSmith.MakeWim.<mode>.Receiver` of the first one (retries for ~10 s; refused while it asks or runs) and ends. The window: folders ([Add...] / [Remove]), *one WIM file (an image per folder)* or *a WIM file per folder* (`<folder name>.wim` in a folder), compression, threads, VSS snapshot, verify, `--check`; `one` / `each` preset the way and press [Start] by themselves, without the confirmation, once no folder came in for a second (timer 2); `open` waits for [Start]. [Start] runs the elevated helper itself (job.ini in `%TEMP%\WimSmith_job\<time>-<pid>`, `capture` with `source_count = n`, `s<k>` / `w<k>` / `n<k>`, `append = 1`) and shows its progress (`App::PhaseText`, phase `source <k> <n>`). The helper writes one file per distinct `w<k>` (all its images in one write, `CaptureFile`), adds to an existing file, renames a used image name to "name (2)" (`UniqueName`), and refuses a WIM file inside any source.
- **Restore to a volume**: formats the volume (NTFS) and applies one image to its root, after two confirmations. The Windows drive, the drive of WimSmith and the drive of the WIM file are not offered. Once the partitions are read (`LoadPartitions`), each volume of the target list ends with the style and number of its disk (`TargetText`, text `rs.target_style`, e.g. `· GPT disk 0`; `ShowTargetStyles` keeps the selection and fires no change). **Compression** (Compact OS, key `compact = 1..4`): `WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K / 8K / 16K / LZX`, also for a folder; only when the running Windows is 10 or later (`RtlGetVersion`); the size check is skipped then.
- **Restore to a folder**: extracts an image into an empty folder, without formatting.
- **Disk (partitions)**: clears a whole disk, initializes it as GPT or MBR and creates the partitions, after two confirmations (the disk number has to be typed). *For Windows*: GPT = EFI system (FAT32, 260 MB) + MSR (16 MB, can be left out) + Windows (NTFS); MBR = system reserved (NTFS, 500 MB, active) + Windows; a recovery partition after Windows can be added. *Windows + data*: the same plus a data partition at the end (NTFS / exFAT / FAT32); Windows gets a share of the free space in %, the data partition the rest. *One data partition*: NTFS / exFAT / FAT32 on the whole disk. The disk of the running Windows and the disk of WimSmith cannot be chosen. After a layout with Windows the restore tab gets the new partition as its target, with UEFI (GPT) or BIOS (MBR) boot files on the new system partition.
- Restore and export also take a split WIM (`.swm`): choose any part, the other parts are found in its folder (all of them, with their names unchanged).
- **Export**: copies one image or all images of a WIM / ESD file into a new or an existing WIM file (other compression, ESD -> WIM, several files into one; name and description can be changed). Keeps the boot image mark. No administrator rights needed.
- **Split / join**: splits a WIM file into `.swm` parts of a chosen size (e.g. 4000 MB for a FAT32 USB stick), and joins the parts into one WIM file again (the other parts are found from any one of them). WIM files with solid resources (ESD / LZMS solid) cannot be split: export them with LZX first. No administrator rights needed.
- **Info**: the header of a WIM file (images, compression, size, boot image, GUID, flags) and the details of each image (edition, architecture, version, languages, size, files, dates); copy to the clipboard, or take an image over to the restore / export tab. **[Manage ▾]** (helper mode `modify`, key `op`): `props` (keys `name` / `description` / `flags` / `boot`, only the changed ones; `wimlib_set_image_property`, `wimlib_set_wim_info` boot index), `delete` (`wimlib_delete_image`), `optimize` (`WIMLIB_WRITE_FLAG_REBUILD`), then `wimlib_overwrite`; delete / optimize rebuild the whole file and are refused without free space of the file's size. Elevated only with `admin = 1` (the GUI sets it when `NeedsAdminToWrite`); the browse tab then reads the file again (`BrowseTab::WimChanged`).
- **Verify**: [Verify] on the info tab reads all the data of a WIM / ESD / SWM file and checks it against its SHA-1 hashes, the metadata of every image, and the integrity table when the file has one (`wimlib_verify_wim`). "Verify the WIM file after the backup" on the backup tab does the same right after a backup. Nothing is written.
- **Integrity (`--check`)**: an option on the backup, restore, export and split / join tabs, off by default and not saved. Job key `check = 1`: files are opened with `WIMLIB_OPEN_FLAG_CHECK_INTEGRITY` (checked against their integrity table when they have one; a restore checks before formatting) and written with `WIMLIB_WRITE_FLAG_CHECK_INTEGRITY` (backup, export, every split part, the joined file).
- **Browse**: the files of an image, read with wimlib (`wimlib_iterate_dir_tree`, in the GUI process, no elevation): folder tree and file list, extract the selected files / folders into a folder (`wimlib_extract_paths` without ACLs), open a file. Edit commands ([Edit] button, right-click menu, Del / F2 / F5): new folder, add files / a folder, rename, delete, through `wimlib_update_image` + `wimlib_overwrite` (in place, appended; added files without ACLs); not for split or read-only files. A file the user may not write (root of C:, Program Files) goes to the elevated helper (job mode `update`, keys c<n>_op / _a / _b / _flags; `wl::UpdateImage` builds the commands for both). The version that listed and extracted with `7z.exe` is kept in `backup\cpp-7zip-browse\` (git tag `before-wimlib-browse`). 7-Zip is not used any more (its [Open in 7-Zip] button and download were removed).
- **Drivers (DISM)**: the restore tab can add the drivers of a folder to the restored Windows (volume or folder target), after the apply and before the boot files: job keys `drivers` (the folder) and `dism` (the dism.exe), helper `AddDrivers`: `dism /Image:<root> /Add-Driver /Driver:<folder> /Recurse /LogPath:<work>\dism.log` (`DismArg` quotes a path with spaces and drops a `\` before the closing quote). When DISM fails, the boot files are still written and the job ends with its code. Which DISM: [Settings] `dism = system | folder` and `dism.folder` (`DismExe()`: `System32\dism.exe`, or `<folder>\dism.exe` with a relative folder read against the exe folder, e.g. the ADK DISM folder). [Import] in [Settings] (`ImportSystemDism`, settings.cpp) copies `System32\Dism\*` flat into `<exe>\dism` with `dism.exe`, `dismapi.dll`, `wimgapi.dll` and their `<lang>\*.mui` (the layout of the ADK DISM folder), then sets `dism = folder`, `dism.folder = dism` (saved with [OK]). The folder of the tab is kept as `restore.drivers`; the box is not saved.
- **Boot files**: after a volume restore, `bcdboot` can write UEFI and/or BIOS boot files to a chosen system partition (with a boot manager language, default `ko-kr`). A partition without a drive letter gets one for that time. **Delete the existing BCD** (check box `chkBcdDelete`, off by default, not saved, job key `bcd_delete = 1`): before `bcdboot`, the helper's `DeleteBcd` deletes the BCD store of the firmware (UEFI `\EFI\Microsoft\Boot\BCD`, BIOS `\Boot\BCD`, ALL both) and its `.LOG*` files (hidden / system attributes cleared first). A missing file is fine; one that cannot be deleted ends the job with that error, without `bcdboot`. The confirmation gets the line `rs.confirm_bcd_delete`.
- **When done**: nothing / shut down / restart, after a countdown that can be cancelled: 10 seconds, 1 - 60 in [Settings] (`after_seconds`, `AfterSeconds()` in settings.cpp).
- **Settings** ([Settings] in the footer, `MainWindow::ShowSettings`): the theme; **.wim / .esd / .swm opened with WimSmith**, each on its own (`src\assoc.*`: ProgIDs `WimSmith.wim` / `WimSmith.esd` / `WimSmith.swm` under HKCU\Software\Classes, no elevation; `DefaultIcon` = `"<exe>",-2 / -3 / -4`, the file icons of `app.rc` (`icons\wim.ico`, `esd.ico`, `swm.ico`, drawn by `backup\icon\make_file_icons.ps1`: a page with the layers of the program icon and a band with the extension, WIM teal / ESD violet / SWM orange; `assoc::UpdateIcons` sets it again at start for associations of an older version); the previous default of each extension is kept in the value `WimSmith.PreviousDefault` and given back when the box is cleared; a UserChoice made in Windows wins, then the user is told to pick WimSmith under *Open with*); the tab a WIM file opens in (`open_tab = browse | info | restore | append | export | splitjoin`, default browse; `append` = the backup tab with the file as target and "add a new image", `splitjoin` = split, or join for a `.swm`); the **right-click menu** "WimSmith >" of `.wim` / `.esd` / `.swm` (`assoc::RegisterMenu`: HKCU\Software\Classes\SystemFileAssociations\<ext>\shell\WimSmith with `SubCommands` and items `1_restore`..; shown whatever program opens the file; Windows 11: under "Show more options"; .esd without append / split, .swm without append; made again at start when the language differs, `UpdateMenuLanguage`). **Restore: path** (`relative_paths = 1`, default 0, `SetRelativePaths`): the restore tab saves its last WIM file read as `restore.wim` (`SetRestoreWim`, started with it before the backup's `image_folder`), relative to the exe folder when on the same drive (`PathRelativePathToW`; `RestoreWim()` reads a relative value against the exe folder whatever the setting; toggling writes it again in the new form). The backup tab keeps `image_folder` as a full path. The GUI process runs with the exe folder as its current folder (main.cpp, after the command-line file was made full), so a relative path typed in a tab means the exe folder; `StartHelper` makes the path keys `wim` / `source` / `folder` / `swm` full before job.ini. `WimSmith.exe <file>` opens the file in the `open_tab` tab, `WimSmith.exe --open <tab> <file>` in the tab given (`App::OpenFile`, only with the first window); with `open_all_tabs = 1` (default, [Settings]) the other tabs get the file too, quietly and before the chosen one: restore, export, info, browse always, backup target (+ add a new image) and split only for `.wim`, join for `.swm`.
- Korean and English UI, dark or light theme (chosen in [Settings], saved as `theme = dark | light` in `WimSmith.ini`); the backup tab shows a preview of the job below its options. The old light UI is kept in `backup\ui-classic\` (build it with the `build_gui.bat` there).

## Requirements

- Windows 8.1 / 10 / 11 x64. `WimSmith.exe` is a single native exe (C++, static C runtime) and needs no runtime such as the .NET Framework; it runs in an x64 WinPE as well (no Explorer needed: the file dialogs are its own).
- `lib\` next to `WimSmith.exe` holds the third-party DLL `libwim-15.dll` ([wimlib](https://wimlib.net), x64). When it is missing it is **downloaded automatically in its newest version** when it is first needed (the zip of wimlib.net, unpacked by `src\unzip.cpp`: no 7-Zip or tar.exe needed; nothing is installed). `lib\versions.ini` records the version; a `libwim-15.dll` in the old `pe_tools\` folder is moved into `lib\` at start. Without an internet connection, put the file there by hand. It is not part of this repository; its license texts are kept next to it in `lib\` (see [Third-party programs](#third-party-programs)).
- Administrator rights for backup, restore and the disk tab (Windows asks each time); export, split / join, info, verify and browse do not need them.

## Build

```
build.bat
```

Compiles `src\*.cpp` into `WimSmith.exe` (x64) in this folder with the Visual C++ of Visual Studio 2017 or later (Community or Build Tools, workload "Desktop development with C++"; found with vswhere). The object files go to `obj\`. The language files `lang\*.ini` are embedded; an edited `lang\` folder next to the exe is used first.

wimlib stays a separate DLL loaded at run time (`lib\libwim-15.dll`).

The former C# / .NET Framework version (up to the C++ port) is kept in `backup\cs-winforms\`, with the `build_gui.bat` that builds it.

### Release

A GitHub release has the zip twice: `WimSmith-<version>-x64.zip` and the same file as **`WimSmith-x64.zip`** (`WimSmith.exe`, `lang\`, `LICENSE`, READMEs, manuals; no `lib\`). The PhoenixPE plugin downloads `releases/latest/download/WimSmith-x64.zip`, so that name must not change. Also attached: `phoenixpe\WimSmith.script`.

Release notes are in Korean first, with the English below in the same body: the first line is `[English](#user-content-english)`, the English part starts with `<a id="english"></a>` (a release page puts no anchors on headings), then `## English` and `###` headings.

### PhoenixPE plugin

`phoenixpe\WimSmith.script` (PEBakery script, LF, UTF-8 without BOM, like the scripts of PhoenixPE) puts WimSmith into a PhoenixPE build. Install it by copying it to `<PhoenixPE>\Projects\MyApps\Applications\` (on this PC: `F:\PE\PhoenixPE-2026.07.25-x64`). Source: the latest GitHub release or a local folder (`WimSmith.exe`, `lang\`, `lib\` when present); when there is no `lib\`, wimlib (version in the interface, `txt_WimlibVersion`) is downloaded from wimlib.net at build time and put into `lib\` with its license texts (`wimlib_COPYING*.txt`, `versions.ini`), because a PE often has no internet. It writes `WimSmith.ini` (`language` always set: the one chosen, or with "PE language" `ko` for a `%SourceLang%` of `ko-*`, else `en`; theme, `open_tab`), associates `.wim` / `.esd` / `.swm` (PhoenixAPI `Associate`) and adds the right-click menu "WimSmith >" under HKLM (`SystemFileAssociations`, the same items as `assoc::RegisterMenu`), its texts read with `IniRead` from `lang\<code>.ini` (section `ui`, the same keys as `RegisterMenus`), so the script stays ASCII. PhoenixAPI `Associate` writes the exe as `DefaultIcon`; for a WimSmith of 1.2 or later (its `lang\en.ini` has `rs.drivers`) the section `SetFileIcon` writes the file icons (`"<exe>",-2 / -3 / -4`) to `<ext>file\DefaultIcon`.

## Files

| Path | Content |
|---|---|
| `src\main.cpp` | entry point: the window, or `--wim-helper` |
| `src\app.*`, `src\mainwin.*` | what the tabs share (volumes, confirmations, running a job), the main window, footer and log window |
| `src\tab_*.*` | the tabs: restore, disk, backup, export, split / join, info, browse, "?" (`tab_about`: version, author, e-mail, web page, license text, wimlib) |
| `src\helper.*` | the elevated helper (job.ini -> status.txt): backup, restore, export, split / join, verify, bcdboot |
| `src\disk.*`, `src\storage.*` | disk tab: disks, partitions and volumes (IOCTLs of Windows, no PowerShell), clearing / initializing a disk, formatting (FormatEx), drive letters, the diskpart script, the disk job |
| `src\wim.*`, `src\wimread.cpp` | wimlib (`lib\libwim-15.dll`, loaded at run time), WIM file and image information |
| `src\libs.*`, `src\unzip.*` | `lib\` folder: moving and downloading wimlib (WinHTTP), license texts; reading the zip (stored / Deflate, CRC-32) |
| `src\ui.*` | the window toolkit on Win32: dark controls, tabs, lists, dialogs, row layout |
| `src\lang.*`, `src\settings.*`, `src\util.cpp`, `src\common.h` | language files, `WimSmith.ini`, text / file / process helpers |
| `src\assoc.*` | `.wim` / `.esd` / `.swm` opened with WimSmith for the current user (registry under HKCU, no elevation) |
| `src\makewim.*` | "Make a WIM file" of the folder menu: one window for all selected folders, without the main window |
| `src\extractwim.*` | Extract / Extract each of the WIM file menu: new folders next to the files, without elevation |
| `src\gather.*` | the files / folders of one click in Explorer's menu gathered into the first process (mutex + WM_COPYDATA) |
| `src\version.h` | the version (1.3.1), author, e-mail, web page and the copyright / license line: the version resource of the exe (file properties -> Details), the window title, the first log line and the "?" tab |
| `app.rc`, `app.manifest` | icon (1 = program, 2 / 3 / 4 = .wim / .esd / .swm files, `icons\`), version resource, `LICENSE` (resource type TEXT, shown on the "?" tab) and embedded language files; manifest (DPI, Common Controls 6, Windows versions) |
| `lang\en.ini`, `lang\ko.ini` | texts (en.ini is the reference) |
| `phoenixpe\WimSmith.script` | the PhoenixPE plugin (see [PhoenixPE plugin](#phoenixpe-plugin)) |

The elevated helper works in `%TEMP%\WimSmith_job\<time>` (job.ini, status.txt, wimlib_errors.txt); the folder is deleted after a successful job and kept after a failed one.

## Working on it

- Source files use CRLF line ends; `sed -i` turns them into LF, so fix with `unix2dos` or edit with a script that keeps CRLF. The language files are UTF-8 with BOM.
- `lang\en.ini` is the reference: add every new text there and in `lang\ko.ini`. Hint texts under the start buttons were removed on purpose; explanations go into the manual.
- Screen readers (MSAA): a field is named after the `Static` just before it in the z order, so `Grid::Add` moves each row label before its first control and names a later field of the row (after a check box) with `AccName`; a control without a label before it gets `AccName`. The check boxes (`WimSmithCheck`) and the progress line give their role / state / value through Dynamic Annotation (`AccPropServices`, created through COM: the exe does not import `oleacc.dll`), the tab strip has its own `IAccessible` (`TabsAccessible`, `WM_GETOBJECT`, `oleacc.dll` loaded at run time). Links (`ui::Link`) are owner-drawn buttons (kind 3), so they are tab stops. A destructive confirmation uses `ui::Confirm` (task dialog, buttons named by the action, *Cancel* the default; `TaskDialogIndirect` looked up at run time, Yes / No box without it). Ctrl+Tab / Ctrl+PgDn switch the tabs (`TabKeys` in the message loop).
- Popup menus go through `ui::PopupMenu` (owner-drawn items, menu background, the frame of the #32768 window): the menu of Windows stays light in the dark theme. Do not call `TrackPopupMenu` directly.
- No PowerShell at run time (it is missing in many WinPE images): disks, volumes and formatting go through `src\storage.*`.
- Elevated jobs cannot see the session's network or SUBST drives: paths given to the helper go through `SessionFreePath`.
- Small screens (WinPE without a video driver, 640 x 480): every tab page sits in a scrolling host (`ui::ScrollHost`, minimum 828 x 490, lower for browse / info with `SetScrollMin`); the main window starts maximized when 900 x 600 does not fit, windows and dialogs are kept on the screen (`FitToWorkArea`), the tab strip and the footer get narrower. A new tab or dialog must keep working at that size: lay dialogs out from their client size, not from fixed coordinates.
- When `WimSmith.exe` is running, `build.bat` cannot replace it: close it first, or build to another folder.
- Testing: use a copy of the exe in a scratch folder with its own `WimSmith.ini`; never edit the user's ini, never close the user's running instance, and do not start elevated jobs from a test (the UAC prompt appears on the user's desktop). The helper can be run directly (`WimSmith.exe --wim-helper <folder with job.ini>`) for jobs without elevation, such as a folder backup or verify.
- Learnings from past sessions are in `.trail/learnings/`.

## License

MIT, see [LICENSE](LICENSE). This applies to the code of this repository only.

### Third-party programs

WimSmith uses one separate program. Its code is not part of this repository, and it keeps its own license:

| Program | Files in `lib\` | License | License texts in `lib\` | Source |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | LGPL v2.1 or later for the library on Windows (or GPL v3 or later); includes libdivsufsort-lite (MIT) | `wimlib_COPYING.txt`, `wimlib_COPYING.LGPL.txt`, `wimlib_COPYING.GPLv3.txt`, `wimlib_COPYING.libdivsufsort-lite.txt` | https://wimlib.net/ |

How WimSmith uses them:

- wimlib is loaded as a separate DLL (`libwim-15.dll`) and called through its public API. It is not modified, and it can be replaced by another version by replacing the DLL in `lib\`.
- The repository does not contain it. WimSmith downloads it from wimlib.net on the user's PC, together with its license texts. When the DLL is there without its license texts (for example a `libwim-15.dll` moved from `pe_tools\`), WimSmith downloads only the texts in the background at start.

When you pass on WimSmith **together with** `lib\` (for example in a zip file):

- keep the license texts listed above in `lib\`;
- say that this file is wimlib under its own license, with the links to their sources above (the version of wimlib is in `lib\versions.ini` when WimSmith downloaded it);
- do not change the DLL; if you do, the changed source has to be published under the same license.
