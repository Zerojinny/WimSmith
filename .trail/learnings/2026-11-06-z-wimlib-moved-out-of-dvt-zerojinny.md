---
title: Z_wimlib moved out of DVT into its own repository; DVT keeps its own copy of the backup page
author: Zerojinny
date: 2026-11-06
cost: { minutes: 2, tokens: 19770 }
id: l-f1590ac0
scope: repo
status: active
src: agent
touches:
  - README.md
  - README.ko.md
---

## Decided
- Z_wimlib (the standalone backup/restore tool) is now a separate project in its own git repository next to DVT (F:\Sync\Z_wimlib), at the user's request; the Z_wimlib folder inside DVT was deleted.
- DVT keeps its own backup/restore page. The two copies are no longer linked: a fix in one is not in the other.

## Tried and ruled out
- Keeping Z_wimlib inside the DVT folder as a nested repository: dropped so the code lives in one place and DVT's .gitignore and build do not mix with it.

## Watch out
- When fixing the backup page here (wimlib calls, boot files, restore checks), check whether Z_wimlib needs the same fix, and the other way round.

In the code: `gui_cs/src/BackupPanel.cs`, `gui_cs/src/WimLib.cs` (DVT copy); Z_wimlib has `src/BackupPanel.cs`, `src/WimLib.cs` in its own repository.
