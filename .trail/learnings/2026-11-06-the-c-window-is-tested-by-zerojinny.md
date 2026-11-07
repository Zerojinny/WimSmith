---
title: "The C++ window is tested by driving its real Win32 controls, from a separate test folder"
author: Zerojinny
date: 2026-11-06
branch: master
cost: { minutes: 23, tokens: 346033 }
id: l-c7a189c5
scope: repo
status: active
src: agent
touches:
  - README.ko.md
  - README.md
  - src/app.cpp
  - src/app.h
  - src/mainwin.cpp
  - src/mainwin.h
  - src/tab_disk.cpp
  - src/tab_disk.h
  - src/ui.cpp
  - src/ui.h
---

## Decided
- The C++ window is checked by starting a copy of the exe (7z_wimlib.exe; 7z_wimlib_native.exe before the switch) in a scratch folder with its own 7z_wimlib.ini, lib\ and image_folder (never the user's ini), then sending messages to its controls (EnumChildWindows by class, CB_SETCURSEL + WM_COMMAND/CBN_SELCHANGE to the parent, WM_LBUTTONDOWN on the SevenWimTabs strip (DvtTabs before 2026-11-07)) and taking a screenshot. Unlike the C# window, these are real HWNDs.
- Jobs that elevate (capture, apply, disk) are not started in tests: the UAC prompt would land on the user's desktop.

## Watch out
- The footer's combo boxes (after done, language) are created before the tabs, so they come first in the child order: index 1 is the language box, and selecting it switches the language (the window is rebuilt and the old HWND dies).
- An editable combo box gets the colors of its edit part itself (WM_CTLCOLOREDIT/STATIC go to the combo, not the page): ui.cpp subclasses it, else a disabled one is white with blue selected text.
- Disabled fields look grey in a scaled screenshot but are #202328; check pixels before "fixing" them.
- PowerShell 5.1 test scripts with Korean text need a UTF-8 BOM.
- Modal dialogs (the own file dialog, prompts): open them with PostMessage(WM_COMMAND) to the button's parent, never SendMessage (it blocks until the dialog closes). PrintWindow(…,2) sometimes misses the last painted owner-drawn button (Cancel looked missing); check with Graphics.CopyFromScreen before calling it a bug. Piping a test script into Select-Object -First stops the script early and leaves the test copy open.
