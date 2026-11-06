---
title: "C++ port: the wimlib zip is unpacked with 7-Zip or Windows tar, and dark lists draw their own selection"
author: Zerojinny
date: 2026-11-06
branch: master
cost: { minutes: 26, tokens: 279874 }
id: l-baaa50f8
scope: repo
status: active
src: agent
touches:
  - README.ko.md
  - README.md
  - build.bat
  - src/app.cpp
  - src/libs.cpp
  - src/libs.h
  - src/tab_browse.cpp
  - src/app.h
  - src/common.h
  - src/mainwin.cpp
---

## Decided
- Win32 has no zip API, so the C++ lib\ download unpacks the wimlib zip with lib\7z.exe when 7-Zip is there, else with %SystemRoot%\System32\tar.exe (Windows 10 1803+); when neither exists (WinPE) it downloads 7-Zip first (7-Zip itself comes as an installer that 7zr.exe unpacks, no zip). Writing an inflater was ruled out as too much code for one download.
- HTTP is WinHTTP (TLS 1.2/1.3 set explicitly), not WinINet: no IE settings, works in a service-like helper.

## Watch out
- A ListView with the DarkMode_Explorer theme ignores clrTextBk for the selected row (draws #1A3853 with a #0078D4 frame). ui.cpp draws selected rows cell by cell (CDRF_NOTIFYSUBITEMDRAW, icon included) and hides the focus frame (WM_CHANGEUISTATE).
- The header text of a dark ListView stays dark: its items are owner drawn (HDF_OWNERDRAW, WM_DRAWITEM goes to the list, so the list is subclassed).
- Screenshots for checks use PrintWindow(PW_RENDERFULLCONTENT): CopyFromScreen captured another program the user had open over the window.

In the code: src/libs.cpp (DownloadWimlib, Download7zip, RunDialog), src/ui.cpp (ListDraw, ListSub).
