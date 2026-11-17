---
title: Fields share one frame drawn by the program; the edit and combo box traps behind it
author: Zerojinny
date: 2026-11-17
branch: dev-1.3
cost: { minutes: 14, tokens: 160620 }
id: l-d1554034
scope: repo
status: active
src: agent
touches:
  - src/ui.h
  - src/ui.cpp
  - src/tab_browse.cpp
  - src/mainwin.cpp
---

## Decided
- Every field (edit, combo box, list, tree) gets one frame: FieldBorder (3:1 against Back and Panel), Dim with focus, Border when disabled. Combo boxes are painted by the program in WM_PAINT so they match the edits; the DarkMode_CFD theme stays on them only for the drop-down list and its scroll bar.

## Watch out
- A classic edit control draws its WS_BORDER frame with its text, not in WM_NCPAINT: a frame drawn only in WM_NCPAINT is painted over at once. The old EditSub never showed its color for that reason (the screen showed #646464, COLOR_WINDOWFRAME). Draw the frame after WM_PAINT too.
- GetComboBoxInfo gives the combo box itself as hwndItem for CBS_DROPDOWNLIST. Treating that as "has an edit part" and clipping it out left every drop-down list blank white. Decide "editable" from the style.
- PrintWindow (WM_PRINT) does not go through WM_NCPAINT; to check frame colors, capture the screen (CopyFromScreen) or handle WM_PRINT with PRF_NONCLIENT as FrameSub does.

In the code: src/ui.cpp FrameSub / FieldFrame / PaintCombo / ComboSub; tokens ButtonDown and FieldBorder in src/ui.h and SetTheme.
