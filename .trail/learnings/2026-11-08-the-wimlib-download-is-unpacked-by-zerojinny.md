---
title: "The wimlib download is unpacked by our own zip reader, not 7-Zip or tar.exe"
author: Zerojinny
date: 2026-11-08
branch: master
cost: { minutes: 1, tokens: 1609 }
id: l-c860e5ff
scope: repo
status: active
replaces: l-d4787451
src: agent
touches:
  - src/unzip.cpp
  - src/libs.cpp
---

## Decided
7-Zip was removed completely (the user asked: the browse tab reads with wimlib, [Open in 7-Zip] was the last use). The only other use was unpacking the wimlib zip on download (7z.exe of lib\, else Windows' tar.exe, else 7-Zip was downloaded first just for that). The user chose a small zip reader in the source over tar.exe only, so the automatic download keeps working on Windows 8.1 and in WinPE images without tar.exe. This reverses the earlier call that an inflater was too much code.

## Tried and ruled out
- tar.exe only: missing on Windows 8.1 and in many WinPE images; the download would fail there.
- Shell zip folders (CopyHere): need Explorer / the shell, not in WinPE.

## Watch out
- The reader handles only what wimlib's zip needs: top-folder entries, stored or Deflate, no ZIP64, no encryption; every file is checked with its CRC-32.
- Old lib\ folders may still hold 7z.exe / 7z.dll / 7zFM.exe / 7-Zip_License.txt and a "7zip =" line in versions.ini: nothing reads them now; the README says they can be deleted.

In the code: unzip::ExtractTop in src/unzip.cpp, used by DownloadWimlib in src/libs.cpp (the license-only path too). Tested: a full download into an empty lib\ (same DLL hash as before) and license texts only.
