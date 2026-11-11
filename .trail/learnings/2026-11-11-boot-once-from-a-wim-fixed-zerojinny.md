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
- After success the GUI asks "restart now?" (user's choice), not the "when done" countdown.

## Watch out
- The bcdedit part needs elevation and was not run from a test (no elevated jobs from tests). Running the helper unelevated reaches bcdedit and fails with access denied, which tests everything before it and the cleanup path.
- Many WinPE boot.wim files have boot index 0 (e.g. a PhoenixPE/XPE build), so set_boot is the common case.

In the code: helper.cpp RamBoot / RamLoaderId / RamOptionsId; app.cpp App::OneTimeBoot, ChooseBootImage, App::Shutdown; mainwin.cpp WimMenu (item ramboot); assoc::MenuItemCount re-registers an older menu at start.
