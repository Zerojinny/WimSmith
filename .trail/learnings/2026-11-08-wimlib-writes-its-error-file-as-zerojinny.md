---
title: "wimlib writes its error file as UTF-16 without a BOM, and error 47 on an edit means the WIM file is held open elsewhere"
author: Zerojinny
date: 2026-11-08
branch: master
cost: { minutes: 113, tokens: 1128710 }
id: l-0cb1e52d
scope: repo
status: active
src: agent
touches:
  - src/tab_browse.cpp
  - src/util.cpp
  - src/wim.cpp
  - src/wim.h
  - CLAUDE.md
  - MANUAL.ko.md
  - MANUAL.md
  - lang/en.ini
  - lang/ko.ini
  - src/filedlg.cpp
---

## Decided
- In the GUI process, wimlib's own messages are sent to a fresh file in %TEMP% per operation (wl::BeginMessages) and read back (wl::Messages) into the error box; wimlib keeps the file open until the next one is set, so each call uses a new name and deletes the one before.
- ReadTextFile detects UTF-16 without a BOM (most high bytes 0).

## Tried and ruled out
- A folder delete in the browse tab failing with error 47 was not a delete problem: deleting folders works on plain, solid (LZMS) and a real 682 MB Windows RE image. It was reproduced only by holding the WIM file open in another process (FileShare.Read): wimlib_overwrite then cannot open it for writing ("Permission denied").

## Watch out
- wimlib on Windows writes the error file as UTF-16LE without a BOM; reading it as UTF-8 cuts the text at the first NUL, so the message box showed nothing after the first line.
- "Open in 7-Zip" (7zFM) can keep the WIM file open while the browse tab tries to change it.

In the code: src/wim.cpp (BeginMessages, Messages), src/util.cpp (ReadTextFile), src/tab_browse.cpp (RunUpdate, RunExtract), lang key br.hint_open.
