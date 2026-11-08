# WimSmith

A Windows tool that backs up volumes or folders into WIM images, and restores WIM images to volumes or folders. It is a single exe that needs no installation, and it runs in Windows PE too.

[한국어](README.ko.md) · User manual: [MANUAL.md](MANUAL.md)

## What it does

- **Backup**: saves a volume or folder to a WIM file. A VSS snapshot lets you back up the running Windows. A new image of an existing file takes the unchanged files from the last image (fast), and your own exclusion list leaves out temporary files and caches. You can verify the file afterwards and add an integrity table (`--check`).
- **Restore**: applies a WIM / ESD / SWM image to a volume (formatted first) or to an empty folder, and writes UEFI / BIOS boot files. With Compact OS the files are compressed by Windows 10 (several GB less).
- **Disk**: initializes a disk as GPT or MBR and creates the partitions for Windows.
- **Export**: changes the compression, converts ESD to WIM, and gathers several WIM files into one.
- **Split / Join**: splits a WIM into `.swm` parts that fit on a FAT32 USB drive, and joins them again.
- **Info / Verify**: shows WIM file and image details, and checks a file for damage. Changes the name, description and edition of an image and the boot image, deletes images, and optimizes the file (makes it smaller).
- **Browse**: shows the files inside an image and extracts the ones you need; adds, renames and deletes files in the image.

Korean and English UI, with a dark and a light theme.

## Requirements

- Windows 8.1 / 10 / 11 x64, or x64 Windows PE
- Administrator rights for backup, restore and disk jobs (Windows asks each time)

No runtime such as .NET is needed.

## Package contents

| File | Description |
|---|---|
| `WimSmith.exe` | the program |
| `lib\` | wimlib (`libwim-15.dll`) and its license texts |
| `MANUAL.md`, `MANUAL.ko.md` | user manual (English, Korean) |
| `README.md`, `README.ko.md` | this file |
| `LICENSE` | the license of WimSmith |

## Getting started

1. Unpack the package into any folder, a USB drive included. `WimSmith.exe` and the `lib\` folder must stay together.
2. Run `WimSmith.exe`.
3. Settings are saved in `WimSmith.ini` in the same folder.

When a file in `lib\` is missing, its newest version is downloaded the first time it is needed. To work without internet, keep the `lib\` folder and copy it along.

See the [user manual](MANUAL.md) for details.

## Caution

- **Restore** formats the target volume, and **Disk** clears the whole disk. Check the target before you start. Both ask twice; the second time you must type the drive letter or the disk number.
- The drive and disk of the running Windows, and the drive of WimSmith, cannot be chosen as a restore or disk target.

## License

The code of WimSmith is under the BSD 3-Clause license ([LICENSE](LICENSE)).

wimlib in `lib\` is not part of WimSmith and keeps its own license. WimSmith uses it unchanged, as a separate DLL; replace the DLL to use another version.

| Program | Files | License | License texts | Source |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | LGPL v2.1 or later (or GPL v3 or later); includes libdivsufsort-lite (MIT) | `wimlib_COPYING*.txt` | https://wimlib.net/ |

When you pass the package on, keep the license texts in `lib\`. The wimlib version is recorded in `lib\versions.ini`.

7-Zip is not used any more. A `lib\` of an older version may still hold `7z.exe`, `7z.dll`, `7zFM.exe` and `7-Zip_License.txt`: they can be deleted.

WimSmith was called **7z_wimlib** before. At its first start `WimSmith.exe` copies the settings of `7z_wimlib.ini` in its folder into `WimSmith.ini`; the old `7z_wimlib.exe` and `7z_wimlib.ini` can then be deleted.
