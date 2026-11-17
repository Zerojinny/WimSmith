---
title: "Typing a letter in the browse file list selects the next name, answered through the virtual list's find request"
author: Zerojinny
date: 2026-11-17
branch: dev-1.3
cost: { minutes: 1, tokens: 1423 }
id: l-2f1d6d94
scope: repo
status: active
src: agent
touches:
  - src/tab_browse.cpp
---

## Decided
- Type-to-select in the browse tab's file list (a virtual list, LVS_OWNERDATA) is the list's own incremental search: the program only answers LVN_ODFINDITEMW with the row index, because the list already handles the timing, the repeated letter (next match) and the wrap; a hand-made WM_CHAR search would have to copy all of that. The folder tree needs no code, because a normal tree view searches its own item texts.

## Watch out
- Typing sends LVN_ODFINDITEMW with flags 0x26 = LVFI_STRING | LVFI_SUBSTRING | LVFI_WRAP, not LVFI_PARTIAL. Treating only LVFI_PARTIAL as "starts with" fails silently: every search became an exact match, so nothing was selected. Both mean "starts with" here.
- LVN_INCREMENTALSEARCHW comes before it; leaving it unanswered is fine, because then the list does its default search, which asks LVN_ODFINDITEMW.
- To test without a keyboard (SendInput would type into whatever window has the focus): post WM_CHAR to the list after a posted click on its empty area, because it needs the focus; read the selection with LVM_GETNEXTITEM (LVNI_SELECTED). A test WIM can be made with the helper without elevation (mode = capture, s1 / w1 / n1).

In the code: src/tab_browse.cpp, OnListNotify of the list, case LVN_ODFINDITEMW.
