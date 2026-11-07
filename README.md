# 7z_wimlib

A Windows tool that backs up volumes or folders into WIM images, and restores WIM images to volumes or folders. It is a single exe that needs no installation, and it runs in Windows PE too.

[한국어](README.ko.md) · User manual: [MANUAL.md](MANUAL.md)

## What it does

- **Backup**: saves a volume or folder to a WIM file. A VSS snapshot lets you back up the running Windows. You can verify the file afterwards and add an integrity table (`--check`).
- **Restore**: applies a WIM / ESD / SWM image to a volume (formatted first) or to an empty folder, and writes UEFI / BIOS boot files.
- **Disk**: initializes a disk as GPT or MBR and creates the partitions for Windows.
- **Export**: changes the compression, converts ESD to WIM, and gathers several WIM files into one.
- **Split / Join**: splits a WIM into `.swm` parts that fit on a FAT32 USB drive, and joins them again.
- **Info / Verify**: shows WIM file and image details, and checks a file for damage.
- **Browse**: shows the files inside an image and extracts the ones you need.

Korean and English UI, with a dark and a light theme.

## Requirements

- Windows 8.1 / 10 / 11 x64, or x64 Windows PE
- Administrator rights for backup, restore and disk jobs (Windows asks each time)

No runtime such as .NET is needed.

## Package contents

| File | Description |
|---|---|
| `7z_wimlib.exe` | the program |
| `lib\` | wimlib (`libwim-15.dll`) and 7-Zip (`7z.exe`, `7z.dll`, `7zFM.exe`) with their license texts |
| `MANUAL.md`, `MANUAL.ko.md` | user manual (English, Korean) |
| `README.md`, `README.ko.md` | this file |
| `LICENSE` | the license of 7z_wimlib |

## Getting started

1. Unpack the package into any folder, a USB drive included. `7z_wimlib.exe` and the `lib\` folder must stay together.
2. Run `7z_wimlib.exe`.
3. Settings are saved in `7z_wimlib.ini` in the same folder.

When a file in `lib\` is missing, its newest version is downloaded the first time it is needed. To work without internet, keep the `lib\` folder and copy it along.

See the [user manual](MANUAL.md) for details.

## Caution

- **Restore** formats the target volume, and **Disk** clears the whole disk. Check the target before you start. Both ask twice; the second time you must type the drive letter or the disk number.
- The drive and disk of the running Windows, and the drive of 7z_wimlib, cannot be chosen as a restore or disk target.

## License

The code of 7z_wimlib is under the BSD 3-Clause license ([LICENSE](LICENSE)).

The programs in `lib\` are not part of 7z_wimlib and keep their own licenses. 7z_wimlib uses them unchanged, as a separate DLL and a separate program; replace their files to use another version.

| Program | Files | License | License texts | Source |
|---|---|---|---|---|
| [wimlib](https://wimlib.net) | `libwim-15.dll` | LGPL v2.1 or later (or GPL v3 or later); includes libdivsufsort-lite (MIT) | `wimlib_COPYING*.txt` | https://wimlib.net/ |
| [7-Zip](https://www.7-zip.org) | `7z.exe`, `7z.dll`, `7zFM.exe` | GNU LGPL, parts BSD 3-Clause, unRAR restriction | `7-Zip_License.txt` | https://www.7-zip.org/ |

When you pass the package on, keep the license texts in `lib\`. The wimlib version is recorded in `lib\versions.ini`.
