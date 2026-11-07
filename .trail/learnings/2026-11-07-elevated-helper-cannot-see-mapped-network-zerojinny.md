---
title: "Elevated helper cannot see mapped network drives, so paths are passed as UNC"
author: Zerojinny
date: 2026-11-07
branch: master
cost: { minutes: 8, tokens: 98347 }
id: l-653d6f43
scope: repo
status: active
src: agent
touches:
  - build.bat
  - lang/en.ini
  - lang/ko.ini
  - src/app.cpp
  - src/common.h
  - src/helper.cpp
  - src/util.cpp
  - src/disk.cpp
  - src/disk.h
  - src/tab_disk.cpp
---

## Decided
- A WIM on a mapped network drive (Z: = \server\share) read fine in the GUI but the elevated helper said "Not found": with UAC the elevated token is another logon session, which has neither the mapped drives nor SUBST drives of the normal session. App::StartHelper turns the path keys (wim, source, folder, tools, root) and the helper exe path into \server\share\... (WNetGetConnection) or the SUBST target (QueryDosDevice) for capture / apply / disk only, and logs bk.log_unc.

## Watch out
- The elevated session connects to the share again: it works with the Windows user's own credentials or credentials saved in the Credential Manager; a mapping made with other, unsaved credentials can still fail (the helper's Not found text says so). EnableLinkedConnections would be the system-wide alternative; not set by the tool.

In the code: SessionFreePath in src/util.cpp, StartHelper in src/app.cpp, NotFound in src/helper.cpp; build.bat links mpr.lib.
