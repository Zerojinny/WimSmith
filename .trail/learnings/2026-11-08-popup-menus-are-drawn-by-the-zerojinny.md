---
title: "Popup menus are drawn by the program, not darkened with the hidden uxtheme functions"
author: Zerojinny
date: 2026-11-08
branch: master
cost: { minutes: 16, tokens: 150874 }
id: l-36b09ed1
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - MANUAL.ko.md
  - MANUAL.md
  - lang/en.ini
  - lang/ko.ini
  - src/app.cpp
  - src/helper.cpp
  - src/tab_browse.cpp
  - src/tab_browse.h
  - src/wim.cpp
---

## Decided
The right-click / [Edit] menu of the browse tab was the light menu of Windows in the dark theme. Windows has no documented way to darken a Win32 popup menu. The user chose to draw it ourselves (owner-draw) so it looks the same in both themes and in WinPE.

## Tried and ruled out
- SetPreferredAppMode (uxtheme ordinal 135) + FlushMenuThemes (136): a few lines, but undocumented, may change with Windows updates, and does nothing without the theme service (WinPE stays light).

## Watch out
- Owner-draw items only colour the items; the menu frame needs the #32768 window itself: caught with a WH_CBT hook during TrackPopupMenu, subclassed for WM_NCPAINT and WM_PRINT (the fade animation renders through WM_PRINT). On Windows 11 DWMWA_BORDER_COLOR (34) colours the rounded frame.
- Windows adds the check-mark width to an owner-drawn item's itemWidth: subtract SM_CXMENUCHECK.
- Shortcut keys are a separate field (MenuItem::key), not "(F2)" in the language text.

In the code: ui::PopupMenu, MenuOwnerProc, MenuHook, MenuWindowSub in src/ui.cpp; BrowseTab::ShowMenu in src/tab_browse.cpp.
