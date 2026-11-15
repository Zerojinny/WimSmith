---
title: Boot once from a WIM was moved out of WimSmith into its own project
author: OWNER
date: 2026-11-15
cost: { minutes: 3, tokens: 68238 }
id: l-a14d662c
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - src/app.cpp
  - src/mainwin.cpp
---

## Decided
- The one-time RAM-disk boot from a WIM (right-click menu item "Boot once from this WIM") is no longer part of WimSmith: the user built it as a separate project. Do not add it back here.

## Watch out
- Shutdown / restart for "When done" used to live in the boot-once code; it now sits in app.cpp next to App::Shutdown.
- A .wim right-click menu registered by an older version (7 items) is made again at start because the item count differs, so the stale item disappears by itself.

In the code: src/app.cpp RunShutdown (static); src/mainwin.cpp WimMenu / UpdateMenuLanguage; removed: src/ramboot.*, helper mode `ramboot`, lang keys rb.*, st.row_ramboot, st.ramboot_delete_sdi, setting ramboot.delete_sdi.
