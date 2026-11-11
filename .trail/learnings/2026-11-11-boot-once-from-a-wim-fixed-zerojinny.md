---
title: "Boot once from a WIM: fixed BCD ids, boot.sdi next to the WIM, boot index set first"
author: Zerojinny
date: 2026-11-11
branch: dev-1.2
cost: { minutes: 8, tokens: 191286 }
id: l-d51a475d
scope: repo
status: active
src: agent
touches:
  - src/settings.h
  - src/settings.cpp
  - src/app.cpp
  - src/app.h
  - src/mainwin.cpp
  - lang/en.ini
  - lang/ko.ini
  - src/helper.cpp
  - src/assoc.cpp
  - src/assoc.h
---

## Decided
- The one-time RAM-disk boot uses two BCD objects with fixed GUIDs (deleted and created again each time) instead of parsing `bcdedit /enum`: bcdedit output is localized, and fixed ids mean there is never more than one leftover entry. The loader is not put into the display order, only into `/bootsequence`, so nothing shows in the boot menu afterwards.
- boot.sdi is copied next to the WIM file (when missing) rather than pointing at the system drive: in Windows PE the system drive X: is itself a RAM disk the boot manager cannot read, and a BitLocker C: is not readable either. One drive (the WIM's) then has to be readable at boot.
- A RAM disk starts the WIM's boot index, not any image: the user chose that WimSmith sets the boot index to the chosen image (file rewritten in place) after a confirmation, rather than refusing.
- After success it asks "restart now?" (user's choice), not the "when done" countdown.
- Deleting the copied boot.sdi afterwards (setting ramboot.delete_sdi): a plain RunOnce would delete it at a logoff / logon before the restart and break the WIM start, so a Run value carries the boot time and deletes only once the boot time has changed (a fast startup keeps it, like the pending bootsequence). The user's HKCU Run, not HKLM: the GUI part is not elevated.
- The user asked that the menu item open no WimSmith window: main.cpp runs RunOneTimeBoot (ramboot.cpp) before the App is made, so it does not go through App::StartHelper / the log; it starts and waits for the elevated helper itself.

## Watch out
- The bcdedit part needs elevation and was not run from a test (no elevated jobs from tests). Running the helper unelevated reaches bcdedit and fails with access denied, which tests everything before it and the cleanup path.
- Many WinPE boot.wim files have boot index 0 (e.g. a PhoenixPE/XPE build), so set_boot is the common case.

In the code: helper.cpp RamBoot / RamLoaderId / RamOptionsId; ramboot.cpp RunOneTimeBoot, ChooseBootImage, RunElevated, RunShutdown; mainwin.cpp WimMenu (item ramboot); assoc::MenuItemCount re-registers an older menu at start.
