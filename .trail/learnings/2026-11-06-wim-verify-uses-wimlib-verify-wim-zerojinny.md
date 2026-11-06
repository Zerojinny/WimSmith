---
title: "WIM verify uses wimlib_verify_wim with the integrity flag, and the helper's own error codes must not reuse wimlib codes"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 1, tokens: 1206 }
id: l-e3aaf14e
scope: repo
status: active
src: agent
---

## Decided
Verify (info tab button, and optional after a backup) runs in the helper without admin rights: open with the integrity-check open flag (checks the integrity table only if the file has one; most do not), then wimlib_verify_wim, which reads every blob and checks SHA-1 plus image metadata. Split WIMs work because the other parts are referenced first; any part can be chosen.
A cancelled verification after a backup still exits 0: the backup file is already written and fine.

## Tried and ruled out
Returning exit code 2 for "file / part not found": wimlib error 2 is "invalid compressed data", exactly what a damaged WIM gives, so the error text was swallowed. Own conditions now use a negative code (ErrPartsMissing = -2).

## Watch out
Progress struct offsets: verify_streams starts with a wimfile pointer, so total_bytes is at 16 and completed_bytes at 32 (checked against wimlib.h of 1.14). Message numbers: 16 verify integrity, 27 begin verify image, 29 verify streams.

In the code: src/WimLib.cs VerifyCore, Verify, OpenRead(openFlags), OnProgress; src/BackupPanel.cs StartVerify, chkVerify.
