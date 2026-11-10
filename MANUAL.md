# WimSmith User Manual

WimSmith is a Windows tool that backs up volumes or folders into WIM images and restores WIM images to volumes or folders. It can also prepare disks, export images, split and join WIM files, verify them, and browse the files inside an image. Everything is done with [wimlib](https://wimlib.net).

[한국어](MANUAL.ko.md)

## Contents

1. [Getting started](#1-getting-started)
2. [The window](#2-the-window)
3. [Restore (apply a WIM)](#3-restore-apply-a-wim)
4. [Disk (partitions)](#4-disk-partitions)
5. [Backup (save as WIM)](#5-backup-save-as-wim)
6. [Export](#6-export)
7. [Split / Join](#7-split--join)
8. [Info and Verify](#8-info-and-verify)
9. [Browse](#9-browse)
10. [Common tasks](#10-common-tasks)
11. [Settings file and logs](#11-settings-file-and-logs)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Getting started

### Requirements

- Windows 8.1 / 10 / 11 x64, or x64 Windows PE. No runtime (.NET or other) needs to be installed.
- Backup, restore and disk jobs need administrator rights; Windows asks for them each time. Export, split / join, info, verify and browse do not.

### What is in the package

| File | Description |
|---|---|
| `WimSmith.exe` | the program (a single exe) |
| `lib\` | wimlib (`libwim-15.dll`) and its license texts |
| `WimSmith.ini` | the settings; created on the first run |

When a file is missing from `lib\`, its newest version is downloaded the first time it is needed. To use the program without internet (Windows PE, for example), copy a filled `lib\` folder along with it.

### Using it in Windows PE

- Copy `WimSmith.exe` and the `lib\` folder to a USB drive and run it from there.
- Explorer is not needed: the file and folder dialogs are drawn by the program itself.
- WIM files on a network drive (`net use Z: \\server\share`) work too.
- On a small screen without a video driver (640 × 480, 800 × 600, …) the window opens maximized.
  - A tab larger than the screen gets scroll bars: the mouse wheel scrolls up and down, Shift + wheel left and right.
  - Moving to a field with Tab scrolls it into view.
  - The footer (progress, Cancel) always stays visible.
- For a higher resolution, set `bcdedit /store <BCD file> /set {default} highestmode on` on the PE boot entry: it boots in the highest mode the firmware offers.

---

## 2. The window

The tabs are at the top. The footer at the bottom has:

| Item | Description |
|---|---|
| State | phase, progress and elapsed time of the running job |
| Last log line | click it to open the log window |
| Show log | opens the log window (copy, clear) |
| When done | *Do nothing / Shut down / Restart* when the job ends; shut down and restart can be cancelled during a 60-second countdown |
| Cancel | stops the running job at wimlib's next progress step |
| Settings | opens the settings (below) |
| Language | 한국어 / English |

**[Settings]**:

| Setting | Description |
|---|---|
| Theme | Dark / Light; the window is rebuilt in the new colors |
| Open with WimSmith | `.wim`, `.esd`, `.swm`, each on its own: a double-click on such a file in Explorer starts WimSmith with it. These files get their own icons (a page with the layers and the extension: WIM teal, ESD violet, SWM orange). Set for your Windows user only, no administrator rights; clear a box to undo it (the program used before comes back) |
| Open a WIM file in | the tab a WIM / ESD / SWM file opens in: Browse (default), Info, Restore, Add an image (the backup tab with the file as its target and *Add a new image*), Export or Split / Join. Also for a file opened with *Open with* or dropped on `WimSmith.exe` |
| Right-click menu | adds a **WimSmith ▸** submenu to the right-click menu of `.wim`, `.esd` and `.swm` files in Explorer, whichever program opens them: *Restore*, *Add an image* (.wim only), *Export*, *Split / Join* (.wim: split, .swm: join), *Info*, *Browse*. On Windows 11 it is under *Show more options*. Its texts follow the language of WimSmith |
| DISM (drivers) | the DISM that adds drivers on the restore tab: **DISM of Windows** (`System32`) or **dism.exe in this folder** (e.g. the `DISM` folder of the Windows ADK; a relative folder is read against `WimSmith.exe`) |
| Restore: path | the restore tab remembers its last WIM file; with this on, it is saved relative to `WimSmith.exe` (e.g. `WIM\pc.wim` or `..\Backup\pc.wim`) when it is on the same drive, so a WimSmith on a USB drive still finds it when the drive gets another letter. Off by default; other drives and network paths stay full paths |

When another program was chosen for one of these extensions in Windows before, Windows keeps it, and WimSmith says so: right-click such a file > *Open with* > *Choose another app*, select WimSmith and *Always*. If `WimSmith.exe` is moved to another folder, its boxes are shown cleared: check them again.

Language and theme cannot be changed while a job runs. Closing the window during a job leaves the job running, but its progress can no longer be seen.

The last tab, **?**, shows the version, the author, the e-mail address (click it to write a mail, or [Copy]), the web page, the license (MIT, with its full text) and the wimlib in `lib\` with its license. Without a browser or mail program (Windows PE) a clicked address is copied to the clipboard.

Every start button asks for confirmation first. Formatting a volume and clearing a disk ask a second time, and you must type the drive letter or disk number.

---

## 3. Restore (apply a WIM)

Applies one image of a WIM file to a volume or to an empty folder.

| Item | Description |
|---|---|
| WIM file | `.wim`, `.esd` or `.swm` (split WIM); [Read images] lists its images |
| Image | the image to restore |
| Restore to | a volume from the list, or an empty folder through [Folder...] |
| New volume label | the label given when formatting |
| UEFI/BIOS/None | which boot files to write afterwards, and the boot manager language (default `ko-kr`) |
| System partition | where the boot files go |
| Compression | **Off**, or **Compact OS** XPRESS 4K / 8K / 16K / LZX: the restored files are compressed by Windows (see below). The last choice is kept |
| Drivers | **Add with DISM**: after the restore, DISM adds the drivers of a folder to the restored Windows (see below). The folder is kept, the box is not |
| Integrity | check the WIM file against its integrity table first (`--check`); a damaged file stops the job before anything is formatted. Off by default |

### To a volume

- The volume is **quick-formatted as NTFS**, then the image is applied to its root. All data on the volume is lost.
- Not listed: the drive of the running Windows, the drive of WimSmith and the drive of the WIM file.
- A volume smaller than the image is refused.

### Boot files

- With **UEFI** and/or **BIOS** checked, `bcdboot` writes the boot files to the chosen system partition after the image is applied.
- A system partition without a drive letter gets one for that time and loses it again afterwards.
- UEFI boot files can only go to a FAT32 partition (the EFI system partition).
- On an MBR disk without a System Reserved partition, the restored partition itself is the system partition, and it is made **active** automatically.
- **None** writes no boot files; you can run `bcdboot X:\Windows` yourself later.

### Compact OS (compressed restore)

The files are written with the *System Compression* of Windows 10 (`wimapply --compact`): a Windows volume usually needs several GB less, which helps on small SSDs and eMMC tablets. XPRESS 4K is fast and recommended; LZX is the smallest and the slowest.

- The running Windows or WinPE must be Windows 10 or later (without it the box stays at Off), and the restored Windows must be Windows 10 or later too.
- Files that Windows needs to start are left uncompressed by wimlib.
- The size check before the restore is skipped: the compressed image may fit a volume smaller than its size.
- Files written later by Windows are not compressed; files changed later are written uncompressed again.

### Drivers (DISM)

With **Add with DISM** checked, DISM adds the drivers of the chosen folder (`.inf` files, subfolders included) to the restored Windows right after the image is applied, before the boot files are written (`dism /Image:<target> /Add-Driver /Driver:<folder> /Recurse`). Use it for storage, network or chipset drivers that the restored Windows needs on this PC.

- It works for a volume and for a folder target; the image must contain Windows.
- Which DISM is used is chosen in [Settings]: the DISM of Windows (`System32\dism.exe`, also in Windows PE) or `dism.exe` in a folder, e.g. the `DISM` folder of the Windows ADK (`...\Deployment Tools\amd64\DISM`). A DISM older than the restored Windows may fail: then use the one of the ADK. A relative folder is read against the folder of `WimSmith.exe`.
- When DISM fails, the image stays applied and the boot files are still written; the job ends with the DISM error, and DISM's log is kept as `dism.log` in the job folder.

### To a folder

Choose an empty folder with [Folder...]: nothing is formatted and the image is extracted into that folder, without boot files. A drive root, a folder overlapping the running Windows or WimSmith, and a folder that is not empty cannot be chosen.

### If a restore is cancelled

The volume is left incomplete: format it or restore again. For a folder, empty it and restore again.

---

## 4. Disk (partitions)

**Clears a whole disk**, initializes it as GPT or MBR and creates partitions. Every partition and all data on that disk are lost.

| Item | Description |
|---|---|
| Disk | the disk to clear. The disk of the running Windows, the disk of WimSmith, and read-only or offline disks cannot be chosen |
| Partition style | **GPT (UEFI)** or **MBR (BIOS / legacy)**; MBR can use 2 TB at most |
| Layout | see the table below |
| System partition | size in MB; on GPT the *MSR (16 MB)* box, on MBR the *Create* box for the System Reserved partition |
| Recovery partition | when checked, an NTFS recovery partition at the end of the disk (default 1024 MB) |
| Windows label | the volume label of the Windows partition |
| Data partition | Windows share (%), file system (NTFS / exFAT / FAT32), label |
| Partitions | a preview of the result |

### Layouts

| Layout | GPT | MBR |
|---|---|---|
| For Windows | EFI system (FAT32, 260 MB) + MSR (16 MB) + Windows (NTFS) | System Reserved (NTFS, 500 MB, active) + Windows (NTFS) |
| Windows + data | the same + a data partition | the same + a data partition |
| One data partition | the whole disk as one partition | the whole disk as one partition |

- **Windows + data**: of the remaining space, Windows gets the chosen share in %, the data partition the rest.
- **Without MSR**: Windows boots, but Windows Setup and some disk tools expect an MSR on a GPT disk.
- **MBR without System Reserved**: the Windows partition becomes the active partition and also holds the BIOS boot files.
- A FAT32 data partition can be at most 32 GB.

### Next step

A layout with Windows goes on to the restore tab by itself, with the new Windows partition as the target and the new system partition and boot type (UEFI for GPT, BIOS for MBR) already set: just choose the WIM file and the image. A data partition gets the next free drive letter.

---

## 5. Backup (save as WIM)

Saves a volume or folder as an image in a WIM file. The **job preview** below the options shows source, target, method, rights and verification.

| Item | Description |
|---|---|
| Source | a volume from the list, or a folder through [Folder...] |
| WIM file | the `.wim` file to write |
| Image name / description | name and description inside the WIM; the name must be unique in the file |
| Compression | **LZX** (default: fair size, fast restore), **XPRESS** (fastest, larger file), **LZMS solid** (smallest file, slow); threads 0 = automatic |
| Existing file | **Replace the file**: a new file is written and replaces the old one at the end (the old one stays until then). **Add a new image**: the image is appended to the existing file (with that file's compression). **Fast**: see below |
| Exclude | *Exclude these paths* and [List...]: leave out more paths (see below) |
| Snapshot | *Back up from a VSS snapshot* |
| Verify | read the new file once more and verify it after the backup |
| Integrity | add an integrity table to the WIM file (wimlib `--check`); when adding to an existing file, that file is checked against its integrity table first. Off by default |

### Backing up the running Windows

- The running Windows (its system drive or its Windows folder) can only be saved with **VSS snapshot** on. The snapshot is made at the start and deleted at the end; files in use are saved as they were at that moment.
- Without a snapshot, connect the source PC's disk to another PC, or boot Windows PE and back up from there.
- The WIM file must not be inside the source; with a snapshot, put it on another drive.
- `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `System Volume Information`, `$Recycle.Bin` and the like are left out (wimlib's default exclusion list for Windows).

### Fast backup into an existing file

With **Add a new image** and **Fast: unchanged files from the last image** (on by default), files whose size and times are the same as in the last image of the file are not read again: their data is taken from that image (wimlib `--update-of`). Backing up the same drive into the same file again is then much faster. Changed and new files are read as usual.

The comparison is by size and times only, as wimlib does it. A program that changes a file and restores its times would not be noticed; turn the box off for such a case.

### Exclusions

wimlib always leaves out `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `System Volume Information`, `$Recycle.Bin` and `\Windows\CSC`. With **Exclude these paths** checked, the paths of the list are left out too. [List...] edits the list, one path per line:

- paths start at the root of the source: `\Users\*\Downloads\*` (for a volume that is `X:\Users\...`; for a folder source, inside that folder)
- `*` and `?` are wildcards, also in the middle of a path
- `\folder\*` leaves the empty folder in the image; `\folder` leaves it out altogether
- a name without `\` (e.g. `*.tmp`) matches in every folder

[Recommended list] fills in data Windows makes again by itself: `\Windows\Temp`, the Windows Update download cache, `\Windows\Prefetch`, error reports, the users' temporary folders and web caches. The list is kept in `WimSmith.ini`.

### Verify and the integrity table

- **Verify** checks all data against its SHA-1 hashes right after the backup. The file is read once more, so it takes longer.
- The **integrity table** holds a hash of every 10 MB of the file; later verifications check it too. Writing it takes extra time.
- The integrity option is also on the restore, export and split / join tabs. A file with an integrity table is checked against it before the job (a file without one is used as it is), and a newly written file gets a table. A damaged file stops the job with "failed integrity check", and no result file is made.

---

## 6. Export

Copies images of a WIM / ESD file into another WIM file. No administrator rights needed.

- change the compression (for example LZMS → LZX)
- convert ESD → WIM
- gather images of several files into one (**Add the image(s)**)
- rename an image or change its description (only when exporting one image; empty = those of the source)

With **Integrity** checked, the source is checked against its integrity table and the target gets one (`--check`).

Export one image or *all*. The bootable-image mark is kept. If the target already has an image with the same name, export that image alone under another name.

---

## 7. Split / Join

No administrator rights needed.

### Split (WIM → SWM parts)

- Splits a WIM file into `.swm` parts of a given size in MB, named `<name>.swm`, `<name>2.swm`, `<name>3.swm`, …
- For a FAT32 USB drive use **4000 MB or less**. A part holding a single file larger than the part size gets bigger than that.
- With **Integrity** checked, the WIM file is checked against its integrity table and every part gets one (`--check`).
- Files with solid resources (ESD, LZMS solid) cannot be split: export them as LZX or XPRESS on the export tab first, then split the new file.

### Join (SWM parts → WIM)

- Choose any part: the other parts in the same folder are found by themselves.
- All parts must be in the same folder under their original names.
- With **Integrity** checked, the parts are checked against their integrity tables and the joined file gets one (`--check`).

A split WIM can be used as it is, without joining, for restore, export, info, verify and browse.

---

## 8. Info and Verify

### Info

Shows the WIM file's image count, compression, size, boot image, GUID and flags (such as the integrity table), and for each image its edition, architecture, version, languages, size, file count and dates.

- [Copy all]: copies the information to the clipboard
- [Restore this image] / [Export this image] / [Browse this image]: takes the selected image to that tab

### Manage

[Manage ▾] changes the WIM file:

| Command | What it does |
|---|---|
| Image info... | the name, description and edition (`FLAGS`, e.g. Professional) of the selected image, and whether it is the boot image |
| Mark as the boot image / Remove the boot image mark | the image Windows Setup or WinPE starts from a boot WIM (`boot.wim`) |
| Delete this image... | deletes the selected image; the images after it are numbered again. The only image of a file cannot be deleted |
| Optimize the file... | writes the file again without the data no image uses any more: data left by files deleted on the browse tab, by deleted images and by cancelled jobs. The file becomes smaller |

- Image info and the boot mark are written at once (only the header and the image list change).
- Deleting and optimizing write the whole file again next to the old one and then replace it: this takes about as long as an export, needs as much free space on its drive as the file's size, and can be cancelled (the old file stays).
- A split WIM and a read-only file cannot be changed; the menu says why. For a file in a place a normal user may not write (the root of `C:\`, `Program Files`), Windows asks for administrator rights.
- The other tabs showing the file read it again afterwards.

### Verify

[Verify] reads all data of a WIM / ESD / SWM file and checks it against the SHA-1 hashes, together with the image metadata, and checks the integrity table when there is one. The file is not changed. Do not restore from a file found damaged.

---

## 9. Browse

Shows the files inside an image, read with wimlib (a split WIM too: choose any part). Administrator rights are not needed. Viewing and extracting do not change the WIM file; only the edit commands below do.

- Folder tree on the left, file list on the right; [Up] goes to the parent folder.
- [Extract selected...]: extracts the selected files / folders to a folder of your choice; asks before overwriting.
- [Open file]: extracts the file to a temporary folder and opens it with its program. In Windows PE without Explorer, text files open in Notepad, and for other files without a program the extracted location is shown.
- Programs (.exe and the like) are not started: their location is shown in Explorer, or as a path without Explorer.
- [Cancel] stops listing, extracting or editing; a cancelled extraction leaves nothing in the target folder.

### Editing (changing the files of the image)

The [Edit ▾] button, the **right-click menu** of the list or the folder tree, and the keys change the image shown.

| Command | Key | What it does |
|---|---|---|
| New folder... | | an empty folder in the current folder (in the tree: in that folder) |
| Add files... | | files of the PC into the current folder; files of the same name are replaced after a question |
| Add a folder... | | a whole folder of the PC; a folder of the same name is merged after a question |
| Rename... | F2 | the one selected file or folder |
| Delete | Del | the selected files and folders (folders with their content), after a question |
| Refresh | F5 | reads the list again |

- Every command writes the WIM file right away. There is no undo: copy important files first.
- The new content is appended to the file, so a cancelled or failed command leaves the WIM file usable as it was. Deleting does not make the file smaller: use [Manage ▾] → *Optimize the file...* on the info tab for that.
- Added files get no access rights (ACLs): a restore gives them the rights of their place.
- Usually no administrator rights are needed. For a WIM file where a normal user may not write (the root of `C:\`, `Program Files`, …) Windows asks for them, and the change is done with them; the menu then says "Changes ask for administrator rights", and the progress and [Cancel] are in the footer.
- A WIM file another program holds open (7-Zip, …) cannot be changed (wimlib error 47): close that program and try again.
- A split WIM (.swm) and a read-only file cannot be changed: the edit commands are grey in the menu, with the reason. Join a split WIM first.
- Other tabs showing the same WIM file read its images again after an edit.

---

## 10. Common tasks

### Install Windows on a new disk (UEFI)

1. **Disk** tab: choose the disk, *GPT (UEFI)* and *For Windows*, then [Clear disk and create partitions].
2. The **Restore** tab opens. Choose the WIM file (for example `sources\install.wim` or `install.esd` of Windows setup media), [Read images], and pick the edition.
3. [Format and restore]. When it is done, the disk can boot.

### Older PCs (BIOS)

As above, with *MBR (BIOS / legacy)* in step 1. The System Reserved partition can be left out (uncheck *Create*).

### Back up the current Windows

On the **Backup** tab choose the system drive as the source and turn on *Back up from a VSS snapshot*. Put the WIM file on another drive (external disk, network drive, …).

### A large WIM on a FAT32 USB drive

On the **Split / Join** tab split it into 4000 MB parts and copy all `.swm` parts to the USB drive. On the restore tab choose the first part and restore as usual.

---

## 11. Settings file and logs

### WimSmith.ini

Next to `WimSmith.exe`. Values changed in the window are saved here; it can also be edited by hand while the program is closed.

| Key | Value |
|---|---|
| `language` | `ko`, `en` |
| `theme` | `dark` (default), `light` |
| `open_tab` | the tab a WIM file opens in: `browse` (default), `info`, `restore`, `append`, `export`, `splitjoin` |
| `image_folder` | the folder of the last WIM file backed up to |
| `restore.wim` | the last WIM file of the restore tab (relative to `WimSmith.exe` with *Restore: path* on) |
| `relative_paths` | `1`: *Restore: path* in [Settings] is on |
| `restore.boot_lang` | boot manager language (default `ko-kr`) |
| `backup.snapshot` | the VSS snapshot box |
| `backup.update_of` | the *Fast* box of the backup tab (default on) |
| `backup.exclude_on`, `backup.exclude` | the *Exclude these paths* box and the list (paths separated by `\|`) |
| `restore.compact` | the compression of the restore tab: 0 = off, 1-4 = XPRESS 4K / 8K / 16K / LZX |
| `restore.drivers` | the driver folder of the restore tab |
| `dism`, `dism.folder` | the DISM for drivers: `system` or `folder`, and that folder |
| `disk.*` | the last choices of the disk tab (style, layout, sizes, labels, …) |

Verify and integrity table are not saved; they are off at every start.

### Logs

- [Show log] in the footer shows the log of this session.
- Jobs with administrator rights write their log files to `%TEMP%\WimSmith_job\<time>\` (`status.txt`, `wimlib_errors.txt`). They are deleted after a success and kept after a failure or cancel.

### Language files

A `lang\` folder next to `WimSmith.exe` with `ko.ini` / `en.ini` is read before the copies built into the exe: texts can be changed and languages added there.

---

## 12. Troubleshooting

| Problem | Cause and fix |
|---|---|
| "libwim-15.dll cannot be used" | wimlib is missing from `lib\` and could not be downloaded. Put `libwim-15.dll` of the wimlib Windows release (x64) into `lib\` |
| The drivers were not added | DISM ended with an error (its code is in the log; `dism.log` is in the job folder). A DISM older than the restored Windows: choose the DISM of the Windows ADK in [Settings] |
| Administrator request cancelled | backup, restore and disk jobs need administrator rights; start again and answer Yes |
| The running Windows cannot be backed up | turn on *Back up from a VSS snapshot*, or back up from Windows PE |
| A volume is missing from the restore targets | the drives of the running Windows, of WimSmith and of the WIM file are not listed; move the WIM file or choose another target |
| A WIM on a network drive is not found | elevated jobs cannot see that drive letter, so the program uses the `\\server\share` path instead; if it still fails, check the credentials for the share |
| No boot after a restore | check that the boot type (UEFI / BIOS) matches the PC's firmware setting and that the right system partition was chosen; after a restore with None, run `bcdboot X:\Windows` |
| Split is refused | ESD / LZMS solid files cannot be split; export them as LZX first |
| Deleting an image or optimizing stops with "free" | the drive of the WIM file needs as much free space as the file, for the new copy |
| Compact OS stays at Off | the running Windows / WinPE is older than Windows 10 |
| The disk is offline | bring it online first in Disk Management (or `online disk` in `diskpart`) |

---

WimSmith is under the MIT License (see the **?** tab or `LICENSE`). WimSmith uses wimlib (LGPL v2.1 or later, or GPL v3 or later) as a separate DLL. Its license texts are in the `lib\` folder.
