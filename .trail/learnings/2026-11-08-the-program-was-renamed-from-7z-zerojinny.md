---
title: The program was renamed from 7z_wimlib to WimSmith and takes over the old settings file
author: Zerojinny
date: 2026-11-08
branch: master
cost: { minutes: 4, tokens: 69163 }
id: l-8c5e59f7
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - MANUAL.ko.md
  - MANUAL.md
  - README.ko.md
  - README.md
  - lang/en.ini
  - lang/ko.ini
  - src/app.cpp
  - src/common.h
  - src/helper.cpp
---

## Decided
- The program is WimSmith (was 7z_wimlib) since 7-Zip is no longer used. The old ini is copied, not moved, so an old 7z_wimlib.exe in the same folder keeps working; the user deletes the old files.
- The project folder, git tags and backup\ keep the old name on purpose (history; the folder is also the session path).

## Watch out
- Korean texts need other particles after WimSmith (ends in a vowel sound): 이->가, 은->는, 을->를, 과->와.
- .trail/learnings still say 7z_wimlib: that is the same program.

In the code: src/settings.cpp SettingsFile(); namespace wimsmith; window classes WimSmith*; %TEMP%\WimSmith_job.
